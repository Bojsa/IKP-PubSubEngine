// PubSubEngine.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "../Common/pch.h"
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <conio.h>

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

#include "PublisherEngine.h"
#include "../Common/message.h"
#include "../Common/logging.h"
#include "../Common/error.h"
#include "../Common/client.h"

#define BUFFER_SIZE 1024
#define SE_PORT "55002"
#define SS_PORT "55003"
#define CONNECTION_RETRY_DELAY 3000

// Global variables
static SOCKET serverSocket = INVALID_SOCKET;
static SOCKET seSocket = INVALID_SOCKET;
static SOCKET ssSocket = INVALID_SOCKET;
static Client* publishers[MAX_CLIENTS];
static int publisherCount = 0;
static HANDLE publishersMutex;
static volatile bool shouldStop = false;
static HANDLE consoleHandle;
static COORD cursorPosition = { 0, 0 };

// Connection status
static volatile bool seConnected = false;
static volatile bool ssConnected = false;

// Forward declarations
static unsigned __stdcall HandleClientThread(void* param);
static unsigned __stdcall HandleRequestsThread(void* param);
static unsigned __stdcall ConnectionManagerThread(void* param);
static void ClearScreen(void);
static void UpdateDisplay(void);
static void MoveCursor(int x, int y);
static bool ConnectToService(const char* port, SOCKET* serviceSocket, const char* serviceName);
static bool RequestTopicsFromSE(char* topicList, size_t bufferSize);
static bool IsTopicInList(const char* topic, const char* topicList);
static bool ForwardToSE(const char* topic, const char* message);
static bool ForwardToSS(const char* topic, const char* message);
static bool IsUsernameUnique(const char* username);

bool PublisherEngine_Init(void) {
    InitializeLogging("publisher_engine.log");
    SetLogLevel(LOG_INFO);

    publishersMutex = CreateMutex(NULL, FALSE, NULL);
    if (publishersMutex == NULL) {
        LogMessage(LOG_ERROR, "Failed to create mutex: %s", GetErrorDescription(ERROR_MUTEX_ERROR));
        return false;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogMessage(LOG_ERROR, "WSAStartup failed: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
        return false;
    }

    struct addrinfo* result = NULL, hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &result) != 0) {
        LogMessage(LOG_ERROR, "getaddrinfo failed: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
        WSACleanup();
        return false;
    }

    serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (serverSocket == INVALID_SOCKET) {
        LogMessage(LOG_ERROR, "Socket creation failed: %s", GetErrorDescription(ERROR_SOCKET_ERROR));
        freeaddrinfo(result);
        WSACleanup();
        return false;
    }

    if (bind(serverSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Bind failed: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
        freeaddrinfo(result);
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    freeaddrinfo(result);

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Listen failed: %s", GetErrorDescription(ERROR_CONNECTION_FAILED));
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    LogMessage(LOG_INFO, "Publisher Engine initialized and listening on port %s", DEFAULT_PORT);

    consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (consoleHandle == INVALID_HANDLE_VALUE) {
        LogMessage(LOG_ERROR, "Failed to get console handle");
        return false;
    }

    UpdateDisplay();
    return true;
}

bool PublisherEngine_IsAuthorized(const char* authMessage) {
    return strcmp(authMessage, PES_AUTH_MESSAGE) == 0;
}

bool PublisherEngine_ReceiveMessage(const char* topic, const char* message) {
    if (!topic || !message) {
        LogMessage(LOG_ERROR, "Invalid parameters: %s", GetErrorDescription(ERROR_INVALID_MESSAGE));
        return false;
    }

    LogMessage(LOG_INFO, "Received message for topic '%s': %s", topic, message);

    // Get current topics from SE
    char topicList[2048];
    bool topicExists = false;

    if (seConnected && RequestTopicsFromSE(topicList, sizeof(topicList))) {
        topicExists = IsTopicInList(topic, topicList);
        LogMessage(LOG_INFO, "Topic '%s' %s in subscriber topics", topic, topicExists ? "found" : "not found");
    }

    // Forward to SE only if topic exists in subscriber topics
    if (seConnected && topicExists) {
        ForwardToSE(topic, message);
    }

    // Always forward to SS if connected
    if (ssConnected) {
        ForwardToSS(topic, message);
    }

    return true;
}

bool PublisherEngine_ForwardMessage(const char* topic, const char* message) {
    return PublisherEngine_ReceiveMessage(topic, message);
}

static bool ConnectToService(const char* port, SOCKET* serviceSocket, const char* serviceName) {
    if (*serviceSocket != INVALID_SOCKET) {
        return true; // Already connected
    }

    struct addrinfo* result = NULL, hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo("localhost", port, &hints, &result) != 0) {
        return false;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return false;
    }

    // Set non-blocking mode for timeout
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(sock);
            freeaddrinfo(result);
            return false;
        }

        // Wait for connection with timeout
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);

        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        if (select(0, NULL, &writeSet, NULL, &timeout) <= 0) {
            closesocket(sock);
            freeaddrinfo(result);
            return false;
        }
    }

    // Set back to blocking mode
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
    freeaddrinfo(result);

    // Send authentication
    char authMessage[256];
    snprintf(authMessage, sizeof(authMessage), "%s|Publisher Service", PES_AUTH_MESSAGE);
    if (send(sock, authMessage, strlen(authMessage), 0) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    *serviceSocket = sock;
    LogMessage(LOG_INFO, "Connected to %s successfully", serviceName);
    return true;
}

