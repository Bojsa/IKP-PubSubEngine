#include "../Common/pch.h"
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include "SubscriberClient.h"
#include "../Common/logging.h"
#include "../Common/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

// Global variables
static SOCKET serverSocket = INVALID_SOCKET;
static char username[MAX_USERNAME_INPUT + 1] = "";
static ConnectionState connectionState = STATE_DISCONNECTED;
static HANDLE receiverThread = NULL;

// Forward declarations
static unsigned __stdcall MessageReceiverThread(void* param);
static void DisplayMessage(const char* message, int delay);
static void ClearScreen(void);
static const char* ReceiveServerResponse(void);
bool Client_Initialize(void);
bool Client_SetUsername(const char* newUsername);
bool Client_ConnectToServer(void);
void Client_Disconnect(void);
bool Client_SubscribeToTopic(const char* topic);
ConnectionState Client_GetConnectionState(void);
const char* Client_GetUsername(void);
void Client_Cleanup(void);
void DisplayMenu(void);

bool Client_Initialize(void) {
    InitializeLogging("subscriber_client.log");
    SetLogLevel(LOG_INFO);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogMessage(LOG_ERROR, "WSAStartup failed");
        return false;
    }

    return true;
}

bool Client_SetUsername(const char* newUsername) {
    if (!newUsername || strlen(newUsername) == 0 || strlen(newUsername) > MAX_USERNAME_INPUT) {
        return false;
    }
    strncpy(username, newUsername, MAX_USERNAME_INPUT);
    username[MAX_USERNAME_INPUT] = '\0';
    return true;
}

static const char* ReceiveServerResponse(void) {
    static char buffer[1024];
    int bytesReceived = recv(serverSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        LogMessage(LOG_ERROR, "Server closed connection during authentication");
        Client_Disconnect();
        return "SERVER_CLOSED";
    }
    buffer[bytesReceived] = '\0';
    return buffer;
}

bool Client_ConnectToServer(void) {
    if (connectionState == STATE_CONNECTED) {
        return false;
    }

    struct addrinfo* result = NULL, hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo("localhost", DEFAULT_PORT, &hints, &result) != 0) {
        LogMessage(LOG_ERROR, "getaddrinfo failed");
        return false;
    }

    serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (serverSocket == INVALID_SOCKET) {
        LogMessage(LOG_ERROR, "Socket creation failed");
        freeaddrinfo(result);
        return false;
    }

    if (connect(serverSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Connection failed");
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }

    freeaddrinfo(result);

    // Send authentication message with username
    char authMessage[256];
    snprintf(authMessage, sizeof(authMessage), "%s|%s", SUB_AUTH_MESSAGE, username);
    if (send(serverSocket, authMessage, strlen(authMessage), 0) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Failed to send authentication");
        Client_Disconnect();
        return false;
    }

    // Wait for server response before starting receiver thread
    const char* response = ReceiveServerResponse();
    if (strcmp(response, "SERVER_CLOSED") == 0) {
        return false;
    }

    // Display server response
    DisplayMessage(response, 1000);

    // Check if the response indicates denial
    if (strstr(response, "Unauthorized") ||
        strstr(response, "Maximum clients") ||
        strstr(response, "Username already")) {
        LogMessage(LOG_WARNING, "Server denied connection: %s", response);
        Client_Disconnect();
        return false;
    }

    // Start receiver thread
    unsigned threadId;
    receiverThread = (HANDLE)_beginthreadex(NULL, 0, MessageReceiverThread, NULL, 0, &threadId);
    if (receiverThread == NULL) {
        LogMessage(LOG_ERROR, "Failed to create receiver thread");
        Client_Disconnect();
        return false;
    }

    connectionState = STATE_CONNECTED;
    LogMessage(LOG_INFO, "Connected to server successfully");
    return true;
}

void Client_Disconnect(void) {
    if (connectionState == STATE_DISCONNECTED) {
        return;
    }

    connectionState = STATE_DISCONNECTED;

    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    if (receiverThread != NULL) {
        WaitForSingleObject(receiverThread, INFINITE);
        CloseHandle(receiverThread);
        receiverThread = NULL;
    }

    fflush(stdout);
}

