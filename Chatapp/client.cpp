#include "common.h"
#include <atomic>
#include <process.h>
#include <thread>
#include <chrono>
#include <map>

class ChatClient {
private:
    SOCKET clientSocket;
    std::string username;
    std::string serverIP;
    int port;
    std::atomic<bool> connected;
    HANDLE receiveThread;
    HANDLE inputThread;
    std::string lastPrivateSender;
    std::map<std::string, std::vector<std::string>> privateChatHistory;
    
    static unsigned int __stdcall receiveMessagesStatic(void* param) {
        ChatClient* client = (ChatClient*)param;
        client->receiveMessages();
        return 0;
    }
    
    static unsigned int __stdcall inputHandlerStatic(void* param) {
        ChatClient* client = (ChatClient*)param;
        client->handleInput();
        return 0;
    }
    
public:
    ChatClient(const std::string& ip, int portNum) : 
        serverIP(ip), port(portNum), connected(false) {
        clientSocket = INVALID_SOCKET;
        receiveThread = NULL;
        inputThread = NULL;
        lastPrivateSender = "";
    }
    
    ~ChatClient() {
        disconnect();
    }
    
    bool connectToServer() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
        
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            WSACleanup();
            return false;
        }
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = inet_addr(serverIP.c_str());
        
        if (::connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Failed to connect to server. Make sure the server is running." << std::endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }
        
        // Send username
        send(clientSocket, username.c_str(), username.length(), 0);
        
        // Wait for response
        ChatMessage response;
        int bytesReceived = recv(clientSocket, (char*)&response, sizeof(response), 0);
        if (bytesReceived > 0 && response.type == MSG_ERROR) {
            std::cerr << "Server error: " << response.content << std::endl;
            closesocket(clientSocket);
            WSACleanup();
            return false;
        }
        
        std::cout << "\n✓ Connected to chat server as '" << username << "'" << std::endl;
        
        connected = true;
        receiveThread = (HANDLE)_beginthreadex(NULL, 0, receiveMessagesStatic, this, 0, NULL);
        inputThread = (HANDLE)_beginthreadex(NULL, 0, inputHandlerStatic, this, 0, NULL);
        
        return true;
    }
    
    void sendTypingIndicator(const std::string& to) {
        if (!connected) return;
        
        ChatMessage msg;
        msg.type = MSG_TYPING;
        strcpy(msg.sender, username.c_str());
        strcpy(msg.recipient, to.c_str());
        send(clientSocket, (char*)&msg, sizeof(msg), 0);
    }
    
    void receiveMessages() {
        ChatMessage msg;
        
        while (connected) {
            int bytesReceived = recv(clientSocket, (char*)&msg, sizeof(msg), 0);
            
            if (bytesReceived <= 0) {
                if (connected) {
                    std::cout << "\n⚠ Disconnected from server" << std::endl;
                    connected = false;
                }
                break;
            }
            
            // Clear the current input line
            std::cout << "\r\033[K";
            
            // Display message based on type
            switch(msg.type) {
                case MSG_NORMAL:
                    std::cout << "💬 [" << msg.sender << "]: " << msg.content << std::endl;
                    break;
                    
                case MSG_PRIVATE:
                    if (strcmp(msg.sender, "System") == 0) {
                        std::cout << "⚙ " << msg.content << std::endl;
                    } else {
                        std::cout << msg.content << std::endl;
                        // Store in private chat history
                        lastPrivateSender = msg.sender;
                        privateChatHistory[msg.sender].push_back(msg.content);
                    }
                    break;
                    
                case MSG_TYPING:
                    std::cout << "✏️ " << msg.sender << " " << msg.content << std::endl;
                    break;
                    
                case MSG_READ_RECEIPT:
                    std::cout << "✓✓ " << msg.sender << " has read your message" << std::endl;
                    break;
                    
                case MSG_JOIN:
                    std::cout << "🟢 " << msg.content << std::endl;
                    break;
                    
                case MSG_LEAVE:
                    std::cout << "🔴 " << msg.content << std::endl;
                    break;
                    
                case MSG_LIST_USERS:
                    std::cout << "👥 " << msg.content << std::endl;
                    break;
                    
                case MSG_PRIVATE_HISTORY:
                    std::cout << "\n📜 " << msg.content << std::endl;
                    break;
                    
                case MSG_ERROR:
                    std::cout << "❌ Error: " << msg.content << std::endl;
                    break;
            }
            
            std::cout << "> " << std::flush;
        }
    }
    
    void handleInput() {
        std::string input;
        
        while (connected) {
            std::getline(std::cin, input);
            if (!input.empty() && connected) {
                sendMessage(input);
            }
        }
    }
    
    void sendMessage(const std::string& message) {
        if (!connected) return;
        
        if (message[0] == '/') {
            handleCommand(message);
            return;
        }
        
        ChatMessage msg;
        msg.type = MSG_NORMAL;
        strcpy(msg.sender, username.c_str());
        strcpy(msg.content, message.c_str());
        msg.timestamp = time(nullptr);
        send(clientSocket, (char*)&msg, sizeof(msg), 0);
    }
    
    void handleCommand(const std::string& command) {
        if (command == "/quit" || command == "/exit") {
            std::cout << "Disconnecting..." << std::endl;
            disconnect();
        }
        else if (command == "/help") {
            printBanner();
            std::cout << "> " << std::flush;
        }
        else if (command == "/users") {
            ChatMessage msg;
            msg.type = MSG_LIST_USERS;
            strcpy(msg.sender, username.c_str());
            send(clientSocket, (char*)&msg, sizeof(msg), 0);
        }
        else if (command == "/reply" || command.substr(0, 6) == "/reply") {
            if (lastPrivateSender.empty()) {
                std::cout << "No one has sent you a private message yet. Use /whisper instead." << std::endl;
                std::cout << "> " << std::flush;
                return;
            }
            
            std::string message;
            if (command == "/reply") {
                std::cout << "Reply to " << lastPrivateSender << ": ";
                std::getline(std::cin, message);
            } else {
                message = command.substr(6);
                // Trim leading spaces
                size_t start = message.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    message = message.substr(start);
                }
            }
            
            if (!message.empty()) {
                sendPrivateMessage(lastPrivateSender, message);
            }
        }
        else if (command.substr(0, 8) == "/whisper" || command.substr(0, 2) == "/w") {
            std::string rest;
            if (command.substr(0, 8) == "/whisper") {
                rest = command.substr(8);
            } else {
                rest = command.substr(2);
            }
            
            // Trim leading spaces
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                rest = rest.substr(start);
            }
            
            size_t spacePos = rest.find(' ');
            if (spacePos != std::string::npos) {
                std::string recipient = rest.substr(0, spacePos);
                std::string message = rest.substr(spacePos + 1);
                sendPrivateMessage(recipient, message);
            } else {
                std::cout << "Usage: /whisper <username> <message> or /w <username> <message>" << std::endl;
                std::cout << "> " << std::flush;
            }
        }
        else if (command.substr(0, 7) == "/history") {
            std::string rest = command.substr(7);
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                std::string user = rest.substr(start);
                // Remove any trailing spaces
                user.erase(std::find_if(user.rbegin(), user.rend(), 
                          [](unsigned char ch) { return !std::isspace(ch); }).base(), user.end());
                
                if (!user.empty()) {
                    ChatMessage msg;
                    msg.type = MSG_PRIVATE_HISTORY;
                    strcpy(msg.sender, username.c_str());
                    strcpy(msg.recipient, user.c_str());
                    send(clientSocket, (char*)&msg, sizeof(msg), 0);
                } else {
                    std::cout << "Usage: /history <username>" << std::endl;
                    std::cout << "> " << std::flush;
                }
            } else {
                std::cout << "Usage: /history <username>" << std::endl;
                std::cout << "> " << std::flush;
            }
        }
        else if (command.substr(0, 6) == "/block") {
            std::string rest = command.substr(6);
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                std::string user = rest.substr(start);
                user.erase(std::find_if(user.rbegin(), user.rend(), 
                          [](unsigned char ch) { return !std::isspace(ch); }).base(), user.end());
                
                if (!user.empty()) {
                    ChatMessage msg;
                    msg.type = MSG_BLOCK_USER;
                    strcpy(msg.sender, username.c_str());
                    strcpy(msg.recipient, user.c_str());
                    send(clientSocket, (char*)&msg, sizeof(msg), 0);
                } else {
                    std::cout << "Usage: /block <username>" << std::endl;
                    std::cout << "> " << std::flush;
                }
            } else {
                std::cout << "Usage: /block <username>" << std::endl;
                std::cout << "> " << std::flush;
            }
        }
        else if (command.substr(0, 8) == "/unblock") {
            std::string rest = command.substr(8);
            size_t start = rest.find_first_not_of(" \t");
            if (start != std::string::npos) {
                std::string user = rest.substr(start);
                user.erase(std::find_if(user.rbegin(), user.rend(), 
                          [](unsigned char ch) { return !std::isspace(ch); }).base(), user.end());
                
                if (!user.empty()) {
                    ChatMessage msg;
                    msg.type = MSG_UNBLOCK_USER;
                    strcpy(msg.sender, username.c_str());
                    strcpy(msg.recipient, user.c_str());
                    send(clientSocket, (char*)&msg, sizeof(msg), 0);
                } else {
                    std::cout << "Usage: /unblock <username>" << std::endl;
                    std::cout << "> " << std::flush;
                }
            } else {
                std::cout << "Usage: /unblock <username>" << std::endl;
                std::cout << "> " << std::flush;
            }
        }
        else if (command == "/blocked") {
            ChatMessage msg;
            msg.type = MSG_BLOCKED_LIST;
            strcpy(msg.sender, username.c_str());
            send(clientSocket, (char*)&msg, sizeof(msg), 0);
        }
        else {
            std::cout << "Unknown command. Type /help for available commands." << std::endl;
            std::cout << "> " << std::flush;
        }
    }
    
    void sendPrivateMessage(const std::string& recipient, const std::string& message) {
        ChatMessage msg;
        msg.type = MSG_WHISPER;
        strcpy(msg.sender, username.c_str());
        strcpy(msg.recipient, recipient.c_str());
        strcpy(msg.content, message.c_str());
        send(clientSocket, (char*)&msg, sizeof(msg), 0);
        
        // Simulate typing indicator (remove the '//' to enable)
        // std::thread([this, recipient]() {
        //     sendTypingIndicator(recipient);
        // }).detach();
    }
    
    void disconnect() {
        if (connected) {
            connected = false;
            if (clientSocket != INVALID_SOCKET) {
                closesocket(clientSocket);
                clientSocket = INVALID_SOCKET;
            }
            if (receiveThread) {
                WaitForSingleObject(receiveThread, 2000);
                CloseHandle(receiveThread);
                receiveThread = NULL;
            }
            if (inputThread) {
                WaitForSingleObject(inputThread, 2000);
                CloseHandle(inputThread);
                inputThread = NULL;
            }
            WSACleanup();
            std::cout << "Disconnected from chat. Goodbye!" << std::endl;
        }
    }
    
    void setUsername(const std::string& name) {
        username = name;
    }
    
    void run() {
        std::cout << "\nConnected! Type /help for commands." << std::endl;
        std::cout << "> " << std::flush;
        
        while (connected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

int main(int argc, char* argv[]) {
    std::string serverIP = "127.0.0.1";
    int port = 8888;
    std::string username;
    
    system("cls");
    printBanner();
    
    std::cout << "\nEnter your username: ";
    std::getline(std::cin, username);
    
    // Remove any whitespace
    username.erase(std::remove_if(username.begin(), username.end(), ::isspace), username.end());
    
    if (username.empty()) {
        username = "Anonymous";
    }
    
    if (argc > 1) serverIP = argv[1];
    if (argc > 2) port = atoi(argv[2]);
    
    std::cout << "\nConnecting to " << serverIP << ":" << port << "..." << std::endl;
    
    ChatClient client(serverIP, port);
    client.setUsername(username);
    
    if (!client.connectToServer()) {
        std::cerr << "\nFailed to connect to server. Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }
    
    client.run();
    return 0;
}