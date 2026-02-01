#include "../Common/pch.h"
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

#include "StorageService.h"
#include "../Common/message.h"
#include "../Common/logging.h"
#include "../Common/error.h"

// Static variables for the service
static char g_storagePath[256];
static HANDLE g_storageMutex;
static bool g_isInitialized = false;

// Network-related globals
static SOCKET serverSocket = INVALID_SOCKET;
static SOCKET clientSocket = INVALID_SOCKET;
static volatile bool shouldStop = false;

#define DEFAULT_PORT "55003"
#define AUTH_KEY "X8k9#mP2$vL5nQ7"
#define BUFFER_SIZE 1024

void StorageService_Init(const char* storageFilePath) {
    if (g_isInitialized) {
        LogMessage(LOG_WARNING, "Storage Service already initialized: %s", GetErrorDescription(ERROR_CLIENT_INIT_FAILED));
        return;
    }

    strncpy(g_storagePath, storageFilePath, sizeof(g_storagePath) - 1);
    g_storagePath[sizeof(g_storagePath) - 1] = '\0';
    
    InitializeLogging("storage_service.log");
    SetLogLevel(LOG_INFO);
    
    g_storageMutex = CreateMutex(NULL, FALSE, NULL);
    if (g_storageMutex == NULL) {
        LogMessage(LOG_ERROR, "Mutex error: %s", GetErrorDescription(ERROR_MUTEX_ERROR));
        return;
    }
    
    LogMessage(LOG_INFO, "Storage Service initialized with path: %s", storageFilePath);
    printf("[Storage] Initialized -> path: %s\n", storageFilePath);
    fflush(stdout);
    g_isInitialized = true;
}

bool StorageService_SaveMessage(const char* topic, const char* message) {
    if (!g_isInitialized) {
        LogMessage(LOG_ERROR, "Storage error: %s", GetErrorDescription(ERROR_STORAGE_FAILURE));
        return false;
    }

    Message msg;
    Message_Init(&msg, topic, message);

    if (!Message_Validate(&msg)) {
        LogMessage(LOG_ERROR, "Message error: %s", GetErrorDescription(ERROR_INVALID_MESSAGE));
        return false;
    }

    DWORD waitResult = WaitForSingleObject(g_storageMutex, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        LogMessage(LOG_ERROR, "Thread error: %s", GetErrorDescription(ERROR_MUTEX_ERROR));
        return false;
    }

    FILE* storage = fopen(g_storagePath, "a");
    bool success = false;

    if (storage != NULL) {
        if (fprintf(storage, "%s|%s\n", msg.topic, msg.message) > 0) {
            success = true;
            LogMessage(LOG_INFO, "Message saved successfully for topic: %s", topic);
            printf("[Storage] Saved -> %s|%s\n", msg.topic, msg.message);
            fflush(stdout);
        } else {
            LogMessage(LOG_ERROR, "Storage error: %s", GetErrorDescription(ERROR_STORAGE_FAILURE));
        }
        fclose(storage);
    } else {
        LogMessage(LOG_ERROR, "File access error: %s", GetErrorDescription(ERROR_FILE_ACCESS_DENIED));
    }

    ReleaseMutex(g_storageMutex);
    return success;
}

bool StorageService_GetMessages(const char* topic, char** buffer, int* messageCount) {
    if (!g_isInitialized) {
        LogMessage(LOG_ERROR, "Storage error: %s", GetErrorDescription(ERROR_STORAGE_FAILURE));
        return false;
    }

    DWORD waitResult = WaitForSingleObject(g_storageMutex, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        LogMessage(LOG_ERROR, "Thread error: %s", GetErrorDescription(ERROR_MUTEX_ERROR));
        return false;
    }

    FILE* storage = fopen(g_storagePath, "r");
    if (storage == NULL) {
        LogMessage(LOG_ERROR, "File access error: %s", GetErrorDescription(ERROR_FILE_ACCESS_DENIED));
        ReleaseMutex(g_storageMutex);
        return false;
    }

    // First pass: count matching messages
    char line[MAX_MESSAGE_LENGTH + MAX_TOPIC_LENGTH + 2];
    *messageCount = 0;
    while (fgets(line, sizeof(line), storage)) {
        char* delimiter = strchr(line, '|');
        if (delimiter) {
            *delimiter = '\0';
            if (strcmp(line, topic) == 0) {
                (*messageCount)++;
            }
        }
    }

    // Allocate buffer for messages
    *buffer = (char*)malloc(*messageCount * MAX_MESSAGE_LENGTH);
    if (*buffer == NULL) {
        LogMessage(LOG_ERROR, "Storage error: %s", GetErrorDescription(ERROR_STORAGE_FULL));
        fclose(storage);
        ReleaseMutex(g_storageMutex);
        return false;
    }

    // Second pass: copy messages
    rewind(storage);
    char* currentPos = *buffer;
    int foundCount = 0;
    
    while (fgets(line, sizeof(line), storage) && foundCount < *messageCount) {
        char* delimiter = strchr(line, '|');
        if (delimiter) {
            *delimiter = '\0';
            if (strcmp(line, topic) == 0) {
                char* message = delimiter + 1;
                size_t messageLen = strlen(message);
                if (messageLen > 0 && message[messageLen-1] == '\n') {
                    message[messageLen-1] = '\0';
                }
                strncpy(currentPos, message, MAX_MESSAGE_LENGTH - 1);
                currentPos[MAX_MESSAGE_LENGTH - 1] = '\0';
                currentPos += MAX_MESSAGE_LENGTH;
                foundCount++;
            }
        }
    }

    fclose(storage);
    ReleaseMutex(g_storageMutex);
    return true;
}

