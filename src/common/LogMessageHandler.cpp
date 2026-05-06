#include "LogMessageHandler.h"
#include "TerminalSimLogger.h"

#include <QMessageLogContext>
#include <QString>

#include <cstring>

namespace TerminalSim {

namespace {

TerminalSimLogger *s_fileLogger      = nullptr;
QtMessageHandler   s_previousHandler = nullptr;

const char    k_prefix[]  = "terminalsim.";
constexpr int k_prefixLen = sizeof(k_prefix) - 1;

void terminalSimLogHandler(QtMsgType                 type,
                           const QMessageLogContext &ctx,
                           const QString            &msg)
{
    // Always delegate to previous handler (Qt default → stderr).
    if (s_previousHandler)
        s_previousHandler(type, ctx, msg);

    // Forward terminalsim.* messages to file logger.
    if (s_fileLogger)
    {
        const char *cat = ctx.category;
        if (cat && std::strncmp(cat, k_prefix, k_prefixLen) == 0)
            s_fileLogger->log(type, cat, msg);
    }
}

} // anonymous namespace

void installTerminalSimLogHandler(TerminalSimLogger *fileLogger)
{
    s_fileLogger      = fileLogger;
    s_previousHandler = qInstallMessageHandler(terminalSimLogHandler);
}

} // namespace TerminalSim
