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

TerminalGraphServer* TerminalGraphServer::getInstance(const QString& pathToTerminalsDirectory)
{
    QMutexLocker locker(&s_instanceMutex);
    
    if (!s_instance) {
        s_instance = new TerminalGraphServer(pathToTerminalsDirectory);
    }
    
    return s_instance;
}

TerminalGraphServer::TerminalGraphServer(const QString& pathToTerminalsDirectory)
    : QObject(nullptr),
      m_graph(new TerminalGraph(pathToTerminalsDirectory)),
      m_pathToTerminalsDirectory(pathToTerminalsDirectory),
      m_rabbitMQHandler(nullptr),
      m_commandProcessor(nullptr),
      m_heartbeatTimer(nullptr),
      m_serverId(QUuid::createUuid().toString())
{
    qInfo() << "Terminal Graph Server created with ID:" << m_serverId
            << "and terminal directory:" << (!pathToTerminalsDirectory.isEmpty() ? pathToTerminalsDirectory : "None");
    
    // Create command processor
    m_commandProcessor = new CommandProcessor(m_graph, this);
    
    // Create heartbeat timer
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &TerminalGraphServer::sendHeartbeat);
}

TerminalGraphServer::~TerminalGraphServer()
{
    QMutexLocker locker(&m_mutex);
    
    // Stop heartbeat timer
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
    
    // Disconnect from RabbitMQ
    if (m_rabbitMQHandler) {
        m_rabbitMQHandler->disconnect();
        delete m_rabbitMQHandler;
        m_rabbitMQHandler = nullptr;
    }
    
    // Delete graph
    delete m_graph;
    m_graph = nullptr;
    
    qInfo() << "Terminal Graph Server destroyed";
    
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
        m_rabbitMQHandler = new RabbitMQHandler(this);
        
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
        // Start heartbeat timer
        m_heartbeatTimer->start(30000); // 30 seconds
        
        qInfo() << "Terminal Graph Server initialized and connected to RabbitMQ at"
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
    
    qInfo() << "Shutting down Terminal Graph Server...";
    
    // Stop heartbeat timer
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
    
    // Disconnect from RabbitMQ
    if (m_rabbitMQHandler) {
        m_rabbitMQHandler->disconnect();
    }
    
    // Signal application to quit
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
}

bool TerminalGraphServer::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    
    return m_rabbitMQHandler && m_rabbitMQHandler->isConnected();
}

QJsonObject TerminalGraphServer::serializeGraph() const
{    
    if (!m_graph) {
        qWarning() << "Cannot serialize graph: graph is null";
        return QJsonObject();
    }
    
    return m_graph->serializeGraph();
}

bool TerminalGraphServer::deserializeGraph(const QJsonObject& graphData)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_graph) {
        qWarning() << "Cannot deserialize graph: graph is null";
        return false;
    }
    
    try {
        // Create a new graph from the data
        TerminalGraph* newGraph = TerminalGraph::deserializeGraph(graphData, m_pathToTerminalsDirectory);
        
        // Store the old graph for deletion
        TerminalGraph* oldGraph = m_graph;
        
        // Point to the new graph
        m_graph = newGraph;
        
        // Update the command processor with the new graph
        if (m_commandProcessor) {
            delete m_commandProcessor;
            m_commandProcessor = new CommandProcessor(m_graph, this);
        }
        
        // Delete the old graph
        delete oldGraph;
        
        qInfo() << "Graph deserialized successfully";
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Failed to deserialize graph:" << e.what();
        return false;
    }
}

bool TerminalGraphServer::saveGraph(const QString& filepath) const
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_graph) {
        qWarning() << "Cannot save graph: graph is null";
        return false;
    }
    
    try {
        QJsonObject graphData = m_graph->serializeGraph();
        
        // Ensure directory exists
        QFileInfo fileInfo(filepath);
        QDir dir = fileInfo.dir();
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        
        QFile file(filepath);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Failed to open file for writing:" << filepath;
            return false;
        }
        
        QJsonDocument doc(graphData);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        
        qInfo() << "Graph saved to file:" << filepath;
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Failed to save graph:" << e.what();
        return false;
    }
}

bool TerminalGraphServer::loadGraph(const QString& filepath)
{
    QJsonObject graphData;

    // Load file and parse JSON with minimal locking
    {
        QMutexLocker locker(&m_mutex);

        try {
            QFile file(filepath);
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << "Failed to open file for reading:" << filepath;
                return false;
            }

            QByteArray data = file.readAll();
            file.close();

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(data, &error);

            if (error.error != QJsonParseError::NoError) {
                qWarning() << "Failed to parse JSON from file:" << error.errorString();
                return false;
            }

            if (!doc.isObject()) {
                qWarning() << "Invalid JSON format: root element is not an object";
                return false;
            }

            graphData = doc.object();
        } catch (const std::exception& e) {
            qWarning() << "Failed to load graph:" << e.what();
            return false;
        }
    } // Release lock before deserializing

    // Now deserialize with a separate lock
    return deserializeGraph(graphData);
}

QVariant TerminalGraphServer::processCommand(const QString& command, const QVariantMap& params)
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
        qWarning() << "Cannot process message: command processor or RabbitMQ handler is null";
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
        response["processed_timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
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
        
        response["processed_timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    
    // Emit signal for monitoring
    emit messageSending(response);
    
    // Send response
    m_rabbitMQHandler->sendResponse(response);
}

void TerminalGraphServer::sendHeartbeat()
{
    QMutexLocker locker(&m_mutex);

    if (!m_rabbitMQHandler || !m_rabbitMQHandler->isConnected()) {
        return;
    }

    // Create heartbeat message
    QJsonObject heartbeat;
    heartbeat["type"] = "heartbeat";
    heartbeat["server_id"] = m_serverId;
    heartbeat["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    heartbeat["server_status"] = "active";

    // Add some basic stats
    if (m_graph) {
        heartbeat["terminal_count"] = m_graph->getTerminalCount();

        // Get total container count across all terminals
        int totalContainers = 0;
        QVariantMap terminalStatus = m_graph->getTerminalStatus(); // Remove the .toMap() call

        for (auto it = terminalStatus.constBegin(); it != terminalStatus.constEnd(); ++it) {
            QVariantMap status = it.value().toMap();
            totalContainers += status.value("container_count", 0).toInt();
        }

        heartbeat["container_count"] = totalContainers;
    }

    // Add memory usage information
    // Note: this is a simplified approach, more accurate memory tracking would require platform-specific code
    heartbeat["memory_usage_kb"] = QCoreApplication::applicationPid(); // Just a placeholder

    // Send heartbeat message
    m_rabbitMQHandler->sendResponse(heartbeat);

    qDebug() << "Sent heartbeat message";
}

} // namespace TerminalSim
