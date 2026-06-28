#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

class OffsetDB {
private:
    std::unordered_map<std::string, std::streampos> db;

    std::ofstream logFile;
    std::shared_mutex dbMutex;

    std::string dbFilename;

    // Load commands from log file
    void loadFromFile(const std::string& filename) {
        std::ifstream inFile(filename);
        if (!inFile.is_open()) return;

        std::string line;

        std::streampos currentPos = inFile.tellg();

        while (std::getline(inFile, line)) {

            if (line.substr(0, 4) == "ADD ") {

                size_t commaPos = line.find(',');
                if (commaPos != std::string::npos) {
                    std::string key = line.substr(4, commaPos - 4);
                    std::string value = line.substr(commaPos + 1);
                    db[key] = currentPos;
                }
            }
            else if (line.substr(0, 4) == "DEL ") {

                std::string key = line.substr(4);
                db.erase(key);
            }

            currentPos = inFile.tellg();
        }
        std::cout << "Database loaded successfully\n";
    }

public:
    OffsetDB(const std::string& filename) {
        dbFilename = filename;

        loadFromFile(filename);

        logFile.open(filename, std::ios::app);
        if (!logFile.is_open())
            std::cerr << "Error: Could not open log file\n";
    }

    ~OffsetDB() {
        if (logFile.is_open())
            logFile.close();
    }

    void Compact() {
        std::unique_lock<std::shared_mutex> lock(dbMutex);

        if (logFile.is_open())
            logFile.close();

        std::string tempFilename = dbFilename + ".tmp";
        std::ofstream tempFile(tempFilename);

        std::unordered_map<std::string, std::streampos> newDb;

        std::ifstream oldFile(dbFilename);

        for (const auto& pair : db) {
            const std::string key = pair.first;
            std::streampos oldPos = pair.second;

            oldFile.seekg(oldPos);
            std::string line;
            std::getline(oldFile, line);

            size_t commaPos = line.find(',');
            if (commaPos != std::string::npos) {
                std::string value = line.substr(commaPos + 1);

                std::streampos newPos = tempFile.tellp();

                tempFile << key << "," << value << "\n";

                newDb[key] = newPos;
            }
        }

        tempFile.close();
        oldFile.close();

        std::filesystem::remove(dbFilename);
        std::filesystem::rename(tempFilename, dbFilename);

        logFile.open(dbFilename, std::ios::app);

        db = newDb;

        std::cout << "Compact done successfully\n";
    }

    void Add(const std::string& key, const std::string& value) {
        std::unique_lock<std::shared_mutex> lock(dbMutex);

        if (logFile.is_open()) {
            std::streampos writePos = logFile.tellp();

            logFile << "ADD " << key << "," << value << "\n";
            logFile.flush();

            db[key] = writePos;
        }

        std::cout << "Thread [" << std::this_thread::get_id() << "] added [" << key << "] -> [" << value << "]\n";
    }

    std::string Get(const std::string& key) {
        std::shared_lock<std::shared_mutex> lock(dbMutex);

        auto it = db.find(key);

        if (it != db.end()) {
            std::streampos pos = it->second;

            std::ifstream inFile(dbFilename);
            if (!inFile.is_open())
                std::cerr << "Error: Could not read disk\n";

            inFile.seekg(pos);

            std::string line;
            std::getline(inFile, line);

            size_t commaPos = line.find(',');
            if (commaPos != std::string::npos) {
                std::string value = line.substr(commaPos + 1);
                std::cout << "Data loaded from disk for key [" << key << "]\n";
                return value;
            }
            else
                std::cerr << "Error: Data corrupted on disk\n";
        }
        else {
            std::cout << "Key [" << key << "] not found\n";
            return "Key not found";
        }
    }

    std::string Delete(const std::string& key) {
        std::unique_lock<std::shared_mutex> lock(dbMutex);

        if (db.erase(key)) {
            if (logFile.is_open()) {
                logFile << "DEL " << key << "\n";
                logFile.flush();
            }
            std::cout << "Key [" << key << "] deleted\n";
            return "Key deleted";
        }
        else {
            std::cout << "Key [" << key << "] not found\n";
            return "Key not found";
        }
    }
};

void HandleClient(SOCKET clientSocket, OffsetDB& db) {
    while (true) {
        char buffer[1024] = { 0 };
        int bytesReceived = recv(clientSocket, buffer, 1024, 0);

        if (bytesReceived > 0) {
            std::string command(buffer, bytesReceived);
            std::cout << "Command received: " << command << "\n";

            std::stringstream ss(command);
            std::string operation, key, value, response;

            ss >> operation;

            if (operation == "ADD") {
                ss >> key;
                std::getline(ss, value);

                // Remove starting white spaces
                while (!value.empty() && value[0] == ' ')
                    value.erase(0, 1);

                if (!key.empty() && !value.empty()) {
                    db.Add(key, value);
                    response = "OK\n";
                }
                else
                    response = "Error: Invalid format. Use: ADD key value\n";
            }
            else if (operation == "GET") {
                ss >> key;
                if (!key.empty())
                    response = db.Get(key) + "\n";
                else
                    response = "Error: Invalid format. Use: GET key\n";
            }
            else if (operation == "DEL") {
                ss >> key;
                if (!key.empty())
                    response = db.Delete(key) + "\n";
                else
                    response = "Error: Invalid format. Use: DEL key\n";
            }
            else if (operation == "COMPACT") {
                db.Compact();
                response = "Compact success\n";
            }
            else {
                response = "Error: Unknown command\n";
            }
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        else {
            std::cout << "Client disconnected\n";
            break;
        }
    }
    closesocket(clientSocket);
}

void StartServer(OffsetDB& db) {
    // Initialize winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Error: could not initialize winsock\n";
        return;
    }

    // Create socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Error: could not create socket\n";
        WSACleanup();
        return;
    }

    // Configure server address
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    // Bind socket to port 8080
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Error: could not bind socket\n";
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    listen(serverSocket, SOMAXCONN);
    std::cout << "Listening to port 8080\n";

    // Server main loop
    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) continue;

        std::cout << "New client connected\n";

        std::thread clientThread(HandleClient, clientSocket, std::ref(db));

        clientThread.detach();
    }

    closesocket(serverSocket);
    WSACleanup();
}

int main()
{
    OffsetDB myDb("database_log.txt");
    StartServer(myDb);
    return 0;
}
