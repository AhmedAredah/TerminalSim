#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <rabbitmq-c/amqp.h>

namespace TerminalSim {

/**
 * @brief Handles RabbitMQ communication for the terminal graph server
 */
class RabbitMQHandler : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a RabbitMQ handler
     * @param parent Parent object
     */
    explicit RabbitMQHandler(QObject* parent = nullptr);
    ~RabbitMQHandler();

    /**
     * @brief Connect to RabbitMQ server
     * @param host RabbitMQ host
     * @param port RabbitMQ port
     * @param username RabbitMQ username
     * @param password RabbitMQ password
     * @return True if connected successfully
     */
    bool connect(
        const QString& host = "localhost",
        int port = 5672,
        const QString& username = "guest",
        const QString& password = "guest"
        );

    /**
     * @brief Disconnect from RabbitMQ server
     */
    void disconnect();

    /**
     * @brief Check if connected to RabbitMQ server
     * @return True if connected
     */
    bool isConnected() const;

    /**
     * @brief Send a message to the response queue
     * @param message Message to send
     * @return True if sent successfully
     */
    bool sendResponse(const QJsonObject& message);

signals:
    /**
     * @brief Signal emitted when a command is received
     * @param command Command message
     */
    void commandReceived(const QJsonObject& command);

    /**
     * @brief Signal emitted when connection status changes
     * @param connected Connection status
     */
    void connectionChanged(bool connected);

private:
    // RabbitMQ connection state and settings
    amqp_connection_state_t m_connection;
    bool m_connected;

    // Connection parameters
    QString m_host;
    int m_port;
    QString m_username;
    QString m_password;

    // Exchange and queue names
    QString m_exchangeName;
    QString m_commandQueueName;
    QString m_responseQueueName;
    QString m_commandRoutingKey;
    QString m_responseRoutingKey;

    // Thread for asynchronous communication
    QThread* m_workerThread;
    std::atomic<bool> m_threadRunning;

    // Command queue
    QQueue<QJsonObject> m_commandQueue;
    QMutex m_commandQueueMutex;
    QWaitCondition m_commandQueueCondition;

    // Private methods for RabbitMQ operations
    bool setupExchange();
    bool setupQueues();
    bool bindQueues();
    void startConsuming();
    void processReceivedMessages();

    // Thread safety
    mutable QMutex m_mutex;

    // Worker thread method
    void workerThreadFunction();
};

} // namespace TerminalSim
