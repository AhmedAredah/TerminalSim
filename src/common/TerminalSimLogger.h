#pragma once

#include <QtGlobal>
#include <QFile>
#include <QMutex>
#include <QTextStream>
#include <QString>

namespace TerminalSim {

class TerminalSimLogger
{
public:
    static TerminalSimLogger *getInstance();

    void startLogging(const QString &logDir);
    void stopLogging();
    void log(QtMsgType type, const char *category, const QString &msg);

private:
    TerminalSimLogger()  = default;
    ~TerminalSimLogger() = default;
    TerminalSimLogger(const TerminalSimLogger &)            = delete;
    TerminalSimLogger &operator=(const TerminalSimLogger &) = delete;

    QFile       m_logFile;
    QTextStream m_logStream;
    QMutex      m_mutex;
};

} // namespace TerminalSim
