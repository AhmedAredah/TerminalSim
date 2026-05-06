#include "LogCategories.h"

// Application startup
Q_LOGGING_CATEGORY(lcInit,             "terminalsim.init")

// Server layer
Q_LOGGING_CATEGORY(lcServer,           "terminalsim.server")
Q_LOGGING_CATEGORY(lcRabbitMQ,         "terminalsim.rabbitmq")
Q_LOGGING_CATEGORY(lcCommandProcessor, "terminalsim.commands")

// Domain layer
Q_LOGGING_CATEGORY(lcTerminal,         "terminalsim.terminal")
Q_LOGGING_CATEGORY(lcTerminalGraph,    "terminalsim.terminal.graph")
Q_LOGGING_CATEGORY(lcDwellTime,        "terminalsim.dwelltime")

// Graph algorithms
Q_LOGGING_CATEGORY(lcGraph,            "terminalsim.graph")

// Common utilities
Q_LOGGING_CATEGORY(lcCommon,           "terminalsim.common")
