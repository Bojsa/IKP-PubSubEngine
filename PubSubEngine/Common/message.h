#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>

// Constants for message and topic lengths
#define MAX_TOPIC_LENGTH 128
#define MAX_MESSAGE_LENGTH 256

// Structure to represent a message
typedef struct {
    char topic[MAX_TOPIC_LENGTH];     // The topic associated with the message
    char message[MAX_MESSAGE_LENGTH]; // The content of the message
} Message;

// Function to initialize a message
void Message_Init(Message* msg, const char* topic, const char* message);

// Function to validate a message (e.g., check if topic and message are within limits)
bool Message_Validate(const Message* msg);

// Function to print a message (for debugging or logging purposes)
void Message_Print(const Message* msg);

#endif // MESSAGE_H
