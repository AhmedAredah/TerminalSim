// terminal_graph_server.cpp
#include "terminal_graph_server.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QDateTime>
#include <QUuid>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <stdexcept>

namespace TerminalSim {

// Initialize static members
TerminalGraphServer* TerminalGraphServer::s_instance = nullptr;
QMutex TerminalGraphServer::s_instanceMutex;

TerminalGraphServer*
TerminalGraphServer::getInstance(const QString& pathToTerminalsDirectory)
{
    QMutexLocker locker(&s_instanceMutex);
    
    if (!s_instance) {
        s_instance = new TerminalGraphServer(pathToTerminalsDirectory);
    }
    
    return s_instance;
}

TerminalGraphServer::TerminalGraphServer(
    const QString& pathToTerminalsDirectory)
    : QObject(nullptr),
    m_graph(new TerminalGraph(pathToTerminalsDirectory)),
    m_pathToTerminalsDirectory(pathToTerminalsDirectory),
    m_rabbitMQHandler(nullptr),
    m_commandProcessor(nullptr),
    m_serverId(QUuid::createUuid().toString())
{
    qDebug() << "Terminal Graph Server created with ID:" << m_serverId
             << "and terminal directory:"
             << (!pathToTerminalsDirectory.isEmpty() ?
                     pathToTerminalsDirectory : "None");
    
    // Create command processor
    m_commandProcessor = new CommandProcessor(m_graph, this);
}

TerminalGraphServer::~TerminalGraphServer()
{
    QMutexLocker locker(&m_mutex);
    
    // Disconnect from RabbitMQ
    if (m_rabbitMQHandler) {
        m_rabbitMQHandler->disconnect();
        delete m_rabbitMQHandler;
        m_rabbitMQHandler = nullptr;
    }
    
    // Delete graph
    delete m_graph;
    m_graph = nullptr;
    
    qDebug() << "Terminal Graph Server destroyed";
    
    // Reset singleton instance
    QMutexLocker instanceLocker(&s_instanceMutex);
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool TerminalGraphServer::initialize(
    const QString& rabbitMQHost,
    int rabbitMQPort,
    const QString& rabbitMQUser,
    const QString& rabbitMQPassword)
{
    QMutexLocker locker(&m_mutex);
    
    // Create RabbitMQ handler if not already created
    if (!m_rabbitMQHandler) {
        m_rabbitMQHandler = new RabbitMQHandler(nullptr);
        
        // Connect signals
        connect(m_rabbitMQHandler, &RabbitMQHandler::connectionChanged,
                this, &TerminalGraphServer::connectionChanged);

        connect(m_rabbitMQHandler, &RabbitMQHandler::commandReceived,
                this, &TerminalGraphServer::onMessageReceived);
    }
    
    // Connect to RabbitMQ
    bool connected = m_rabbitMQHandler->connect(
        rabbitMQHost, 
        rabbitMQPort, 
        rabbitMQUser, 
        rabbitMQPassword
        );
    
    if (connected) {
        
        qDebug() << "Terminal Graph Server initialized "
                    "and connected to RabbitMQ at"
                 << rabbitMQHost << ":" << rabbitMQPort;
    } else {
        qWarning() << "Failed to connect to RabbitMQ at"
                   << rabbitMQHost << ":" << rabbitMQPort;
    }
    
    return connected;
}

void TerminalGraphServer::shutdown()
{
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "Shutting down Terminal Graph Server...";
    
    // Disconnect from RabbitMQ
    if (m_rabbitMQHandler) {
        m_rabbitMQHandler->disconnect();
    }
    
    // Signal application to quit
    QTimer::singleShot(0,
                       QCoreApplication::instance(),
                       &QCoreApplication::quit);
}

bool TerminalGraphServer::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    
    return m_rabbitMQHandler && m_rabbitMQHandler->isConnected();
}

QVariant
TerminalGraphServer::processCommand(const QString& command,
                                    const QVariantMap& params)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_commandProcessor) {
        qWarning() << "Cannot process command: command processor is null";
        throw std::runtime_error("Command processor is not initialized");
    }
    
    return m_commandProcessor->processCommand(command, params);
}

void TerminalGraphServer::onMessageReceived(const QJsonObject& message)
{
    QMutexLocker locker(&m_mutex);
    
    // Emit signal for monitoring
    emit messageReceived(message);
    
    if (!m_commandProcessor || !m_rabbitMQHandler) {
        qWarning() << "Cannot process message: command processor or "
                      "RabbitMQ handler is null";
        return;
    }
    
    // Process the message
    QJsonObject response;
    
    try {
        response = m_commandProcessor->processJsonCommand(message);
        
        // Add server ID to response
        response["server_id"] = m_serverId;
        
        // Copy message ID if present
        if (message.contains("message_id")) {
            response["message_id"] = message["message_id"];
        }
        
        // Add processed timestamp
        response["processed_timestamp"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    } catch (const std::exception& e) {
        // Create error response
        response["success"] = false;
        response["error"] = QString("Internal server error: %1").arg(e.what());
        response["server_id"] = m_serverId;
        
        // Copy message ID and request ID if present
        if (message.contains("message_id")) {
            response["message_id"] = message["message_id"];
        }
        if (message.contains("request_id")) {
            response["request_id"] = message["request_id"];
        }
        
        response["processed_timestamp"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    
    // Emit signal for monitoring
    emit messageSending(response);
    
    // Send response
    m_rabbitMQHandler->sendResponse(response);
}

} // namespace TerminalSim
