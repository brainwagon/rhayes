#ifndef XPT_H
#define XPT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Log Levels
 */
typedef enum {
    XPT_LEVEL_TRACE = 0,
    XPT_LEVEL_DEBUG = 10,
    XPT_LEVEL_INFO  = 20,
    XPT_LEVEL_WARN  = 30,
    XPT_LEVEL_ERROR = 40,
    XPT_LEVEL_FATAL = 50,
    XPT_LEVEL_NONE  = 100,
    XPT_LEVEL_UNSET = -1
} xpt_level_t;

/*
 * Sink (Handler) Function Type
 */
typedef void (*xpt_sink_fn)(xpt_level_t level, const char* name, const char* msg, void* userdata);

/*
 * Public API
 */

/**
 * @brief Initialize the xpt system. Must be called once before any logging.
 */
void xpt_init(void);

/**
 * @brief Configure a logger's minimum level.
 * @param name Logger name (e.g., "app.net"). NULL or empty for root logger.
 * @param level The minimum level to log.
 */
void xpt_set_level(const char* name, xpt_level_t level);

/**
 * @brief Add a sink to a logger.
 * @param name Logger name. NULL or empty for root logger.
 * @param sink_fn Function to call for each log message.
 * @param userdata User-provided data passed to sink_fn.
 */
void xpt_add_sink(const char* name, xpt_sink_fn sink_fn, void* userdata);

/**
 * @brief Log a message at a specific level.
 * @param name Logger name.
 * @param level Log level.
 * @param fmt Format string (printf-style).
 */
void xpt_log(const char* name, xpt_level_t level, const char* fmt, ...);

/*
 * Convenience Macros
 */
#define xpt_trace(name, ...) xpt_log(name, XPT_LEVEL_TRACE, __VA_ARGS__)
#define xpt_debug(name, ...) xpt_log(name, XPT_LEVEL_DEBUG, __VA_ARGS__)
#define xpt_info(name, ...)  xpt_log(name, XPT_LEVEL_INFO,  __VA_ARGS__)
#define xpt_warn(name, ...)  xpt_log(name, XPT_LEVEL_WARN,  __VA_ARGS__)
#define xpt_error(name, ...) xpt_log(name, XPT_LEVEL_ERROR, __VA_ARGS__)
#define xpt_fatal(name, ...) xpt_log(name, XPT_LEVEL_FATAL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* XPT_H */

#ifdef XPT_IMPLEMENTATION

#ifdef XPT_THREAD_SAFE
#include <pthread.h>
static pthread_mutex_t g_xpt_mutex = PTHREAD_MUTEX_INITIALIZER;
#define XPT_LOCK() pthread_mutex_lock(&g_xpt_mutex)
#define XPT_UNLOCK() pthread_mutex_unlock(&g_xpt_mutex)
#else
#define XPT_LOCK()
#define XPT_UNLOCK()
#endif

#ifndef XPT_MAX_LOGGERS
#define XPT_MAX_LOGGERS 128
#endif

#ifndef XPT_MAX_SINKS_PER_LOGGER
#define XPT_MAX_SINKS_PER_LOGGER 8
#endif

#ifndef XPT_MAX_TOTAL_SINKS
#define XPT_MAX_TOTAL_SINKS 32
#endif

typedef struct {
    xpt_sink_fn fn;
    void* userdata;
} xpt_sink_t;

typedef struct {
    char name[64];
    xpt_level_t level;
    xpt_sink_t sinks[XPT_MAX_SINKS_PER_LOGGER];
    int num_sinks;
    bool propagate;
} xpt_logger_t;

static xpt_logger_t g_loggers[XPT_MAX_LOGGERS];
static int g_num_loggers = 0;

static xpt_logger_t* find_or_create_logger_unlocked(const char* name) {
    if (name == NULL) name = "";
    
    for (int i = 0; i < g_num_loggers; ++i) {
        if (strcmp(g_loggers[i].name, name) == 0) {
            return &g_loggers[i];
        }
    }
    
    if (g_num_loggers >= XPT_MAX_LOGGERS) return NULL;
    
    xpt_logger_t* logger = &g_loggers[g_num_loggers++];
    strncpy(logger->name, name, sizeof(logger->name) - 1);
    logger->name[sizeof(logger->name) - 1] = '\0';
    logger->level = XPT_LEVEL_UNSET;
    logger->num_sinks = 0;
    logger->propagate = true;
    return logger;
}

