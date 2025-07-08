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

#include "SubscribeEngine.h"
#include "../Common/message.h"
#include "../Common/logging.h"
#include "../Common/error.h"
#include "../Common/client.h"

#define BUFFER_SIZE 1024

// Global variables
static SOCKET serverSocket = INVALID_SOCKET;
static SOCKET pesSocket = INVALID_SOCKET;
static Subscriber* subscribers[MAX_CLIENTS];
static int subscriberCount = 0;
static HANDLE subscribersMutex;
static volatile bool shouldStop = false;
static HANDLE consoleHandle;
static COORD cursorPosition = { 0, 0 };

// Forward declarations
static unsigned __stdcall HandleClientThread(void* param);
static unsigned __stdcall HandleRequestsThread(void* param);
static void ClearScreen(void);
static void UpdateDisplay(void);
static void MoveCursor(int x, int y);

Subscriber* SubscriberEngine_CreateSubscriber(SOCKET socket, const char* username, int id) {
    Subscriber* sub = (Subscriber*)malloc(sizeof(Subscriber));
    if (!sub) return NULL;

    Client_Init(&sub->client, socket, username, id);
    sub->topics = (char**)malloc(MAX_TOPICS_PER_CLIENT * sizeof(char*));
    if (!sub->topics) {
        free(sub);
        return NULL;
    }

    sub->topicCount = 0;
    sub->topicCapacity = MAX_TOPICS_PER_CLIENT;
    return sub;
}

void SubscriberEngine_FreeSubscriber(Subscriber* subscriber) {
    if (!subscriber) return;

    for (size_t i = 0; i < subscriber->topicCount; i++) {
        free(subscriber->topics[i]);
    }
    free(subscriber->topics);
    Client_Cleanup(&subscriber->client);
    free(subscriber);
}

