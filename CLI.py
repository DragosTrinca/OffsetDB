import socket
import cmd
import sys

class OffsetDBCLI(cmd.Cmd):
    intro = (
        "========================================\n"
        "  Connected successfully to OffsetDB\n"
        "  Type 'help' to see available commands\n"
        "========================================\n"
    )
    prompt = "OffsetDB> "

    def __init__(self, host = '127.0.0.1', port = 8080):
        cmd.Cmd.__init__(self)
        self.client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.client.connect((host, port))
        except ConnectionRefusedError:
            print(f"Error: Failed to connect to {host} : {port}")
            print("Make sure the OffsetDB server is running before turning on the client")
            sys.exit(1)

    def _send_command(self, command_str):
        try:
            self.client.sendall(command_str.encode('utf-8'))
            answer = self.client.recv(1024).decode('utf-8')

            if not answer:
                print("Error: Server has closed the connection")
                return True
            
            print(answer)
            
        except (ConnectionAbortedError, ConnectionResetError):
            print("Lost connection")
            return True

        except Exception as e:
            print(f"Network error: {e}")
            return True
        
        return False

    # Command definitions

    def do_ADD(self, arg):
        """Add a new entry\nADD <key> <value>"""
        if not arg or len(arg.split()) < 2:
            print("Invalid format. Use ADD <key> <value>")
            return
        
        self._send_command(f"ADD {arg}\n")
 
    def do_GET(self, arg):
        """Return the value assigned to a key\nGET <key>"""
        if not arg or len(arg.split()) != 1:
            print("Invalid format. Use GET <key>")

        self._send_command(f"GET {arg}\n")

    def do_DEL(self, arg):
        """Delete an entry from the database\nDEL <key>"""
        if not arg or len(arg.split()) != 1:
            print("Invalid format. Use DEL <key>")

        self._send_command(f"DEL {arg}\n")

    def do_COMPACT(self, arg):
        """Remove duplicate or deleted entries from log"""
        self._send_command("COMPACT\n")

    def do_exit(self, arg):
        """Close client CLI"""
        print("Closing client...")
        self.client.close()
        return True
    
    do_quit = do_exit

if __name__ == "__main__":
    try:
        OffsetDBCLI().cmdloop()
    except KeyboardInterrupt:
        print("\nEnding session")