#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>

#include "common/LogCategories.h"
#include "common/LogMessageHandler.h"
#include "common/TerminalSimLogger.h"
#include "server/terminal_graph_server.h"


bool isAnotherInstanceRunning(const QString &serverName)
{
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(100))
        return true;
    return false;
}

void createLocalServer(const QString &serverName)
{
    QLocalServer *localServer = new QLocalServer();
    localServer->setSocketOptions(QLocalServer::WorldAccessOption);
    if (!localServer->listen(serverName))
    {
        qCCritical(lcInit) << "Failed to create local server:"
                           << localServer->errorString();
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    // Configure filter rules — debug on in Debug builds, off in Release.
    // Override at runtime: QT_LOGGING_RULES="terminalsim.*=true"
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
    QCoreApplication::setApplicationName("TerminalSim");
    QCoreApplication::setApplicationVersion("1.0.0");

    const QString uniqueServerName = "TerminalSimServerInstance";

    if (isAnotherInstanceRunning(uniqueServerName))
    {
        qCCritical(lcInit) << "Another instance of TerminalSim "
                              "Server is already running.";
        return EXIT_FAILURE;
    }

    createLocalServer(uniqueServerName);

    QCommandLineParser parser;
    parser.setApplicationDescription("TerminalSim Server");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption rabbitHostOption(
        QStringList() << "H" << "host",
        "RabbitMQ host address", "host", "localhost");
    QCommandLineOption rabbitPortOption(
        QStringList() << "p" << "port",
        "RabbitMQ port", "port", "5672");
    QCommandLineOption rabbitUserOption(
        QStringList() << "u" << "user",
        "RabbitMQ username", "username", "guest");
    QCommandLineOption rabbitPasswordOption(
        QStringList() << "w" << "password",
        "RabbitMQ password", "password", "guest");
    QCommandLineOption dataPathOption(
        QStringList() << "d" << "data-path",
        "Path to terminal data directory", "path", "./data");
    QCommandLineOption loadGraphOption(
        QStringList() << "l" << "load",
        "Load graph from file", "file");

    parser.addOption(rabbitHostOption);
    parser.addOption(rabbitPortOption);
    parser.addOption(rabbitUserOption);
    parser.addOption(rabbitPasswordOption);
    parser.addOption(dataPathOption);
    parser.addOption(loadGraphOption);

    parser.process(app);

    const QString rabbitHost     = parser.value(rabbitHostOption);
    const int     rabbitPort     = parser.value(rabbitPortOption).toInt();
    const QString rabbitUser     = parser.value(rabbitUserOption);
    const QString rabbitPassword = parser.value(rabbitPasswordOption);
    const QString dataPath       = parser.value(dataPathOption);
    const QString loadGraphFile  = parser.value(loadGraphOption);

    QDir dataDir(dataPath);
    if (!dataDir.exists())
        dataDir.mkpath(".");

    // Start file logger (logs go to <dataPath>/logs/).
    TerminalSim::TerminalSimLogger::getInstance()
        ->startLogging(dataPath + "/logs");

    // Install handler: stderr output + file logging for terminalsim.*.
    TerminalSim::installTerminalSimLogHandler(
        TerminalSim::TerminalSimLogger::getInstance());

    qCDebug(lcInit) << "Starting TerminalSim Server...";
    qCDebug(lcInit) << "RabbitMQ Host:" << rabbitHost;
    qCDebug(lcInit) << "RabbitMQ Port:" << rabbitPort;
    qCDebug(lcInit) << "Data Path:"     << dataPath;

    TerminalSim::TerminalGraphServer *server =
        TerminalSim::TerminalGraphServer::getInstance(dataPath);

    if (!server->initialize(rabbitHost, rabbitPort,
                            rabbitUser, rabbitPassword))
    {
        qCCritical(lcInit) << "Failed to initialize server. Exiting.";
        return 1;
    }

    qCInfo(lcInit) << "Server initialized and connected to RabbitMQ";
    qCInfo(lcInit) << "Listening for commands...";

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [server]() {
        qCInfo(lcInit) << "Shutting down server...";
        server->shutdown();
        TerminalSim::TerminalSimLogger::getInstance()->stopLogging();
    });

    return app.exec();
}
