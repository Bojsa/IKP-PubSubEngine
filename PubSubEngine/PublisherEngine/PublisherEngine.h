#ifndef PUBLISHER_ENGINE_H
#define PUBLISHER_ENGINE_H

#include <stdbool.h>
#include "../Common/client.h"

#define MAX_TOPICS_PER_CLIENT 50
#define MAX_TOPIC_LENGTH 128
#define MAX_CLIENTS 100
#define DEFAULT_PORT "55001"
#define PES_AUTH_MESSAGE "PES_AUTH"
#define SUB_AUTH_MESSAGE "SUB_AUTH"

// Function to initialize the Publisher Engine
bool PublisherEngine_Init(void);

// Function to receive a new message
bool PublisherEngine_ReceiveMessage(const char* topic, const char* message);

// Function to forward the new message to other services
bool PublisherEngine_ForwardMessage(const char* topic, const char* message);

// Function to clean up resources used by the Publisher Engine
void PublisherEngine_Destroy(void);

// Function to check if client is authorized
bool PublisherEngine_IsAuthorized(const char* authMessage);

#endif // SUBSCRIBER_ENGINE_H
