# xpt.h

A robust, thread-safe, single-header C logging library with hierarchical loggers and pluggable sinks, inspired by Python's `logging` module.

## Features

- **Single-Header Implementation**: Just drop `xpt.h` into your project. Zero dependencies beyond the C standard library.
- **Hierarchical Loggers**: Organize loggers using dot-notation (e.g., `app.ui`, `app.db`). Configuration and messages propagate up the hierarchy.
- **Pluggable Sinks**: Route logs to multiple destinations (stdout, files, network, etc.) by attaching custom sink functions to any logger.
- **Granular Control**: Set minimum log levels per logger. Child loggers inherit their parent's level unless explicitly overridden.
- **Thread Safety**: Optional thread-safe logging using POSIX mutexes (enable with `#define XPT_THREAD_SAFE`).
- **Memory Efficient**: Uses a static registry for loggers to avoid dynamic allocation after initialization.

## Log Levels

| Level | Value | Description |
|-------|-------|-------------|
| `TRACE` | 0 | Finest-grained informational events |
| `DEBUG` | 10 | Useful for debugging applications |
| `INFO` | 20 | Progress of the application (Default) |
| `WARN` | 30 | Potentially harmful situations |
| `ERROR` | 40 | Error events that allow the app to continue |
| `FATAL` | 50 | Severe errors leading to application abort |
| `NONE` | 100 | Disables all logging |

## Quick Start

### 1. Integration
Define `XPT_IMPLEMENTATION` in **one** C file before including `xpt.h` to create the implementation.

```c
#define XPT_IMPLEMENTATION
#include "xpt.h"
```

### 2. Basic Usage
```c
#include "xpt.h"

int main() {
    xpt_init(); // Initializes root logger with console output

    xpt_info("main", "Application started");
    xpt_warn("main", "Low memory detected: %d MB", 128);

    return 0;
}
```

### 3. Hierarchical Levels and Inheritance
```c
xpt_init();

// Set 'app' to only show WARNING and above
xpt_set_level("app", XPT_LEVEL_WARN);

xpt_info("app.net", "This won't show");  // Inherits WARN from 'app'
xpt_error("app.db", "Database failed"); // This WILL show
```

### 4. Custom Sinks
You can intercept logs to write to a file, a buffer, or a remote server.

```c
void file_sink(xpt_level_t level, const char* name, const char* msg, void* userdata) {
    FILE* f = (FILE*)userdata;
    fprintf(f, "[%s] %s
", name, msg);
}

int main() {
    xpt_init();
    FILE* my_log = fopen("app.log", "a");

    // Add a file sink to the root logger (affects everything)
    xpt_add_sink("", file_sink, my_log);

    xpt_info("app", "This goes to both console and file!");

    fclose(my_log);
    return 0;
}
```

## Compilation
To enable thread safety, link with `lpthread`:

```bash
gcc -DXPT_THREAD_SAFE main.c -lpthread
```
