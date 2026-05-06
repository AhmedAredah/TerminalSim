#include "TerminalSimLogger.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

namespace TerminalSim {

TerminalSimLogger *TerminalSimLogger::getInstance()
{
    static TerminalSimLogger instance;
    return &instance;
}

void TerminalSimLogger::startLogging(const QString &logDir)
{
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen())
        return;

    QDir dir(logDir);
    if (!dir.exists())
        dir.mkpath(".");

    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    const QString fileName =
        dir.filePath("terminalsim_" + timestamp + ".log");

    m_logFile.setFileName(fileName);
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text
                       | QIODevice::Append))
    {
        m_logStream.setDevice(&m_logFile);
    }
    else
    {
        qWarning() << "TerminalSimLogger: failed to open log file:" << fileName;
    }
}

void TerminalSimLogger::stopLogging()
{
    QMutexLocker locker(&m_mutex);
    if (!m_logFile.isOpen())
        return;
    m_logStream.flush();
    m_logStream.setDevice(nullptr);
    m_logFile.close();
}

void TerminalSimLogger::log(QtMsgType     type,
                            const char   *category,
                            const QString &msg)
{
    QMutexLocker locker(&m_mutex);
    if (!m_logFile.isOpen())
        return;

    const char *levelStr = [type]() -> const char * {
        switch (type) {
        case QtDebugMsg:    return "DEBUG";
        case QtInfoMsg:     return "INFO";
        case QtWarningMsg:  return "WARNING";
        case QtCriticalMsg: return "CRITICAL";
        case QtFatalMsg:    return "FATAL";
        default:            return "UNKNOWN";
        }
    }();

    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");

    m_logStream << "[" << timestamp << "] "
                << "[" << levelStr  << "] "
                << "[" << category  << "] "
                << msg << "\n";
    m_logStream.flush();
}

} // namespace TerminalSim
