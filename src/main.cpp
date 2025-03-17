#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>

#include "server/terminal_graph_server.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TerminalSimulation");
    QCoreApplication::setApplicationVersion("1.0.0");
    
    // Command line parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("Terminal Simulation Server");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption rabbitHostOption(
        QStringList() << "h" << "host",
        "RabbitMQ host address",
        "host",
        "localhost"
    );
    
    QCommandLineOption rabbitPortOption(
        QStringList() << "p" << "port",
        "RabbitMQ port",
        "port",
        "5672"
    );
    
    QCommandLineOption rabbitUserOption(
        QStringList() << "u" << "user",
        "RabbitMQ username",
        "username",
        "guest"
    );
    
    QCommandLineOption rabbitPasswordOption(
        QStringList() << "w" << "password",
        "RabbitMQ password",
        "password",
        "guest"
    );
    
    QCommandLineOption dataPathOption(
        QStringList() << "d" << "data-path",
        "Path to terminal data directory",
        "path",
        "./data"
    );
    
    QCommandLineOption loadGraphOption(
        QStringList() << "l" << "load",
        "Load graph from file",
        "file"
    );
    
    parser.addOption(rabbitHostOption);
    parser.addOption(rabbitPortOption);
    parser.addOption(rabbitUserOption);
    parser.addOption(rabbitPasswordOption);
    parser.addOption(dataPathOption);
    parser.addOption(loadGraphOption);
    
    parser.process(app);
    
    // Get command line values
    QString rabbitHost = parser.value(rabbitHostOption);
    int rabbitPort = parser.value(rabbitPortOption).toInt();
    QString rabbitUser = parser.value(rabbitUserOption);
    QString rabbitPassword = parser.value(rabbitPasswordOption);
    QString dataPath = parser.value(dataPathOption);
    QString loadGraphFile = parser.value(loadGraphOption);
    
    // Ensure data directory exists
    QDir dataDir(dataPath);
    if (!dataDir.exists()) {
        dataDir.mkpath(".");
    }
    
    qInfo() << "Starting Terminal Simulation Server...";
    qInfo() << "RabbitMQ Host:" << rabbitHost;
    qInfo() << "RabbitMQ Port:" << rabbitPort;
    qInfo() << "Data Path:" << dataPath;
    
    // Initialize server
    TerminalSim::TerminalGraphServer* server = 
        TerminalSim::TerminalGraphServer::getInstance(dataPath);
    
    // Load graph if requested
    if (!loadGraphFile.isEmpty()) {
        qInfo() << "Loading graph from" << loadGraphFile;
        if (!server->loadGraph(loadGraphFile)) {
            qWarning() << "Failed to load graph from" << loadGraphFile;
        }
    }
    
    // Connect to RabbitMQ
    if (!server->initialize(rabbitHost, rabbitPort, rabbitUser, rabbitPassword)) {
        qCritical() << "Failed to initialize server. Exiting.";
        return 1;
    }
    
    qInfo() << "Server initialized and connected to RabbitMQ";
    qInfo() << "Listening for commands...";
    
    // Handle graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [server]() {
        qInfo() << "Shutting down server...";
        server->shutdown();
    });
    
    return app.exec();
}