#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include <ctime>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>
#include <set>

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

// Message types
enum MessageType {
    MSG_NORMAL = 1,
    MSG_PRIVATE = 2,
    MSG_JOIN = 3,
    MSG_LEAVE = 4,
    MSG_LIST_USERS = 5,
    MSG_WHISPER = 6,
    MSG_FILE = 7,
    MSG_ERROR = 8,
    MSG_TYPING = 9,      // Typing indicator
    MSG_READ_RECEIPT = 10, // Read receipt
    MSG_BLOCK_USER = 11,   // Block a user
    MSG_UNBLOCK_USER = 12, // Unblock a user
    MSG_BLOCKED_LIST = 13, // Show blocked users
    MSG_PRIVATE_HISTORY = 14 // Private message history
};

// Structure for chat message
struct ChatMessage {
    int type;
    char sender[50];
    char recipient[50];
    char content[1024];
    time_t timestamp;
    
    ChatMessage() {
        type = MSG_NORMAL;
        memset(sender, 0, sizeof(sender));
        memset(recipient, 0, sizeof(recipient));
        memset(content, 0, sizeof(content));
        timestamp = time(nullptr);
    }
};

// Structure for private message history
struct PrivateMessage {
    std::string from;
    std::string to;
    std::string content;
    time_t timestamp;
    bool isRead;
};

// Utility functions
void printBanner() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "         C++ Chat Application          " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  /help           - Show this help" << std::endl;
    std::cout << "  /users          - List all users" << std::endl;
    std::cout << "  /whisper <user> <msg> - Send private message" << std::endl;
    std::cout << "  /w <user> <msg> - Shortcut for whisper" << std::endl;
    std::cout << "  /reply <msg>    - Reply to last private message" << std::endl;
    std::cout << "  /history <user> - Show private chat history" << std::endl;
    std::cout << "  /block <user>   - Block a user" << std::endl;
    std::cout << "  /unblock <user> - Unblock a user" << std::endl;
    std::cout << "  /blocked        - Show blocked users" << std::endl;
    std::cout << "  /quit           - Exit chat" << std::endl;
    std::cout << "========================================" << std::endl;
}

std::string getCurrentTime() {
    time_t now = time(nullptr);
    char* timeStr = ctime(&now);
    timeStr[strlen(timeStr) - 1] = '\0';
    return std::string(timeStr);
}

std::string formatTimestamp(time_t timestamp) {
    struct tm* timeinfo = localtime(&timestamp);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    return std::string(buffer);
}

#endif