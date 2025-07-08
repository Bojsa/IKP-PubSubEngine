#include "pch.h"
#include "logging.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE* logFile = NULL;
static LogLevel currentLogLevel = LOG_INFO;

// Convert LogLevel to string
static const char* LogLevelToString(LogLevel level) {
    switch (level) {
    case LOG_DEBUG:   return "DEBUG";
    case LOG_INFO:    return "INFO";
    case LOG_WARNING: return "WARNING";
    case LOG_ERROR:   return "ERROR";
    default:          return "UNKNOWN";
    }
}

int InitializeLogging(const char* logFilePath) {
    if (logFile != NULL) {
        return 1; // Already initialized
    }

    logFile = fopen(logFilePath, "a");
    if (logFile == NULL) {
        return 2; // Failed to open file
    }

    return 0;
}

void CloseLogging(void) {
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
}

void LogMessage(LogLevel level, const char* format, ...) {
    if (level < currentLogLevel || logFile == NULL) {
        return;
    }

    // Get current time
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    char timestamp[26];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    // Print timestamp and log level
    fprintf(logFile, "[%s] [%s] ", timestamp, LogLevelToString(level));

    // Print the actual message
    va_list args;
    va_start(args, format);
    vfprintf(logFile, format, args);
    va_end(args);

    // Add newline
    fprintf(logFile, "\n");

    // Flush the file buffer
    fflush(logFile);
}

void SetLogLevel(LogLevel level) {
    currentLogLevel = level;
}