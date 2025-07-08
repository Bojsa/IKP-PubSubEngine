#include "pch.h"
#include "error.h"
#include <string.h>

const char* GetErrorDescription(int errorCode) {
    switch (errorCode) {
        // Success
    case ERROR_NONE:
        return "No error";

        // Connection errors
    case ERROR_CONNECTION_FAILED:
        return "Failed to establish connection";
    case ERROR_CONNECTION_LOST:
        return "Connection lost";
    case ERROR_SOCKET_ERROR:
        return "Socket error occurred";
    case ERROR_TIMEOUT:
        return "Operation timed out";

        // Message errors
    case ERROR_INVALID_MESSAGE:
        return "Invalid message format";
    case ERROR_MESSAGE_TOO_LARGE:
        return "Message exceeds size limit";
    case ERROR_INVALID_TOPIC:
        return "Invalid topic format";
    case ERROR_TOPIC_TOO_LONG:
        return "Topic name too long";
    case ERROR_EMPTY_MESSAGE:
        return "Empty message not allowed";

        // Publisher errors
    case ERROR_PUBLISH_FAILED:
        return "Failed to publish message";
    case ERROR_PUBLISHER_NOT_CONNECTED:
        return "Publisher not connected";
    case ERROR_TOPIC_RESTRICTED:
        return "Topic is restricted";
    case ERROR_PUBLISHER_UNAUTHORIZED:
        return "Publisher not authorized";

        // Subscriber errors
    case ERROR_SUBSCRIBE_FAILED:
        return "Failed to subscribe to topic";
    case ERROR_SUBSCRIBER_NOT_CONNECTED:
        return "Subscriber not connected";
    case ERROR_SUBSCRIPTION_LIMIT_REACHED:
        return "Subscription limit reached";
    case ERROR_ALREADY_SUBSCRIBED:
        return "Already subscribed to topic";
    case ERROR_INVALID_SUBSCRIPTION:
        return "Invalid subscription request";

        // Storage errors
    case ERROR_STORAGE_FAILURE:
        return "Storage operation failed";
    case ERROR_STORAGE_FULL:
        return "Storage capacity full";
    case ERROR_FILE_ACCESS_DENIED:
        return "File access denied";
    case ERROR_DATABASE_ERROR:
        return "Database error occurred";
    case ERROR_STORAGE_CORRUPTED:
        return "Storage data corrupted";

        // Thread errors
    case ERROR_THREAD_CREATE_FAILED:
        return "Failed to create thread";
    case ERROR_THREAD_JOIN_FAILED:
        return "Failed to join thread";
    case ERROR_MUTEX_ERROR:
        return "Mutex operation failed";
    case ERROR_DEADLOCK_DETECTED:
        return "Deadlock detected";

        // Client errors
    case ERROR_CLIENT_INIT_FAILED:
        return "Failed to initialize client";
    case ERROR_CLIENT_INVALID_STATE:
        return "Invalid client state";
    case ERROR_CLIENT_DISCONNECTED:
        return "Client disconnected";
    case ERROR_USERNAME_TAKEN:
        return "Username already taken";
    case ERROR_INVALID_USERNAME:
        return "Invalid username";

    default:
        return "Unknown error";
    }
}
