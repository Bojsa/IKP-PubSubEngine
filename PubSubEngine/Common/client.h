#ifndef CLIENT_H
#define CLIENT_H

#include <WinSock2.h>

// Constants
#define MAX_USERNAME 32

// Structure to represent a client
typedef struct {
    SOCKET clientSocket;
    char username[MAX_USERNAME];
    int id;
} Client;

// Initialize a client structure
void Client_Init(Client* client, SOCKET socket, const char* username, int id);

// Validate client data
bool Client_Validate(const Client* client);

// Clean up client resources
void Client_Cleanup(Client* client);

// Set client username
int Client_SetUsername(Client* client, const char* username);

// Get client username
const char* Client_GetUsername(const Client* client);

// Get client ID
int Client_GetId(const Client* client);

#endif // CLIENT_H