static bool RequestTopicsFromSE(char* topicList, size_t bufferSize) {
    if (seSocket == INVALID_SOCKET) {
        return false;
    }

    // Send topic request
    const char* request = "request|topics";
    if (send(seSocket, request, strlen(request), 0) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Failed to send topic request to SE");
        seConnected = false;
        closesocket(seSocket);
        seSocket = INVALID_SOCKET;
        return false;
    }

    // Receive response
    int bytesReceived = recv(seSocket, topicList, bufferSize - 1, 0);
    if (bytesReceived <= 0) {
        LogMessage(LOG_ERROR, "Failed to receive topic list from SE");
        seConnected = false;
        closesocket(seSocket);
        seSocket = INVALID_SOCKET;
        return false;
    }

    topicList[bytesReceived] = '\0';
    LogMessage(LOG_INFO, "Received topic list from SE: %s", topicList);
    return true;
}

static bool IsTopicInList(const char* topic, const char* topicList) {
    if (!topic || !topicList) return false;

    char* listCopy = _strdup(topicList);
    char* token = strtok(listCopy, ",");

    while (token != NULL) {
        if (strcmp(token, topic) == 0) {
            free(listCopy);
            return true;
        }
        token = strtok(NULL, ",");
    }

    free(listCopy);
    return false;
}

static bool ForwardToSE(const char* topic, const char* message) {
    if (seSocket == INVALID_SOCKET) {
        return false;
    }

    char buffer[MAX_TOPIC_LENGTH + MAX_MESSAGE_LENGTH + 2];
    snprintf(buffer, sizeof(buffer), "%s|%s", topic, message);

    if (send(seSocket, buffer, strlen(buffer), 0) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Failed to forward message to SE");
        seConnected = false;
        closesocket(seSocket);
        seSocket = INVALID_SOCKET;
        return false;
    }

    LogMessage(LOG_INFO, "Forwarded message to SE: %s", buffer);
    return true;
}

static bool ForwardToSS(const char* topic, const char* message) {
    if (ssSocket == INVALID_SOCKET) {
        return false;
    }

    char buffer[MAX_TOPIC_LENGTH + MAX_MESSAGE_LENGTH + 2];
    snprintf(buffer, sizeof(buffer), "%s|%s", topic, message);

    if (send(ssSocket, buffer, strlen(buffer), 0) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Failed to forward message to SS");
        ssConnected = false;
        closesocket(ssSocket);
        ssSocket = INVALID_SOCKET;
        return false;
    }

    LogMessage(LOG_INFO, "Forwarded message to SS: %s", buffer);
    return true;
}

static bool IsUsernameUnique(const char* username) {
    for (int i = 0; i < publisherCount; i++) {
        if (strcmp(publishers[i]->username, username) == 0) {
            return false;
        }
    }
    return true;
}

static unsigned __stdcall ConnectionManagerThread(void* param) {
    while (!shouldStop) {
        // Try to connect to SE
        if (!seConnected) {
            if (ConnectToService(SE_PORT, &seSocket, "Subscriber Engine")) {
                seConnected = true;
                UpdateDisplay();
            }
        }

        // Try to connect to SS
        if (!ssConnected) {
            if (ConnectToService(SS_PORT, &ssSocket, "Storage Service")) {
                ssConnected = true;
                UpdateDisplay();
            }
        }

        Sleep(CONNECTION_RETRY_DELAY);
    }
    return 0;
}

