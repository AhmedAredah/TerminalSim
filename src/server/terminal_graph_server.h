#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QJsonObject>
#include <QVariant>
#include <QTimer>
#include <QUuid>

#include "terminal/terminal_graph.h"
#include "server/rabbit_mq_handler.h"
#include "server/command_processor.h"

namespace TerminalSim {

/**
 * @brief Server class that manages a TerminalGraph
 *        and processes client requests
 */
class TerminalGraphServer : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance
     * @param pathToTerminalsDirectory Directory for terminal storage
     * @return Singleton instance
     */
    static TerminalGraphServer*
    getInstance(const QString& pathToTerminalsDirectory = QString());
    
    /**
     * @brief Initialize the server
     * @param rabbitMQHost RabbitMQ host address
     * @param rabbitMQPort RabbitMQ port
     * @param rabbitMQUser RabbitMQ username
     * @param rabbitMQPassword RabbitMQ password
     * @return True if initialized successfully
     */
    bool initialize(
        const QString& rabbitMQHost = "localhost",
        int rabbitMQPort = 5672,
        const QString& rabbitMQUser = "guest",
        const QString& rabbitMQPassword = "guest"
    );
    
    /**
     * @brief Shut down the server
     */
    void shutdown();
    
    /**
     * @brief Check if the server is connected
     * @return True if connected
     */
    bool isConnected() const;

    /**
     * @brief Process a command directly (for testing)
     * @param command Command to process
     * @param params Command parameters
     * @return Command result
     */
    QVariant
    processCommand(const QString& command, const QVariantMap& params);
    
signals:
    /**
     * @brief Signal emitted when a message is received
     * @param message The received message
     */
    void messageReceived(const QJsonObject& message);
    
    /**
     * @brief Signal emitted when a message is about to be sent
     * @param message The message to be sent
     */
    void messageSending(const QJsonObject& message);
    
    /**
     * @brief Signal emitted on connection status change
     * @param connected Connection status
     */
    void connectionChanged(bool connected);
    
private slots:
    /**
     * @brief Process incoming messages
     * @param message The received message
     */
    void onMessageReceived(const QJsonObject& message);
    
private:
    // Constructor is private for singleton
    explicit TerminalGraphServer(
        const QString& pathToTerminalsDirectory = QString());
    ~TerminalGraphServer();
    
    // Prevent copy
    TerminalGraphServer(const TerminalGraphServer&) = delete;
    TerminalGraphServer& operator=(const TerminalGraphServer&) = delete;
    
    // Singleton instance
    static TerminalGraphServer* s_instance;
    static QMutex s_instanceMutex;
    
    // Terminal graph
    TerminalGraph* m_graph;
    QString m_pathToTerminalsDirectory;
    
    // RabbitMQ handler
    RabbitMQHandler* m_rabbitMQHandler;
    
    // Command processor
    CommandProcessor* m_commandProcessor;
    
    // Server ID
    QString m_serverId;
    
    // Thread safety
    mutable QMutex m_mutex;
};

} // namespace TerminalSim
