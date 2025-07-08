#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

// Log levels
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

// Initialize logging with a file path
// Returns 0 on success, non-zero on failure
int InitializeLogging(const char* logFilePath);

// Close the log file
void CloseLogging(void);

// Log a message with the specified level
void LogMessage(LogLevel level, const char* format, ...);

// Set minimum log level (messages below this level won't be logged)
void SetLogLevel(LogLevel level);

#endif // LOGGING_H 