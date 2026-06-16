/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include <stdio.h>
#include <stdarg.h>
#include <mutex>

#include "log.h"

static int verbose_level = 0;
static bool progressLogging;

// The annealing search places parts from several threads at once, and the
// placer logs as it runs. Serialise every write to stderr so concurrent output
// can't interleave or race on the stream.
static std::mutex logMutex;

void increaseVerboseLevel()
{
    verbose_level++;
}

void enableProgressLogging()
{
    progressLogging = true;
}

void logError(const char* fmt, ...)
{
    std::lock_guard<std::mutex> lock(logMutex);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}

void _log(const char* fmt, ...)
{
    if (verbose_level < 1)
        return;

    std::lock_guard<std::mutex> lock(logMutex);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}
void logProgress(const char* type, int value, int maxValue)
{
    if (!progressLogging)
        return;

    std::lock_guard<std::mutex> lock(logMutex);
    fprintf(stderr, "Progress:%s:%i:%i\n", type, value, maxValue);
    fflush(stderr);
}