bool Client_SubscribeToTopic(const char* topic) {
    if (!topic || strlen(topic) == 0 || connectionState != STATE_CONNECTED) {
        return false;
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s|SUBSCRIBE", topic);

    if (send(serverSocket, buffer, strlen(buffer), 0) == SOCKET_ERROR) {
        LogMessage(LOG_ERROR, "Failed to send subscription request");
        return false;
    }

    return true;
}

ConnectionState Client_GetConnectionState(void) {
    return connectionState;
}

const char* Client_GetUsername(void) {
    return username;
}

void Client_Cleanup(void) {
    Client_Disconnect();
    WSACleanup();
    CloseLogging();
}

static unsigned __stdcall MessageReceiverThread(void* param) {
    char buffer[1024];

    while (connectionState == STATE_CONNECTED) {
        int bytesReceived = recv(serverSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            LogMessage(LOG_WARNING, "Lost connection to server");
            DisplayMessage("Lost connection to server", 1000);
            Client_Disconnect();
            printf("\nPress Enter to continue...\n");
            fflush(stdout);
            return 0;
        }

        buffer[bytesReceived] = '\0';
        Sleep(1000);
        printf("\nReceived message: %s\n", buffer);
        fflush(stdout);
    }

    return 0;
}

static void DisplayMessage(const char* message, int delay) {
    ClearScreen();
    printf("Received message: %s\n", message);
    Sleep(delay);
}

void ClearScreen(void) {
    // TODO: Potentially replace with solution from SubscriberEngine
    system("cls");
}

void DisplayMenu(void) {
    ClearScreen();
    printf("=== Subscriber Client ===\n");
    printf("Username: %s\n", strlen(username) > 0 ? username : "Not set");
    printf("Status: %s\n\n", connectionState == STATE_CONNECTED ? "Connected" : "Disconnected");

    if (connectionState == STATE_DISCONNECTED) {
        printf("1. Set Username\n");
        printf("2. Connect to Server\n");
        printf("3. Exit\n");
    }
    else {
        printf("1. Subscribe to Topic\n");
        printf("2. Exit\n");
    }
    printf("\nEnter choice: ");
}

int main(void) {
    if (!Client_Initialize()) {
        printf("Failed to initialize client.\n");
        return 1;
    }

    char input[256];
    bool running = true;

    while (running) {
        DisplayMenu();

        if (fgets(input, sizeof(input), stdin) == NULL) {
            LogMessage(LOG_ERROR, "Failed to read input");
            continue;
        }

        LogMessage(LOG_INFO, "Received input: %c", input[0]);
        input[strcspn(input, "\n")] = 0;

        if (connectionState == STATE_DISCONNECTED) {
            LogMessage(LOG_INFO, "Processing disconnected state input");
            switch (input[0]) {
            case '1':
                printf("Enter username: ");
                if (fgets(input, sizeof(input), stdin) != NULL) {
                    input[strcspn(input, "\n")] = 0;
                    if (!Client_SetUsername(input)) {
                        printf("Invalid username! Must be between 1 and %d characters.\n", MAX_USERNAME_INPUT);
                        system("pause");
                    }
                }
                break;

            case '2':
                if (strlen(username) == 0) {
                    printf("Please set username first!\n");
                    system("pause");
                }
                else {
                    Client_ConnectToServer();
                }
                break;

            case '3':
                running = false;
                break;

            default:
                printf("Invalid choice!\n");
                system("pause");
                break;
            }
        }
        else {
            LogMessage(LOG_INFO, "Processing connected state input");
            switch (input[0]) {
            case '1':
                printf("Enter topic to subscribe to: ");
                if (fgets(input, sizeof(input), stdin) != NULL) {
                    input[strcspn(input, "\n")] = 0;
                    if (!Client_SubscribeToTopic(input)) {
                        printf("Failed to subscribe to topic!\n");
                        system("pause");
                    }
                }
                break;

            case '2':
                Client_Disconnect();
                continue;

            default:
                printf("Invalid choice!\n");
                system("pause");
                break;
            }
        }
    }

    Client_Cleanup();
    return 0;
}
