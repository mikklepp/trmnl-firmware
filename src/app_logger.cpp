#include <trmnl_log.h>
#include <ArduinoLog.h>
#include <cstdarg>
#include <cstdio>
#include <bl.h>
#include <stored_logs.h>
#include <string_utils.h>

extern StoredLogs storedLogs;

/// Logs at or above this severity will be sent to the server
static LogLevel store_submit_threshold = LogLevel::LOG_ERROR;

static void handle_store_submit(LogLevel level, const char *clean_message, const char* file, int line, LogMode mode = LOG_STORE_ONLY)
{
    if (level >= store_submit_threshold)
    {
        if (mode == LOG_STORE_ONLY) {
            logWithAction(LOG_ACTION_STORE, clean_message, getTime(), line, file);
        } else {
            logWithAction(LOG_ACTION_SUBMIT_OR_STORE, clean_message, getTime(), line, file);
        }
    }
}

void log_impl(LogLevel level, LogMode mode, const char* file, int line, const char* format, ...) {
    const int MAX_USER_MESSAGE = 512;
    const int MAX_SERIAL_MESSAGE = 640;

    // Static rather than alloca(): these buffers used to be carved out of the
    // caller's stack on every log call, which overflows the 8 KB Arduino loop
    // task deep in a call chain — newlib's %f path (_dtoa_r) is stack-hungry on
    // its own. Neither buffer escapes this function, so a single shared copy is
    // safe. Logging is not reentrant here (single-threaded callers, no logging
    // from ISRs); if that ever changes this needs a lock.
    static char user_message[MAX_USER_MESSAGE];
    static char serial_buffer[MAX_SERIAL_MESSAGE];

    va_list args;
    va_start(args, format);

    // Format user message with truncation
    format_message_truncated(user_message, MAX_USER_MESSAGE, format, args);
    va_end(args);

    // Truncates rather than sizing to fit: the old code measured the exact
    // length and alloca'd it, so a long file path grew the stack frame without
    // bound.
    snprintf(serial_buffer, MAX_SERIAL_MESSAGE, "%s [%d]: %s", file, line, user_message);

    switch (level) {
    case LOG_VERBOSE:
        Log.verboseln(serial_buffer);
        break;
    case LOG_INFO:
        Log.infoln(serial_buffer);
        break;
    case LOG_ERROR:
        Log.errorln(serial_buffer);
        break;
    case LOG_FATAL:
        Log.fatalln(serial_buffer);
        break;
    }

    if (mode != LOG_SERIAL_ONLY)
    {
        handle_store_submit(level, user_message, file, line, mode);
    }
}
