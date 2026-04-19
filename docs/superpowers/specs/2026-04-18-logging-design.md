# TerminalSim Logging Design

**Date:** 2026-04-18
**Status:** Approved

## Goal

Add structured, categorized logging to TerminalSim that mirrors CargoNetSim's `QLoggingCategory` architecture. Provides per-module filtering, persistent file output (replacing the GUI tabs CargoNetSim uses), and full migration of all existing plain `qDebug`/`qWarning`/`qCritical` calls to categorized equivalents.

---

## Architecture

Three new files in `src/common/`, alongside the existing `common.h/.cpp`:

```
src/common/
  LogCategories.h          — Q_DECLARE_LOGGING_CATEGORY (9 modules)
  LogCategories.cpp        — Q_LOGGING_CATEGORY definitions (terminalsim.*)
  LogMessageHandler.h      — installTerminalSimLogHandler() declaration
  LogMessageHandler.cpp    — Qt custom handler: stderr + file bridge
  TerminalSimLogger.h      — singleton file+stderr writer
  TerminalSimLogger.cpp    — implementation
```

No existing files are relocated. All source files gain `#include "common/LogCategories.h"` (path relative to their depth) and have their plain logging calls migrated.

---

## Log Categories

| Variable | String | Covers |
|---|---|---|
| `lcInit` | `terminalsim.init` | `main.cpp` startup |
| `lcServer` | `terminalsim.server` | `TerminalGraphServer` |
| `lcRabbitMQ` | `terminalsim.rabbitmq` | `RabbitMQHandler` |
| `lcCommandProcessor` | `terminalsim.commands` | `CommandProcessor` |
| `lcTerminal` | `terminalsim.terminal` | `Terminal` |
| `lcTerminalGraph` | `terminalsim.terminal.graph` | `TerminalGraph` |
| `lcDwellTime` | `terminalsim.dwelltime` | `ContainerDwellTime` |
| `lcGraph` | `terminalsim.graph` | `Graph.h`, `Algorithms.h` |
| `lcCommon` | `terminalsim.common` | `common.cpp`, `EnumUtils` |

The `terminalsim.*` prefix enables wildcard filtering identical to CargoNetSim's `cargonetsim.*` pattern.

**Runtime filtering example:**
```
QT_LOGGING_RULES="terminalsim.terminal.*=true;terminalsim.rabbitmq=false"
```

---

## TerminalSimLogger

Singleton. Opens a timestamped log file on startup and writes every message that passes through the custom handler. Thread-safe via `QMutex`.

### File naming

`<logDir>/terminalsim_YYYY-MM-DD_HH-MM-SS.log`

`logDir` defaults to `<dataPath>/logs/`. The directory is created if it does not exist.

### Log entry format

```
[YYYY-MM-DD hh:mm:ss.zzz] [LEVEL] [category] message
```

`LEVEL` is one of `DEBUG`, `INFO`, `WARNING`, `CRITICAL`.

### Interface

```cpp
namespace TerminalSim {

class TerminalSimLogger {
public:
    static TerminalSimLogger *getInstance();
    void startLogging(const QString &logDir);
    void stopLogging();
    void log(QtMsgType type, const char *category, const QString &msg);

private:
    TerminalSimLogger() = default;
    QFile       m_logFile;
    QTextStream m_logStream;
    QMutex      m_mutex;
    static TerminalSimLogger *s_instance;
};

} // namespace TerminalSim
```

- `startLogging()` is called from `main()` after `--data-path` is parsed.
- `stopLogging()` is called in the `aboutToQuit` handler.
- No async queue — this is a CLI server with no UI thread to protect.

---

## LogMessageHandler

Mirrors CargoNetSim's `LogMessageHandler.cpp` exactly. Two file-static pointers:

- `s_fileLogger` — `TerminalSimLogger*`, set by `installTerminalSimLogHandler()`
- `s_previousHandler` — Qt's default stderr handler, captured at install time

On each message the handler:
1. Delegates to `s_previousHandler` → stderr with Qt's standard formatting
2. If `s_fileLogger` is non-null **and** the category starts with `terminalsim.` → calls `s_fileLogger->log(type, category, msg)`

### Install function

```cpp
namespace TerminalSim {
    void installTerminalSimLogHandler(TerminalSimLogger *fileLogger = nullptr);
}
```

Passing `nullptr` gives stderr-only output (useful in unit tests).

---

## `main.cpp` Changes

```cpp
#include "common/LogCategories.h"
#include "common/LogMessageHandler.h"
#include "common/TerminalSimLogger.h"

int main(int argc, char *argv[]) {
    // 1. Configure filter rules (same pattern as CargoNetSim CLI)
#ifdef QT_DEBUG
    QLoggingCategory::setFilterRules(QStringLiteral(
        "terminalsim.*.debug=true\n"
        "terminalsim.*.info=true\n"
        "terminalsim.*.warning=true\n"
        "terminalsim.*.critical=true\n"));
#else
    QLoggingCategory::setFilterRules(QStringLiteral(
        "terminalsim.*.debug=false\n"
        "terminalsim.*.info=true\n"
        "terminalsim.*.warning=true\n"
        "terminalsim.*.critical=true\n"));
#endif

    QCoreApplication app(argc, argv);
    // ... argument parsing ...

    // 2. Start file logger (after dataPath is parsed)
    TerminalSim::TerminalSimLogger::getInstance()
        ->startLogging(dataPath + "/logs");

    // 3. Install handler
    TerminalSim::installTerminalSimLogHandler(
        TerminalSim::TerminalSimLogger::getInstance());

    // 4. Stop logger on shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        TerminalSim::TerminalSimLogger::getInstance()->stopLogging();
    });

    // ... rest of main ...
}
```

All existing plain calls in `main.cpp` migrate to `lcInit`:
- `qDebug()` → `qCDebug(lcInit)`
- `qInfo()` → `qCInfo(lcInit)`
- `qCritical()` → `qCCritical(lcInit)`

---

## Migration Map

| File | Category |
|---|---|
| `src/main.cpp` | `lcInit` |
| `src/server/terminal_graph_server.cpp` | `lcServer` |
| `src/server/rabbit_mq_handler.cpp` | `lcRabbitMQ` |
| `src/server/command_processor.cpp` | `lcCommandProcessor` |
| `src/terminal/terminal.cpp` | `lcTerminal` |
| `src/terminal/terminal_graph.cpp` | `lcTerminalGraph` |
| `src/dwell_time/container_dwell_time.cpp` | `lcDwellTime` |
| `src/graph/Graph.h`, `src/graph/Algorithms.h` | `lcGraph` |
| `src/common/common.cpp` | `lcCommon` |

Each file gains `#include "common/LogCategories.h"` with the path adjusted for directory depth (e.g., `src/terminal/terminal.cpp` uses `"../common/LogCategories.h"`).

---

## CMakeLists.txt Changes

The six new files must be added to the `TerminalSim` target sources:

```cmake
target_sources(TerminalSim PRIVATE
    src/common/LogCategories.cpp
    src/common/LogMessageHandler.cpp
    src/common/TerminalSimLogger.cpp
)
```

Headers are discovered automatically via `target_include_directories`.

---

## Out of Scope

- Log file rotation (single file per process run is sufficient)
- Async logging queue (no GUI thread to protect)
- Structured/JSON log format
- GUI log viewer
