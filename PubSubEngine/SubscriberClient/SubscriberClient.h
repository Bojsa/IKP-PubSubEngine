#ifndef SUBSCRIBER_CLIENT_H
#define SUBSCRIBER_CLIENT_H

#include <stdbool.h>
#include <WinSock2.h>

#define MAX_USERNAME_INPUT 31
#define DEFAULT_PORT "55002"
#define SUB_AUTH_MESSAGE "SUB_AUTH"

// Client states
typedef enum {
    STATE_DISCONNECTED,
    STATE_CONNECTED
} ConnectionState;

// Initialize the client
bool Client_Initialize(void);

// Connect to the Subscriber Engine Service
bool Client_ConnectToServer(void);

// Disconnect from the server
void Client_Disconnect(void);

// Set the username
bool Client_SetUsername(const char* username);

// Subscribe to a topic
bool Client_SubscribeToTopic(const char* topic);

// Get the current connection state
ConnectionState Client_GetConnectionState(void);

// Get the current username
const char* Client_GetUsername(void);

// Clean up resources
void Client_Cleanup(void);

// Display the main menu
void DisplayMenu(void);

// Clear the console screen
void ClearScreen(void);

#endif // SUBSCRIBER_CLIENT_H