static unsigned __stdcall HandleRequestsThread(void* param) {
    SOCKET clientSocket = *(SOCKET*)param;
    char buffer[BUFFER_SIZE];

    while (!shouldStop) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            // Client disconnected
            WaitForSingleObject(publishersMutex, INFINITE);
            for (int i = 0; i < publisherCount; i++) {
                if (publishers[i]->clientSocket == clientSocket) {
                    Client_Cleanup(publishers[i]);
                    free(publishers[i]);
                    // Move remaining publishers up
                    for (int j = i; j < publisherCount - 1; j++) {
                        publishers[j] = publishers[j + 1];
                    }
                    publisherCount--;
                    break;
                }
            }
            ReleaseMutex(publishersMutex);
            UpdateDisplay();
            break;
        }

        buffer[bytesReceived] = '\0';

        char* delimiter = strchr(buffer, '|');
        if (delimiter) {
            *delimiter = '\0';
            char* topic = buffer;
            char* message = delimiter + 1;

            LogMessage(LOG_INFO, "Publisher sent message: %s|%s", topic, message);
            PublisherEngine_ReceiveMessage(topic, message);
        }
    }

    return 0;
}

static unsigned __stdcall HandleClientThread(void* param) {
    while (!shouldStop) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) continue;

        char buffer[BUFFER_SIZE];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            closesocket(clientSocket);
            continue;
        }

        buffer[bytesReceived] = '\0';

        // Split the buffer into auth and username parts
        char* delimiter = strchr(buffer, '|');
        if (!delimiter) {
            send(clientSocket, "Invalid authentication format", strlen("Invalid authentication format"), 0);
            LogMessage(LOG_WARNING, "Invalid authentication format");
            closesocket(clientSocket);
            continue;
        }

        *delimiter = '\0';
        char* authMessage = buffer;
        char* username = delimiter + 1;

        if (!PublisherEngine_IsAuthorized(authMessage)) {
            send(clientSocket, "Unauthorized connection attempt", strlen("Unauthorized connection attempt"), 0);
            LogMessage(LOG_WARNING, "Unauthorized connection attempt");
            closesocket(clientSocket);
            continue;
        }

        WaitForSingleObject(publishersMutex, INFINITE);

        if (publisherCount >= MAX_CLIENTS) {
            send(clientSocket, "Maximum clients reached", strlen("Maximum clients reached"), 0);
            LogMessage(LOG_ERROR, "Maximum clients reached");
            ReleaseMutex(publishersMutex);
            closesocket(clientSocket);
            continue;
        }

        if (!IsUsernameUnique(username)) {
            send(clientSocket, "Username already in use", strlen("Username already in use"), 0);
            LogMessage(LOG_WARNING, "Username already in use");
            ReleaseMutex(publishersMutex);
            closesocket(clientSocket);
            continue;
        }

        Client* newPublisher = (Client*)malloc(sizeof(Client));
        if (!newPublisher) {
            LogMessage(LOG_ERROR, "Failed to create publisher");
            ReleaseMutex(publishersMutex);
            closesocket(clientSocket);
            continue;
        }

        Client_Init(newPublisher, clientSocket, username, publisherCount + 1);
        send(clientSocket, "Welcome to the publisher engine", strlen("Welcome to the publisher engine"), 0);
        LogMessage(LOG_INFO, "New publisher connected. Username: %s, ID: %d", newPublisher->username, newPublisher->id);

        publishers[publisherCount++] = newPublisher;
        ReleaseMutex(publishersMutex);
        UpdateDisplay();

        unsigned threadId;
        HANDLE requestThread = (HANDLE)_beginthreadex(NULL, 0, HandleRequestsThread, &clientSocket, 0, &threadId);
        if (requestThread == NULL) {
            LogMessage(LOG_ERROR, "Failed to create request handler thread");
            closesocket(clientSocket);
            continue;
        }
        CloseHandle(requestThread);
    }
    return 0;
}

