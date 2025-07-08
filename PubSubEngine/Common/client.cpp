#include "pch.h"
#include "client.h"
#include "error.h"
#include "logging.h"
#include <string.h>

void Client_Init(Client* client, SOCKET socket, const char* username, int id) {
    client->clientSocket = socket;
    client->id = id;
    strncpy(client->username, username, MAX_USERNAME - 1);
    client->username[MAX_USERNAME - 1] = '\0'; // Ensure null termination

    LogMessage(LOG_INFO, "Client initialized with username: %s, ID: %d", client->username, client->id);
}

bool Client_Validate(const Client* client) {
    if (client->clientSocket == INVALID_SOCKET) {
        LogMessage(LOG_ERROR, "Invalid client socket");
        return false;
    }

    if (strlen(client->username) == 0) {
        LogMessage(LOG_ERROR, "Invalid client username");
        return false;
    }

    if (client->id < 0) {
        LogMessage(LOG_ERROR, "Invalid client ID");
        return false;
    }

    return true;
}

void Client_Cleanup(Client* client) {
    if (client->clientSocket != INVALID_SOCKET) {
        closesocket(client->clientSocket);
        client->clientSocket = INVALID_SOCKET;
    }
    memset(client->username, 0, MAX_USERNAME);
    client->id = -1;

    LogMessage(LOG_INFO, "Client cleanup completed");
}

int Client_SetUsername(Client* client, const char* username) {
    if (username == NULL || strlen(username) == 0) {
        LogMessage(LOG_ERROR, "Attempted to set invalid username");
        return ERROR_INVALID__TOPIC_MESSAGE;
    }

    strncpy(client->username, username, MAX_USERNAME - 1);
    client->username[MAX_USERNAME - 1] = '\0';

    LogMessage(LOG_INFO, "Username updated to: %s", client->username);
    return ERROR_NONE;
}

const char* Client_GetUsername(const Client* client) {
    return client->username;
}

int Client_GetId(const Client* client) {
    return client->id;
}