void StorageService_Destroy(void) {
    if (!g_isInitialized) {
        return;
    }

    LogMessage(LOG_INFO, "Storage Service shutting down");
    CloseLogging();
    
    if (g_storageMutex != NULL) {
        CloseHandle(g_storageMutex);
        g_storageMutex = NULL;
    }
    
    g_isInitialized = false;
}

static unsigned __stdcall HandleClientRequests(void* param) {
    SOCKET clientSock = *(SOCKET*)param;
    char buffer[BUFFER_SIZE];

    while (!shouldStop) {
        int bytesReceived = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            LogMessage(LOG_ERROR, "Publisher Engine Service disconnected or error occurred");
            printf("[Storage] PES disconnected or error occurred\n");
            fflush(stdout);
            break;
        }

        buffer[bytesReceived] = '\0';
        char* delimiter = strchr(buffer, '|');
        if (delimiter) {
            *delimiter = '\0';
            char* topic = buffer;
            char* message = delimiter + 1;
            StorageService_SaveMessage(topic, message);
        }
    }

    return 0;
}

static bool AuthenticateClient(SOCKET clientSocket) {
    char buffer[256];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        return false;
    }
    buffer[bytesReceived] = '\0';
    return strcmp(buffer, AUTH_KEY) == 0;
}

static bool InitializeServer(void) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogMessage(LOG_ERROR, "Connection error: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
        return false;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    int port = atoi(DEFAULT_PORT);

    while (1) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            LogMessage(LOG_ERROR, "Socket error: %s", GetErrorDescription(ERROR_SOCKET_ERROR));
            WSACleanup();
            return false;
        }

        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            if (WSAGetLastError() == 10048) {
                LogMessage(LOG_WARNING, "Port %d already in use. Trying next port...", port);
                closesocket(serverSocket);
                port++;
            } else {
                LogMessage(LOG_ERROR, "Connection error: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
                closesocket(serverSocket);
                WSACleanup();
                return false;
            }
        } else {
            break;
        }
    }

    // Get the local IP address
    char hostname[100];
    gethostname(hostname, sizeof(hostname));
    struct addrinfo* result = NULL;
    struct addrinfo hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(hostname, NULL, &hints, &result) != 0) {
        LogMessage(LOG_ERROR, "Connection error: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    char localIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in*)result->ai_addr)->sin_addr, localIP, sizeof(localIP));
    freeaddrinfo(result);

    LogMessage(LOG_INFO, "Storage Service is listening on IP %s, port %d", localIP, port);
    printf("[Storage] Listening -> %s:%d\n", localIP, port);
    fflush(stdout);
    return true;
}

int main(void) {
    StorageService_Init("storage.txt");
    
    if (!InitializeServer()) {
        LogMessage(LOG_ERROR, "Failed to initialize server");
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Listen failed");
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    LogMessage(LOG_INFO, "Server started. Waiting for Publisher Engine Service connection...");
    printf("[Storage] Waiting for Publisher Engine Service connection...\n");
    fflush(stdout);

    while (!shouldStop) {
        clientSocket = accept(serverSocket, (struct sockaddr*)NULL, (int*)NULL);
        if (clientSocket == INVALID_SOCKET) {
            LogMessage(LOG_ERROR, "Accept failed");
            closesocket(serverSocket);
            WSACleanup();
            return 1;
        }

        if (AuthenticateClient(clientSocket)) {
            printf("[Storage] PES authenticated successfully\n");
            fflush(stdout);
            break; // Successfully authenticated client
        }

        LogMessage(LOG_WARNING, "Client authentication failed, waiting for new connection");
        printf("[Storage] Client authentication failed, waiting for new connection\n");
        fflush(stdout);
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }

    if (shouldStop) {
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    LogMessage(LOG_INFO, "Publisher Engine Service connected and authenticated successfully");
    printf("[Storage] PES connected and authenticated successfully\n");
    fflush(stdout);

    // Start the background thread to handle client requests
    unsigned threadId;
    HANDLE clientThread = (HANDLE)_beginthreadex(NULL, 0, HandleClientRequests, &clientSocket, 0, &threadId);
    if (clientThread == NULL) {
        LogMessage(LOG_ERROR, "Failed to create client thread");
        closesocket(clientSocket);
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    printf("Press Enter to stop the storage service...\n");
    getchar();

    // Cleanup
    shouldStop = true;
    WaitForSingleObject(clientThread, INFINITE);
    CloseHandle(clientThread);
    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();
    StorageService_Destroy();

    return 0;
}
