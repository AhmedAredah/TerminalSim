#pragma once

#include <QLoggingCategory>

// Application startup
Q_DECLARE_LOGGING_CATEGORY(lcInit)

// Server layer
Q_DECLARE_LOGGING_CATEGORY(lcServer)
Q_DECLARE_LOGGING_CATEGORY(lcRabbitMQ)
Q_DECLARE_LOGGING_CATEGORY(lcCommandProcessor)

// Domain layer
Q_DECLARE_LOGGING_CATEGORY(lcTerminal)
Q_DECLARE_LOGGING_CATEGORY(lcTerminalGraph)
Q_DECLARE_LOGGING_CATEGORY(lcDwellTime)

// Graph algorithms (header-only library)
Q_DECLARE_LOGGING_CATEGORY(lcGraph)

// Common utilities
Q_DECLARE_LOGGING_CATEGORY(lcCommon)
