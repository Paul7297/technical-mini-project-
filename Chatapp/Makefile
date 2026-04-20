CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread

# Windows
ifeq ($(OS), Windows_NT)
    CXXFLAGS += -DWIN32
    LIBS = -lws2_32
    RM = del /Q
    TARGET_EXT = .exe
else
    LIBS = -lpthread
    RM = rm -f
    TARGET_EXT =
endif

all: server client

server: server.cpp common.h
	$(CXX) $(CXXFLAGS) -o server$(TARGET_EXT) server.cpp $(LIBS)

client: client.cpp common.h
	$(CXX) $(CXXFLAGS) -o client$(TARGET_EXT) client.cpp $(LIBS)

clean:
	$(RM) server$(TARGET_EXT) client$(TARGET_EXT)

run-server:
	./server$(TARGET_EXT)

run-client:
	./client$(TARGET_EXT)

.PHONY: all clean run-server run-client