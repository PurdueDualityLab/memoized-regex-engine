#include "log.h"

#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

char logLevels[LOG_MAX+1][16] = {
    "silent",
    "error",
    "warn",
    "info",
    "verbose",
    "debug"
};


int getenvVerbosity() {
    char *verb = getenv("MEMOIZATION_LOGLVL");
    if (verb == NULL) {
        return LOG_SILENT;
    }

    if (strcmp(verb, "silent") == 0)
        return LOG_SILENT;
    else if (strcmp(verb, "error") == 0)
        return LOG_ERROR;
    else if (strcmp(verb, "warn") == 0)
        return LOG_WARN;
    else if (strcmp(verb, "info") == 0)
        return LOG_INFO;
    else if (strcmp(verb, "verbose") == 0)
        return LOG_VERBOSE;
    else if (strcmp(verb, "debug") == 0)
        return LOG_DEBUG;
    
    assert(!"Unknown verbosity");
}

int initialized = 0;
int maxVerbosity = LOG_SILENT;
void log_init() {
    if (initialized) {
        return;
    }

    maxVerbosity = getenvVerbosity();
    initialized = 1;
}

void logMsg_format(const char* tag, const char* message, va_list args);
int shouldLog(int logLvl)
{
    if (!initialized){
        log_init();
    }

    return logLvl <= maxVerbosity;
}

void logMsg(int level, const char* message, ...) {
    va_list args;

    if (!initialized){
        log_init();
    }

    if (shouldLog(level)) {
        va_start(args, message);
        logMsg_format(logLevels[level], message, args);
        va_end(args);
    }
}

void logMsg_format(const char* tag, const char* message, va_list args) {
    time_t now;
    time(&now);
    char * date = ctime(&now);
    date[strlen(date) - 1] = '\0';
    printf("%s [%s]:\t", date, tag);
    vprintf(message, args);
    printf("\n");
}