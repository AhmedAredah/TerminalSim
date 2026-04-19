#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "TerminalSimLogger.h"

class TestTerminalSimLogger : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void test_startLogging_createsLogFile();
    void test_log_writesFormattedEntry();
    void test_stopLogging_allowsRestartWithNewDir();
};

void TestTerminalSimLogger::cleanup()
{
    TerminalSim::TerminalSimLogger::getInstance()->stopLogging();
}

void TestTerminalSimLogger::test_startLogging_createsLogFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TerminalSim::TerminalSimLogger::getInstance()->startLogging(dir.path());

    QDir logDir(dir.path());
    QStringList files = logDir.entryList({"terminalsim_*.log"}, QDir::Files);
    QCOMPARE(files.size(), 1);
}

void TestTerminalSimLogger::test_log_writesFormattedEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto *logger = TerminalSim::TerminalSimLogger::getInstance();
    logger->startLogging(dir.path());
    logger->log(QtInfoMsg,     "terminalsim.test", "hello world");
    logger->log(QtWarningMsg,  "terminalsim.test", "a warning");
    logger->log(QtCriticalMsg, "terminalsim.test", "a critical error");
    logger->stopLogging();

    QDir logDir(dir.path());
    QStringList files = logDir.entryList({"terminalsim_*.log"}, QDir::Files);
    QCOMPARE(files.size(), 1);

    QFile f(dir.filePath(files.first()));
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());

    QVERIFY(content.contains("[INFO]"));
    QVERIFY(content.contains("[WARNING]"));
    QVERIFY(content.contains("[CRITICAL]"));
    QVERIFY(content.contains("[terminalsim.test]"));
    QVERIFY(content.contains("hello world"));
    QVERIFY(content.contains("a warning"));
    QVERIFY(content.contains("a critical error"));
}

void TestTerminalSimLogger::test_stopLogging_allowsRestartWithNewDir()
{
    QTemporaryDir dir1, dir2;
    QVERIFY(dir1.isValid());
    QVERIFY(dir2.isValid());

    auto *logger = TerminalSim::TerminalSimLogger::getInstance();

    logger->startLogging(dir1.path());
    logger->log(QtInfoMsg, "terminalsim.test", "first session");
    logger->stopLogging();

    logger->startLogging(dir2.path());
    logger->log(QtInfoMsg, "terminalsim.test", "second session");
    logger->stopLogging();

    QDir d1(dir1.path()), d2(dir2.path());
    QCOMPARE(d1.entryList({"terminalsim_*.log"}, QDir::Files).size(), 1);
    QCOMPARE(d2.entryList({"terminalsim_*.log"}, QDir::Files).size(), 1);
}

QTEST_MAIN(TestTerminalSimLogger)
#include "test_terminalsim_logger.moc"
