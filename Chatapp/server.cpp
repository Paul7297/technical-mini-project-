#include "common.h"
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include <process.h>
#include <sstream>
#include <queue>

class ChatServer {
private:
    SOCKET serverSocket;
    int port;
    bool running;
    
    struct ClientInfo {
        std::string username;
        SOCKET socket;
        std::set<std::string> blockedUsers;
        std::vector<PrivateMessage> privateHistory;
        std::string lastPrivateSender;
    };
    
    std::map<SOCKET, ClientInfo> clients;
    HANDLE clientsMutex;
    std::vector<HANDLE> clientThreads;
    
    // Store private messages for offline users (max 100 per user)
    std::map<std::string, std::queue<PrivateMessage>> offlineMessages;
    
public:
    ChatServer(int portNum) : port(portNum), running(true) {
        serverSocket = INVALID_SOCKET;
        clientsMutex = CreateMutex(NULL, FALSE, NULL);
    }
    
    ~ChatServer() {
        cleanup();
        if (clientsMutex) CloseHandle(clientsMutex);
        for (HANDLE h : clientThreads) {
            if (h) CloseHandle(h);
        }
    }
    
    bool initialize() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
        
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            WSACleanup();
            return false;
        }
        
        int opt = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, 
                       (char*)&opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }
        
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);
        
        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Failed to bind socket" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }
        
        if (listen(serverSocket, 10) == SOCKET_ERROR) {
            std::cerr << "Failed to listen on socket" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }
        
        std::cout << "Server started on port " << port << std::endl;
        return true;
    }
    
    void broadcastMessage(const ChatMessage& msg, SOCKET excludeClient = INVALID_SOCKET) {
        WaitForSingleObject(clientsMutex, INFINITE);
        for (auto& client : clients) {
            if (client.second.socket != excludeClient) {
                send(client.second.socket, (char*)&msg, sizeof(msg), 0);
            }
        }
        ReleaseMutex(clientsMutex);
    }
    
    void sendTypingIndicator(const std::string& from, const std::string& to) {
        WaitForSingleObject(clientsMutex, INFINITE);
        
        for (auto& client : clients) {
            if (client.second.username == to) {
                // Check if not blocked
                if (client.second.blockedUsers.find(from) == client.second.blockedUsers.end()) {
                    ChatMessage typingMsg;
                    typingMsg.type = MSG_TYPING;
                    strcpy(typingMsg.sender, from.c_str());
                    strcpy(typingMsg.recipient, to.c_str());
                    strcpy(typingMsg.content, "is typing...");
                    send(client.second.socket, (char*)&typingMsg, sizeof(typingMsg), 0);
                }
                break;
            }
        }
        ReleaseMutex(clientsMutex);
    }
    
    void storePrivateMessage(const std::string& from, const std::string& to, 
                            const std::string& content, time_t timestamp) {
        PrivateMessage pm;
        pm.from = from;
        pm.to = to;
        pm.content = content;
        pm.timestamp = timestamp;
        pm.isRead = false;
        
        // Store in sender's history
        for (auto& client : clients) {
            if (client.second.username == from) {
                client.second.privateHistory.push_back(pm);
                // Keep only last 100 messages
                if (client.second.privateHistory.size() > 100) {
                    client.second.privateHistory.erase(client.second.privateHistory.begin());
                }
                break;
            }
        }
        
        // Store in recipient's history
        bool recipientOnline = false;
        for (auto& client : clients) {
            if (client.second.username == to) {
                client.second.privateHistory.push_back(pm);
                if (client.second.privateHistory.size() > 100) {
                    client.second.privateHistory.erase(client.second.privateHistory.begin());
                }
                recipientOnline = true;
                break;
            }
        }
        
        // If recipient is offline, store message for later delivery
        if (!recipientOnline) {
            offlineMessages[to].push(pm);
            // Limit offline messages to 50 per user
            if (offlineMessages[to].size() > 50) {
                offlineMessages[to].pop();
            }
        }
    }
    
    void sendOfflineMessages(SOCKET clientSocket, const std::string& username) {
        if (offlineMessages.find(username) != offlineMessages.end()) {
            while (!offlineMessages[username].empty()) {
                PrivateMessage pm = offlineMessages[username].front();
                offlineMessages[username].pop();
                
                ChatMessage msg;
                msg.type = MSG_PRIVATE;
                strcpy(msg.sender, pm.from.c_str());
                strcpy(msg.recipient, username.c_str());
                strcpy(msg.content, pm.content.c_str());
                msg.timestamp = pm.timestamp;
                
                // Add indicator that this was an offline message
                char offlineNotice[200];
                snprintf(offlineNotice, sizeof(offlineNotice), 
                        "[Offline message from %s at %s]: %s", 
                        pm.from.c_str(), formatTimestamp(pm.timestamp).c_str(), pm.content.c_str());
                strcpy(msg.content, offlineNotice);
                
                send(clientSocket, (char*)&msg, sizeof(msg), 0);
            }
        }
    }
    
    void sendPrivateMessage(SOCKET sender, const std::string& recipient, const std::string& content) {
        WaitForSingleObject(clientsMutex, INFINITE);
        
        std::string senderName;
        SOCKET senderSocket = INVALID_SOCKET;
        
        for (auto& client : clients) {
            if (client.second.socket == sender) {
                senderName = client.second.username;
                senderSocket = client.first;
                break;
            }
        }
        
        if (senderName.empty()) {
            ReleaseMutex(clientsMutex);
            return;
        }
        
        // Check if recipient exists
        bool recipientExists = false;
        SOCKET recipientSocket = INVALID_SOCKET;
        bool isBlocked = false;
        
        for (auto& client : clients) {
            if (client.second.username == recipient) {
                recipientExists = true;
                recipientSocket = client.first;
                // Check if sender is blocked by recipient
                if (client.second.blockedUsers.find(senderName) != client.second.blockedUsers.end()) {
                    isBlocked = true;
                }
                break;
            }
        }
        
        if (!recipientExists) {
            ChatMessage errorMsg;
            errorMsg.type = MSG_ERROR;
            strcpy(errorMsg.sender, "System");
            snprintf(errorMsg.content, sizeof(errorMsg.content), 
                    "User '%s' does not exist", recipient.c_str());
            send(sender, (char*)&errorMsg, sizeof(errorMsg), 0);
            ReleaseMutex(clientsMutex);
            return;
        }
        
        if (isBlocked) {
            ChatMessage errorMsg;
            errorMsg.type = MSG_ERROR;
            strcpy(errorMsg.sender, "System");
            snprintf(errorMsg.content, sizeof(errorMsg.content), 
                    "You are blocked by '%s'. Message not delivered.", recipient.c_str());
            send(sender, (char*)&errorMsg, sizeof(errorMsg), 0);
            ReleaseMutex(clientsMutex);
            return;
        }
        
        // Store message in history
        time_t now = time(nullptr);
        storePrivateMessage(senderName, recipient, content, now);
        
        // Update last private sender for recipient
        for (auto& client : clients) {
            if (client.second.username == recipient) {
                client.second.lastPrivateSender = senderName;
                break;
            }
        }
        
        // Send to recipient
        ChatMessage msgToRecipient;
        msgToRecipient.type = MSG_PRIVATE;
        strcpy(msgToRecipient.sender, senderName.c_str());
        strcpy(msgToRecipient.recipient, recipient.c_str());
        strcpy(msgToRecipient.content, content.c_str());
        msgToRecipient.timestamp = now;
        
        std::string formattedMsg = "🔒 [Private from " + senderName + " at " + 
                                   formatTimestamp(now) + "]: " + content;
        strcpy(msgToRecipient.content, formattedMsg.c_str());
        
        send(recipientSocket, (char*)&msgToRecipient, sizeof(msgToRecipient), 0);
        
        // Send delivery confirmation to sender
        ChatMessage confirmMsg;
        confirmMsg.type = MSG_PRIVATE;
        strcpy(confirmMsg.sender, "System");
        snprintf(confirmMsg.content, sizeof(confirmMsg.content), 
                "✓ Message sent to '%s'", recipient.c_str());
        send(sender, (char*)&confirmMsg, sizeof(confirmMsg), 0);
        
        // Send read receipt request
        ChatMessage receiptMsg;
        receiptMsg.type = MSG_READ_RECEIPT;
        strcpy(receiptMsg.sender, senderName.c_str());
        strcpy(receiptMsg.recipient, recipient.c_str());
        strcpy(receiptMsg.content, "READ_REQUEST");
        send(recipientSocket, (char*)&receiptMsg, sizeof(receiptMsg), 0);
        
        ReleaseMutex(clientsMutex);
        
        std::cout << "[Private] " << senderName << " -> " << recipient << ": " << content << std::endl;
    }
    
    void blockUser(SOCKET clientSocket, const std::string& userToBlock) {
        WaitForSingleObject(clientsMutex, INFINITE);
        
        std::string username;
        for (auto& client : clients) {
            if (client.second.socket == clientSocket) {
                username = client.second.username;
                break;
            }
        }
        
        if (username.empty()) {
            ReleaseMutex(clientsMutex);
            return;
        }
        
        // Check if user exists
        bool userExists = false;
        for (auto& client : clients) {
            if (client.second.username == userToBlock) {
                userExists = true;
                break;
            }
        }
        
        if (!userExists) {
            ChatMessage errorMsg;
            errorMsg.type = MSG_ERROR;
            strcpy(errorMsg.sender, "System");
            snprintf(errorMsg.content, sizeof(errorMsg.content), 
                    "User '%s' does not exist", userToBlock.c_str());
            send(clientSocket, (char*)&errorMsg, sizeof(errorMsg), 0);
            ReleaseMutex(clientsMutex);
            return;
        }
        
        // Block the user
        for (auto& client : clients) {
            if (client.second.socket == clientSocket) {
                client.second.blockedUsers.insert(userToBlock);
                
                ChatMessage successMsg;
                successMsg.type = MSG_NORMAL;
                strcpy(successMsg.sender, "System");
                snprintf(successMsg.content, sizeof(successMsg.content), 
                        "✓ You have blocked '%s'", userToBlock.c_str());
                send(clientSocket, (char*)&successMsg, sizeof(successMsg), 0);
                break;
            }
        }
        
        ReleaseMutex(clientsMutex);
    }
    
    void unblockUser(SOCKET clientSocket, const std::string& userToUnblock) {
        WaitForSingleObject(clientsMutex, INFINITE);
        
        for (auto& client : clients) {
            if (client.second.socket == clientSocket) {
                auto it = client.second.blockedUsers.find(userToUnblock);
                if (it != client.second.blockedUsers.end()) {
                    client.second.blockedUsers.erase(it);
                    
                    ChatMessage successMsg;
                    successMsg.type = MSG_NORMAL;
                    strcpy(successMsg.sender, "System");
                    snprintf(successMsg.content, sizeof(successMsg.content), 
                            "✓ You have unblocked '%s'", userToUnblock.c_str());
                    send(clientSocket, (char*)&successMsg, sizeof(successMsg), 0);
                } else {
                    ChatMessage errorMsg;
                    errorMsg.type = MSG_ERROR;
                    strcpy(errorMsg.sender, "System");
                    snprintf(errorMsg.content, sizeof(errorMsg.content), 
                            "'%s' is not in your block list", userToUnblock.c_str());
                    send(clientSocket, (char*)&errorMsg, sizeof(errorMsg), 0);
                }
                break;
            }
        }
        
        ReleaseMutex(clientsMutex);
    }
    
    void showBlockedList(SOCKET clientSocket) {
        WaitForSingleObject(clientsMutex, INFINITE);
        
        for (auto& client : clients) {
            if (client.second.socket == clientSocket) {
                std::string blockedList = "Blocked users: ";
                if (client.second.blockedUsers.empty()) {
                    blockedList += "none";
                } else {
                    for (const auto& blocked : client.second.blockedUsers) {
                        blockedList += blocked + " ";
                    }
                }
                
                ChatMessage msg;
                msg.type = MSG_LIST_USERS;
                strcpy(msg.sender, "System");
                strcpy(msg.content, blockedList.c_str());
                send(clientSocket, (char*)&msg, sizeof(msg), 0);
                break;
            }
        }
        
        ReleaseMutex(clientsMutex);
    }
    
    void showPrivateHistory(SOCKET clientSocket, const std::string& withUser) {
        WaitForSingleObject(clientsMutex, INFINITE);
        
        for (auto& client : clients) {
            if (client.second.socket == clientSocket) {
                ChatMessage msg;
                msg.type = MSG_PRIVATE_HISTORY;
                strcpy(msg.sender, "System");
                
                std::string history = "Private chat history with " + withUser + ":\n";
                bool found = false;
                
                for (const auto& pm : client.second.privateHistory) {
                    if (pm.from == withUser || pm.to == withUser) {
                        found = true;
                        std::string direction = (pm.from == client.second.username) ? "You -> " : pm.from + " -> ";
                        history += "[" + formatTimestamp(pm.timestamp) + "] " + direction + pm.content + "\n";
                    }
                }
                
                if (!found) {
                    history += "No messages exchanged with " + withUser;
                }
                
                strcpy(msg.content, history.c_str());
                send(clientSocket, (char*)&msg, sizeof(msg), 0);
                break;
            }
        }
        
        ReleaseMutex(clientsMutex);
    }
    
    static unsigned int __stdcall handleClientStatic(void* param) {
        ChatServer* server = (ChatServer*)param;
        server->handleClient();
        return 0;
    }
    
    void handleClient() {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) return;
        
        char buffer[1024];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            closesocket(clientSocket);
            return;
        }
        
        buffer[bytesReceived] = '\0';
        std::string username(buffer);
        username.erase(std::remove(username.begin(), username.end(), '\n'), username.end());
        username.erase(std::remove(username.begin(), username.end(), '\r'), username.end());
        
        // Check if username already exists
        WaitForSingleObject(clientsMutex, INFINITE);
        bool nameExists = false;
        for (auto& client : clients) {
            if (client.second.username == username) {
                nameExists = true;
                break;
            }
        }
        
        if (nameExists) {
            ChatMessage errorMsg;
            errorMsg.type = MSG_ERROR;
            strcpy(errorMsg.sender, "Server");
            strcpy(errorMsg.content, "Username already taken. Please reconnect with a different name.");
            send(clientSocket, (char*)&errorMsg, sizeof(errorMsg), 0);
            ReleaseMutex(clientsMutex);
            closesocket(clientSocket);
            return;
        }
        
        ClientInfo info;
        info.username = username;
        info.socket = clientSocket;
        clients[clientSocket] = info;
        ReleaseMutex(clientsMutex);
        
        std::cout << "User '" << username << "' connected" << std::endl;
        
        // Send offline messages
        sendOfflineMessages(clientSocket, username);
        
        // Announce new user
        ChatMessage joinMsg;
        joinMsg.type = MSG_JOIN;
        strcpy(joinMsg.sender, "Server");
        snprintf(joinMsg.content, sizeof(joinMsg.content), 
                "%s has joined the chat", username.c_str());
        joinMsg.timestamp = time(nullptr);
        broadcastMessage(joinMsg, clientSocket);
        
        // Send welcome message
        ChatMessage welcomeMsg;
        welcomeMsg.type = MSG_NORMAL;
        strcpy(welcomeMsg.sender, "Server");
        strcpy(welcomeMsg.content, "Welcome to the chat! Type /help for commands.");
        send(clientSocket, (char*)&welcomeMsg, sizeof(welcomeMsg), 0);
        
        // Send user list
        sendUserList(clientSocket);
        
        // Handle messages
        ChatMessage msg;
        while (running) {
            bytesReceived = recv(clientSocket, (char*)&msg, sizeof(msg), 0);
            
            if (bytesReceived <= 0) {
                WaitForSingleObject(clientsMutex, INFINITE);
                std::cout << "User '" << username << "' disconnected" << std::endl;
                
                ChatMessage leaveMsg;
                leaveMsg.type = MSG_LEAVE;
                strcpy(leaveMsg.sender, "Server");
                snprintf(leaveMsg.content, sizeof(leaveMsg.content), 
                        "%s has left the chat", username.c_str());
                leaveMsg.timestamp = time(nullptr);
                
                clients.erase(clientSocket);
                ReleaseMutex(clientsMutex);
                
                broadcastMessage(leaveMsg, clientSocket);
                break;
            }
            
            switch(msg.type) {
                case MSG_NORMAL:
                    std::cout << "[" << msg.sender << "]: " << msg.content << std::endl;
                    msg.timestamp = time(nullptr);
                    broadcastMessage(msg, clientSocket);
                    break;
                    
                case MSG_WHISPER:
                    sendPrivateMessage(clientSocket, msg.recipient, msg.content);
                    break;
                    
                case MSG_TYPING:
                    sendTypingIndicator(msg.sender, msg.recipient);
                    break;
                    
                case MSG_LIST_USERS:
                    sendUserList(clientSocket);
                    break;
                    
                default:
                    break;
            }
        }
        
        closesocket(clientSocket);
    }
    
    void sendUserList(SOCKET clientSocket) {
        WaitForSingleObject(clientsMutex, INFINITE);
        std::stringstream userList;
        userList << "Online users (" << clients.size() << "): ";
        
        for (const auto& client : clients) {
            userList << client.second.username << " ";
        }
        
        ChatMessage msg;
        msg.type = MSG_LIST_USERS;
        strcpy(msg.sender, "Server");
        strcpy(msg.content, userList.str().c_str());
        msg.timestamp = time(nullptr);
        send(clientSocket, (char*)&msg, sizeof(msg), 0);
        ReleaseMutex(clientsMutex);
    }
    
    void run() {
        std::cout << "Server is running. Waiting for connections..." << std::endl;
        
        while (running) {
            HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, handleClientStatic, this, 0, NULL);
            if (thread) {
                clientThreads.push_back(thread);
            }
        }
    }
    
    void cleanup() {
        running = false;
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
        }
        WSACleanup();
    }
};

int main(int argc, char* argv[]) {
    int port = 8888;
    
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    printBanner();
    std::cout << "Starting chat server on port " << port << std::endl;
    std::cout << "Enhanced private messaging system active!" << std::endl;
    std::cout << "Press Ctrl+C to stop the server\n" << std::endl;
    
    ChatServer server(port);
    
    if (!server.initialize()) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }
    
    server.run();
    
    return 0;
}