static void default_console_sink(xpt_level_t level, const char* name, const char* msg, void* userdata) {
    (void)userdata;
    const char* level_str = "UNKNOWN";
    switch (level) {
        case XPT_LEVEL_TRACE: level_str = "TRACE"; break;
        case XPT_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case XPT_LEVEL_INFO:  level_str = "INFO";  break;
        case XPT_LEVEL_WARN:  level_str = "WARN";  break;
        case XPT_LEVEL_ERROR: level_str = "ERROR"; break;
        case XPT_LEVEL_FATAL: level_str = "FATAL"; break;
        default: break;
    }
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stderr, "[%s] [%s] [%s] %s\n", time_str, level_str, name, msg);
}

void xpt_init(void) {
    XPT_LOCK();
    g_num_loggers = 0;
    xpt_logger_t* root = find_or_create_logger_unlocked("");
    if (root) {
        root->level = XPT_LEVEL_INFO;
        root->num_sinks = 0;
        root->sinks[0].fn = default_console_sink;
        root->sinks[0].userdata = NULL;
        root->num_sinks = 1;
    }
    XPT_UNLOCK();
}

void xpt_set_level(const char* name, xpt_level_t level) {
    XPT_LOCK();
    xpt_logger_t* logger = find_or_create_logger_unlocked(name);
    if (logger) logger->level = level;
    XPT_UNLOCK();
}

void xpt_add_sink(const char* name, xpt_sink_fn sink_fn, void* userdata) {
    XPT_LOCK();
    xpt_logger_t* logger = find_or_create_logger_unlocked(name);
    if (logger && logger->num_sinks < XPT_MAX_SINKS_PER_LOGGER) {
        logger->sinks[logger->num_sinks].fn = sink_fn;
        logger->sinks[logger->num_sinks].userdata = userdata;
        logger->num_sinks++;
    }
    XPT_UNLOCK();
}

static xpt_level_t get_effective_level_unlocked(xpt_logger_t* logger) {
    while (logger != NULL) {
        if (logger->level != XPT_LEVEL_UNSET) return logger->level;
        
        // Find parent logger
        char parent_name[64];
        strncpy(parent_name, logger->name, sizeof(parent_name));
        parent_name[sizeof(parent_name) - 1] = '\0';
        char* last_dot = strrchr(parent_name, '.');
        if (last_dot) {
            *last_dot = '\0';
            logger = find_or_create_logger_unlocked(parent_name);
        } else if (logger->name[0] != '\0') {
            logger = find_or_create_logger_unlocked("");
        } else {
            break;
        }
    }
    return XPT_LEVEL_INFO;
}

void xpt_log(const char* name, xpt_level_t level, const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    xpt_sink_t sinks_to_call[XPT_MAX_TOTAL_SINKS];
    int num_sinks_to_call = 0;

    XPT_LOCK();
    xpt_logger_t* logger = find_or_create_logger_unlocked(name);
    if (!logger || level < get_effective_level_unlocked(logger)) {
        XPT_UNLOCK();
        return;
    }
    
    // Propagate up the hierarchy and collect sinks
    xpt_logger_t* current = logger;
    while (current != NULL && num_sinks_to_call < XPT_MAX_TOTAL_SINKS) {
        for (int i = 0; i < current->num_sinks && num_sinks_to_call < XPT_MAX_TOTAL_SINKS; ++i) {
            sinks_to_call[num_sinks_to_call++] = current->sinks[i];
        }
        
        if (!current->propagate) break;
        
        // Find parent
        char parent_name[64];
        strncpy(parent_name, current->name, sizeof(parent_name));
        parent_name[sizeof(parent_name) - 1] = '\0';
        char* last_dot = strrchr(parent_name, '.');
        if (last_dot) {
            *last_dot = '\0';
            current = find_or_create_logger_unlocked(parent_name);
        } else if (current->name[0] != '\0') {
            current = find_or_create_logger_unlocked("");
        } else {
            current = NULL;
        }
    }
    XPT_UNLOCK();

    // Call collected sinks outside of lock
    for (int i = 0; i < num_sinks_to_call; ++i) {
        sinks_to_call[i].fn(level, name, msg, sinks_to_call[i].userdata);
    }
}

#endif /* XPT_IMPLEMENTATION */
