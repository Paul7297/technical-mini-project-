# 💬 C++ Chat Application

A real-time, multi-user chat application built in C++ using Winsock2 and Windows threading.

## Features

- Public chat - Send messages to all users
- Private messaging - Send whispers to specific users (/whisper or /w)
- Quick reply - Reply to last private message (/reply)
- User list - See who's online (/users)
- Block users - Block unwanted users (/block, /unblock, /blocked)
- Message history - View private chat history (/history)
- Offline messages - Messages delivered when user reconnects
- Join/Leave notifications - See when users connect/disconnect

## Requirements

- Windows 10/11
- MinGW GCC or Visual Studio
- Winsock2 library (Windows built-in)

## Installation

### Compile Server:
```bash
g++ server.cpp -o server.exe -lws2_32 -std=c++11
```

### Compile Client:
```bash
g++ client.cpp -o client.exe -lws2_32 -std=c++11
```

## How to Run

### 1. Start the Server:
```bash
.\server.exe
```

### 2. Start Clients (in separate terminals):
```bash
.\client.exe
```
Enter username when prompted.

## Commands

| Command | Description |
|---------|-------------|
| `/help` | Show all commands |
| `/users` | List online users |
| `/whisper <user> <msg>` | Send private message |
| `/w <user> <msg>` | Shortcut for whisper |
| `/reply <msg>` | Reply to last private message |
| `/history <user>` | View chat history |
| `/block <user>` | Block a user |
| `/unblock <user>` | Unblock a user |
| `/blocked` | Show blocked users |
| `/quit` | Exit chat |

## Project Structure

```
chat-application/
├── common.h      # Shared definitions
├── server.cpp    # Server implementation
├── client.cpp    # Client implementation
└── README.md     # This file
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Command not found | Use `.\server.exe` in PowerShell |
| Failed to bind socket | Port busy, try: `.\server.exe 8889` |
| Failed to connect | Start server first |
| Username taken | Choose different username |

## Author

Your Name - [Your UID]
Chandigarh University

---
**Enjoy chatting! 💬**
