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
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>

class OffsetDB {
private:
    std::unordered_map<std::string, std::streampos> db;

    std::ofstream logFile;
    std::shared_mutex dbMutex;

    std::string baseFilename;
    std::string currentFilename;
    std::atomic<int> dbVersion{ 1 };

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
        baseFilename = filename;
        currentFilename = baseFilename + "_v1.txt";

        loadFromFile(currentFilename);

        logFile.open(currentFilename, std::ios::app);
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

        int nextVersion = dbVersion.load() + 1;
        std::string newFilename = baseFilename + "_v" + std::to_string(nextVersion) + ".txt";

        std::ofstream tempFile(newFilename);

        std::unordered_map<std::string, std::streampos> newDb;

        std::ifstream oldFile(currentFilename);

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

        currentFilename = newFilename;
        db = newDb;
        dbVersion.store(nextVersion);

        logFile.open(currentFilename, std::ios::app);

        std::cout << "Compact done. New active file: " << currentFilename << "\n";
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

            thread_local std::ifstream inFile;
            thread_local int localVersion = 0;

            if (!inFile.is_open() || localVersion != dbVersion.load()) {
                if (inFile.is_open())
                    inFile.close();

                inFile.open(currentFilename, std::ios::binary);
                localVersion = dbVersion.load();

                if (!inFile.is_open())
                    return "Error: Could not read disk";
            }
            else inFile.clear();

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
                return "Error: Data corrupted on disk";
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
    // Temporary buffer
    char buffer[1024] = { 0 };

    // Buffer that persists between recv() calls
    std::string clientBuffer;

    while (true) {
        // Clear temporary buffer
        memset(buffer, 0, sizeof(buffer));

        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived > 0) {
            // Put new data in the buffer
            clientBuffer.append(buffer, bytesReceived);

            // Look for all full commands inside the buffer
            size_t pos;
            while ((pos = clientBuffer.find('\n')) != std::string::npos) {
                std::string command = clientBuffer.substr(0, pos);

                // Remove command from buffer
                clientBuffer.erase(0, pos + 1);

                if (!command.empty() && command.back() == '\r')
                    command.pop_back();

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

            
        }
        else if (bytesReceived == 0) {
            std::cout << "Client disconnected\n";
            break;
        }
        else {
            std::cout << "Client connection error\n";
            break;
        }
    }
    closesocket(clientSocket);
}

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;

public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; i++) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);

                        // Wait for a task or the stop command
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                            });

                        if (this->stop && this->tasks.empty())
                            return;

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    // Execute task
                    task();
                }
                });
        }
    }

    void Enqueue(std::function<void()> task) {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.emplace(std::move(task));
        condition.notify_one();
    }

    ~ThreadPool() {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
    }
};

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

    // Number of threads may be increased
    ThreadPool pool(8);

    // Server main loop
    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) continue;

        std::cout << "New client connected\n";

        pool.Enqueue([clientSocket, &db]() {
            HandleClient(clientSocket, db);
            });
    }

    closesocket(serverSocket);
    WSACleanup();
}

int main()
{
    OffsetDB myDb("database_log");
    StartServer(myDb);
    return 0;
}