bool SubscriberEngine_Init(void) {
    InitializeLogging("subscriber_engine.log");
    SetLogLevel(LOG_INFO);

    subscribersMutex = CreateMutex(NULL, FALSE, NULL);
    if (subscribersMutex == NULL) {
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

    LogMessage(LOG_INFO, "Subscriber Engine initialized and listening on port %s", DEFAULT_PORT);

    consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (consoleHandle == INVALID_HANDLE_VALUE) {
        LogMessage(LOG_ERROR, "Failed to get console handle");
        return false;
    }
    UpdateDisplay();  // Initial display

    return true;
}

bool SubscriberEngine_IsAuthorized(const char* authMessage) {
    return (strcmp(authMessage, PES_AUTH_MESSAGE) == 0 ||
        strcmp(authMessage, SUB_AUTH_MESSAGE) == 0);
}

bool SubscriberEngine_Subscribe(Client* client, const char* topic) {
    if (!client || !topic) {
        LogMessage(LOG_ERROR, "Invalid parameters: %s", GetErrorDescription(ERROR_INVALID_SUBSCRIPTION));
        return false;
    }

    WaitForSingleObject(subscribersMutex, INFINITE);

    for (int i = 0; i < subscriberCount; i++) {
        if (subscribers[i]->client.id == client->id) {
            if (subscribers[i]->topicCount >= MAX_TOPICS_PER_CLIENT) {
                ReleaseMutex(subscribersMutex);
                send(client->clientSocket, "Subscription limit reached", strlen("Subscription limit reached"), 0);
                LogMessage(LOG_ERROR, "Subscription limit reached: %s", GetErrorDescription(ERROR_SUBSCRIPTION_LIMIT_REACHED));
                return false;
            }

            // Check if already subscribed
            for (size_t j = 0; j < subscribers[i]->topicCount; j++) {
                if (strcmp(subscribers[i]->topics[j], topic) == 0) {
                    ReleaseMutex(subscribersMutex);
                    send(client->clientSocket, "Already subscribed", strlen("Already subscribed"), 0);
                    LogMessage(LOG_WARNING, "Already subscribed: %s", GetErrorDescription(ERROR_ALREADY_SUBSCRIBED));
                    return false;
                }
            }

            // Add new topic
            char* topicCopy = _strdup(topic);
            if (!topicCopy) {
                ReleaseMutex(subscribersMutex);
                return false;
            }

            subscribers[i]->topics[subscribers[i]->topicCount++] = topicCopy;
            ReleaseMutex(subscribersMutex);
            send(client->clientSocket, "Subscribed to topic", strlen("Subscribed to topic"), 0);
            LogMessage(LOG_INFO, "Client %d subscribed to topic: %s", client->id, topic);
            UpdateDisplay();
            return true;
        }
    }

    ReleaseMutex(subscribersMutex);
    send(client->clientSocket, "Failed to subscribe to topic", strlen("Failed to subscribe to topic"), 0);
    return false;
}

bool SubscriberEngine_NotifySubscribers(const char* topic, const char* message) {
    if (!topic || !message) {
        LogMessage(LOG_ERROR, "Invalid parameters: %s", GetErrorDescription(ERROR_INVALID_MESSAGE));
        return false;
    }

    WaitForSingleObject(subscribersMutex, INFINITE);

    for (int i = 0; i < subscriberCount; i++) {
        for (size_t j = 0; j < subscribers[i]->topicCount; j++) {
            if (strcmp(subscribers[i]->topics[j], topic) == 0) {
                Message msg;
                Message_Init(&msg, topic, message);

                char buffer[MAX_TOPIC_LENGTH + MAX_MESSAGE_LENGTH + 2];
                snprintf(buffer, sizeof(buffer), "%s|%s", topic, message);

                send(subscribers[i]->client.clientSocket, buffer, strlen(buffer), 0);
            }
        }
    }

    ReleaseMutex(subscribersMutex);
    return true;
}

static bool IsUsernameUnique(const char* username) {
    for (int i = 0; i < subscriberCount; i++) {
        if (strcmp(subscribers[i]->client.username, username) == 0) {
            return false;
        }
    }
    return true;
}

static unsigned __stdcall HandleRequestsThread(void* param) {
    SOCKET clientSocket = *(SOCKET*)param;
    char buffer[BUFFER_SIZE];

    while (!shouldStop) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            // Client disconnected
            WaitForSingleObject(subscribersMutex, INFINITE);
            if (clientSocket == pesSocket) {
                pesSocket = INVALID_SOCKET;
            }
            else {
                for (int i = 0; i < subscriberCount; i++) {
                    if (subscribers[i]->client.clientSocket == clientSocket) {
                        SubscriberEngine_FreeSubscriber(subscribers[i]);
                        // Move remaining subscribers up
                        for (int j = i; j < subscriberCount - 1; j++) {
                            subscribers[j] = subscribers[j + 1];
                        }
                        subscriberCount--;
                        break;
                    }
                }
            }
            ReleaseMutex(subscribersMutex);
            UpdateDisplay();
            break;
        }

        buffer[bytesReceived] = '\0';

        char* delimiter = strchr(buffer, '|');
        if (delimiter) {
            *delimiter = '\0';
            char* topic = buffer;
            char* message = delimiter + 1;

            if (clientSocket == pesSocket) {
                // Handle PES message
                LogMessage(LOG_INFO, "PES sent message: %s|%s", topic, message);
                SubscriberEngine_NotifySubscribers(topic, message);
            }
            else {
                // Handle subscriber request
                LogMessage(LOG_INFO, "Subscriber sent message: %s|%s", topic, message);
                WaitForSingleObject(subscribersMutex, INFINITE);
                for (int i = 0; i < subscriberCount; i++) {
                    if (subscribers[i]->client.clientSocket == clientSocket) {
                        SubscriberEngine_Subscribe(&subscribers[i]->client, topic);
                        break;
                    }
                }
                ReleaseMutex(subscribersMutex);
            }
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

        if (!SubscriberEngine_IsAuthorized(authMessage)) {
            send(clientSocket, "Unauthorized connection attempt", strlen("Unauthorized connection attempt"), 0);
            LogMessage(LOG_WARNING, "Unauthorized connection attempt");
            closesocket(clientSocket);
            continue;
        }

        if (strcmp(authMessage, PES_AUTH_MESSAGE) == 0) {
            if (pesSocket != INVALID_SOCKET) {
                send(clientSocket, "PES already connected", strlen("PES already connected"), 0);
                LogMessage(LOG_WARNING, "PES already connected");
                closesocket(clientSocket);
                continue;
            }
            pesSocket = clientSocket;
            LogMessage(LOG_INFO, "PES connected successfully");
            UpdateDisplay();
        }
        else { // Subscriber Authentication
            if (subscriberCount >= MAX_CLIENTS) {
                WaitForSingleObject(subscribersMutex, INFINITE);
                send(clientSocket, "Maximum clients reached", strlen("Maximum clients reached"), 0);
                LogMessage(LOG_ERROR, "Maximum clients reached");
                ReleaseMutex(subscribersMutex);
                closesocket(clientSocket);
                continue;
            }

            if (!IsUsernameUnique(username)) {
                send(clientSocket, "Username already in use", strlen("Username already in use"), 0);
                LogMessage(LOG_WARNING, "Username already in use");
                ReleaseMutex(subscribersMutex);
                closesocket(clientSocket);
                continue;
            }

            Subscriber* newSub = SubscriberEngine_CreateSubscriber(clientSocket, username, subscriberCount + 1);
            if (!newSub) {
                LogMessage(LOG_ERROR, "Failed to create subscriber");
                ReleaseMutex(subscribersMutex);
                closesocket(clientSocket);
                continue;
            }

            send(clientSocket, "Welcome to the subscriber engine", strlen("Welcome to the subscriber engine"), 0);
            LogMessage(LOG_INFO, "New subscriber connected. Username: %s, ID: %d", newSub->client.username, newSub->client.id);

            subscribers[subscriberCount++] = newSub;
            ReleaseMutex(subscribersMutex);
            UpdateDisplay();
        }

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

void SubscriberEngine_Destroy(void) {
    shouldStop = true;

    // Close all client connections first
    WaitForSingleObject(subscribersMutex, INFINITE);
    for (int i = 0; i < subscriberCount; i++) {
        if (subscribers[i] && subscribers[i]->client.clientSocket != INVALID_SOCKET) {
            closesocket(subscribers[i]->client.clientSocket);
            SubscriberEngine_FreeSubscriber(subscribers[i]);
        }
    }
    subscriberCount = 0;
    ReleaseMutex(subscribersMutex);

    // Close PES connection
    if (pesSocket != INVALID_SOCKET) {
        closesocket(pesSocket);
        pesSocket = INVALID_SOCKET;
    }

    // Close server socket
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    // Cleanup Windows handles and WSA
    if (subscribersMutex) {
        CloseHandle(subscribersMutex);
        subscribersMutex = NULL;
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
    printf("=== Subscriber Engine Status ===\n");
    printf("Connected Clients: %d/%d | PES Service: %s\n",
        subscriberCount,
        MAX_CLIENTS,
        pesSocket != INVALID_SOCKET ? "Connected" : "Disconnected");
    printf("=====================================\n\n");

    // Client List
    WaitForSingleObject(subscribersMutex, INFINITE);

    if (subscriberCount == 0) {
        printf("No clients connected.\n");
    }
    else {
        for (int i = 0; i < subscriberCount; i++) {
            printf("Client %d: %s\n",
                subscribers[i]->client.id,
                subscribers[i]->client.username);

            if (subscribers[i]->topicCount == 0) {
                printf("  No topics subscribed\n");
            }
            else {
                printf("  Subscribed topics:\n");
                for (size_t j = 0; j < subscribers[i]->topicCount; j++) {
                    printf("    - %s\n", subscribers[i]->topics[j]);
                }
            }
            printf("\n");
        }
    }

    ReleaseMutex(subscribersMutex);
    printf("Press 'q' to quit...\n");
}

int main(void) {
    if (!SubscriberEngine_Init()) {
        return 1;
    }

    unsigned threadId;
    HANDLE clientThread = (HANDLE)_beginthreadex(NULL, 0, HandleClientThread, NULL, 0, &threadId);
    if (clientThread == NULL) {
        LogMessage(LOG_ERROR, "Failed to create client handler thread");
        SubscriberEngine_Destroy();
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

    // Wait for client thread to finish with a timeout
    if (WaitForSingleObject(clientThread, 5000) == WAIT_TIMEOUT) {
        LogMessage(LOG_WARNING, "Client thread did not terminate gracefully, forcing termination");
        TerminateThread(clientThread, 1);
    }

    SubscriberEngine_Destroy();
    return 0;
}