void PublisherEngine_Destroy(void) {
    shouldStop = true;

    // Close all client connections first
    WaitForSingleObject(publishersMutex, INFINITE);
    for (int i = 0; i < publisherCount; i++) {
        if (publishers[i] && publishers[i]->clientSocket != INVALID_SOCKET) {
            closesocket(publishers[i]->clientSocket);
            Client_Cleanup(publishers[i]);
            free(publishers[i]);
        }
    }
    publisherCount = 0;
    ReleaseMutex(publishersMutex);

    // Close service connections
    if (seSocket != INVALID_SOCKET) {
        closesocket(seSocket);
        seSocket = INVALID_SOCKET;
    }

    if (ssSocket != INVALID_SOCKET) {
        closesocket(ssSocket);
        ssSocket = INVALID_SOCKET;
    }

    // Close server socket
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    // Cleanup Windows handles and WSA
    if (publishersMutex) {
        CloseHandle(publishersMutex);
        publishersMutex = NULL;
    }

    if (consoleHandle && consoleHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(consoleHandle);
        consoleHandle = INVALID_HANDLE_VALUE;
    }

    WSACleanup();
    CloseLogging();
}

static void ClearScreen(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;

    if (GetConsoleScreenBufferInfo(consoleHandle, &csbi)) {
        cellCount = csbi.dwSize.X * csbi.dwSize.Y;
        MoveCursor(0, 0);
        FillConsoleOutputCharacter(consoleHandle, ' ', cellCount, cursorPosition, &count);
        FillConsoleOutputAttribute(consoleHandle, csbi.wAttributes, cellCount, cursorPosition, &count);
        MoveCursor(0, 0);
    }
}

static void MoveCursor(int x, int y) {
    cursorPosition.X = (SHORT)x;
    cursorPosition.Y = (SHORT)y;
    SetConsoleCursorPosition(consoleHandle, cursorPosition);
}

static void UpdateDisplay(void) {
    ClearScreen();

    // Status Bar
    printf("=== Publisher Engine Status ===\n");
    printf("Connected Publishers: %d/%d | SE: %s | SS: %s\n",
        publisherCount,
        MAX_CLIENTS,
        seConnected ? "Connected" : "Disconnected",
        ssConnected ? "Connected" : "Disconnected");
    printf("=====================================\n\n");

    // Publisher List
    WaitForSingleObject(publishersMutex, INFINITE);

    if (publisherCount == 0) {
        printf("No publishers connected.\n");
    }
    else {
        printf("Connected Publishers:\n");
        for (int i = 0; i < publisherCount; i++) {
            printf("  Publisher %d: %s\n",
                publishers[i]->id,
                publishers[i]->username);
        }
        printf("\n");
    }

    ReleaseMutex(publishersMutex);
    printf("Press 'q' to quit...\n");
}

int main(void) {
    if (!PublisherEngine_Init()) {
        return 1;
    }

    // Start connection manager thread
    unsigned connThreadId;
    HANDLE connectionThread = (HANDLE)_beginthreadex(NULL, 0, ConnectionManagerThread, NULL, 0, &connThreadId);
    if (connectionThread == NULL) {
        LogMessage(LOG_ERROR, "Failed to create connection manager thread");
        PublisherEngine_Destroy();
        return 1;
    }

    // Start client handler thread
    unsigned clientThreadId;
    HANDLE clientThread = (HANDLE)_beginthreadex(NULL, 0, HandleClientThread, NULL, 0, &clientThreadId);
    if (clientThread == NULL) {
        LogMessage(LOG_ERROR, "Failed to create client handler thread");
        PublisherEngine_Destroy();
        return 1;
    }

    while (!shouldStop) {
        if (_kbhit()) {
            int ch = _getch();
            LogMessage(LOG_INFO, "Key pressed: %d", ch);
            if (ch == 'q' || ch == 'Q') {
                LogMessage(LOG_INFO, "Quit command received");
                break;
            }
        }
        Sleep(100);
    }

    LogMessage(LOG_INFO, "Beginning shutdown sequence");
    shouldStop = true;

    // Force close the server socket to unblock accept()
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    // Wait for threads to finish with timeout
    if (WaitForSingleObject(clientThread, 5000) == WAIT_TIMEOUT) {
        LogMessage(LOG_WARNING, "Client thread did not terminate gracefully, forcing termination");
        TerminateThread(clientThread, 1);
    }

    if (WaitForSingleObject(connectionThread, 5000) == WAIT_TIMEOUT) {
        LogMessage(LOG_WARNING, "Connection thread did not terminate gracefully, forcing termination");
        TerminateThread(connectionThread, 1);
    }

    CloseHandle(clientThread);
    CloseHandle(connectionThread);

    PublisherEngine_Destroy();
    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
