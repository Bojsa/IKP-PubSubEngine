#ifndef SUBSCRIBER_ENGINE_H
#define SUBSCRIBER_ENGINE_H

#include <stdbool.h>
#include "../Common/client.h"

#define MAX_TOPICS_PER_CLIENT 50
#define MAX_TOPIC_LENGTH 128
#define MAX_CLIENTS 100
#define DEFAULT_PORT "55002"
#define PES_AUTH_MESSAGE "PES_AUTH"
#define SUB_AUTH_MESSAGE "SUB_AUTH"

// Structure to hold subscriber information
typedef struct {
    Client client;
    char** topics;  // Array of topic strings
    size_t topicCount;  // Number of topics currently subscribed
    size_t topicCapacity;  // Total capacity of topics array
} Subscriber;

// Function to initialize the Subscriber Engine
bool SubscriberEngine_Init(void);

// Function to subscribe a client to a topic
bool SubscriberEngine_Subscribe(Client* client, const char* topic);

// Function to notify subscribers about a new message
bool SubscriberEngine_NotifySubscribers(const char* topic, const char* message);

// Function to clean up resources used by the Subscriber Engine
void SubscriberEngine_Destroy(void);

// Function to check if client is authorized
bool SubscriberEngine_IsAuthorized(const char* authMessage);

// Function to create a new subscriber
Subscriber* SubscriberEngine_CreateSubscriber(SOCKET socket, const char* username, int id);

// Function to free subscriber resources
void SubscriberEngine_FreeSubscriber(Subscriber* subscriber);

#endif // SUBSCRIBER_ENGINE_H
