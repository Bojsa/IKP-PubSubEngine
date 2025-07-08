#include "pch.h"
#include "message.h"
#include <stdio.h>
#include <string.h>

// Initialize a message structure with a topic and content
void Message_Init(Message* msg, const char* topic, const char* message) {
    strncpy(msg->topic, topic, MAX_TOPIC_LENGTH - 1);
    msg->topic[MAX_TOPIC_LENGTH - 1] = '\0'; // Null-terminate the string
    strncpy(msg->message, message, MAX_MESSAGE_LENGTH - 1);
    msg->message[MAX_MESSAGE_LENGTH - 1] = '\0'; // Null-terminate the string
}

// Validate a message (e.g., topic and message lengths)
bool Message_Validate(const Message* msg) {
    return strlen(msg->topic) > 0 && strlen(msg->message) > 0;
}

// Print a message to the console (for debugging)
void Message_Print(const Message* msg) {
    printf("Topic: %s\n", msg->topic);
    printf("Message: %s\n", msg->message);
}
