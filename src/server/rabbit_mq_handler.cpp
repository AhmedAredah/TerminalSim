#include "rabbit_mq_handler.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <QUuid>
#include <chrono>
#include <thread>

// RabbitMQ-C headers
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <rabbitmq-c/framing.h>

namespace TerminalSim {

// Constants for RabbitMQ connection
static const
    int MAX_RECONNECT_ATTEMPTS = 5;
static const
    int RECONNECT_DELAY_SECONDS = 5;  // Delay between reconnection attempts
static const
    std::string GithubLink = "https://github.com/VTTI-CSM/ShipNetSim";
static const
    std::string EXCHANGE_NAME = "CargoNetSim.Exchange";
static const
    std::string COMMAND_QUEUE_NAME = "CargoNetSim.CommandQueue.TerminalSim";
static const
    std::string RESPONSE_QUEUE_NAME = "CargoNetSim.ResponseQueue.TerminalSim";
static const
    std::string RECEIVING_ROUTING_KEY = "CargoNetSim.Command.TerminalSim";
static const
    std::string PUBLISHING_ROUTING_KEY = "CargoNetSim.Response.TerminalSim";
static const
    int MAX_SEND_COMMAND_RETRIES = 3;

RabbitMQHandler::RabbitMQHandler(QObject* parent)
    : QObject(parent),
    m_connection(nullptr),
    m_connected(false),
    m_host("localhost"),
    m_port(5672),
    m_username("guest"),
    m_password("guest"),
    m_exchangeName(QString::fromStdString(EXCHANGE_NAME)),
    m_commandQueueName(QString::fromStdString(COMMAND_QUEUE_NAME)),
    m_responseQueueName(QString::fromStdString(RESPONSE_QUEUE_NAME)),
    m_commandRoutingKey(QString::fromStdString(RECEIVING_ROUTING_KEY)),
    m_responseRoutingKey(QString::fromStdString(PUBLISHING_ROUTING_KEY)),
    m_workerThread(nullptr),
    m_threadRunning(false)
{
    qDebug() << "RabbitMQ handler initialized with exchange:" << m_exchangeName
             << ", command queue:" << m_commandQueueName
             << ", response queue:" << m_responseQueueName;
}

RabbitMQHandler::~RabbitMQHandler()
{
    disconnect();

    qDebug() << "RabbitMQ handler destroyed";
}

bool RabbitMQHandler::connect(const QString& host,
                              int port,
                              const QString& username,
                              const QString& password)
{
    QMutexLocker locker(&m_mutex);

    if (m_connected) {
        qDebug() << "Already connected to RabbitMQ";
        return true;
    }

    // Store connection parameters
    m_host = host;
    m_port = port;
    m_username = username;
    m_password = password;

    // Display server information
    QString yearRange = QString("(C) %1-%2 ")
                            .arg(QDate::currentDate().year() - 1)
                            .arg(QDate::currentDate().year());

    QString appDetails;
    QTextStream detailsStream(&appDetails);
    detailsStream << "TerminalSim"
                  << " [Version 1.0]" << Qt::endl;
    detailsStream << yearRange << "VTTI-CSM" << Qt::endl;
    detailsStream << QString::fromStdString(GithubLink) << Qt::endl;

    qInfo().noquote() << appDetails;

    // Try to connect with multiple attempts
    int retryCount = 0;
    while (retryCount < MAX_RECONNECT_ATTEMPTS) {
        try {
            // Create new connection
            m_connection = amqp_new_connection();
            if (!m_connection) {
                qWarning() << "Failed to create RabbitMQ connection, "
                              "retrying...";
                retryCount++;
                std::this_thread::sleep_for(
                    std::chrono::seconds(RECONNECT_DELAY_SECONDS));
                continue;
            }

            // Create socket
            amqp_socket_t* socket = amqp_tcp_socket_new(m_connection);
            if (!socket) {
                qWarning() << "Failed to create RabbitMQ socket, retrying...";
                amqp_destroy_connection(m_connection);
                m_connection = nullptr;
                retryCount++;
                std::this_thread::sleep_for(
                    std::chrono::seconds(RECONNECT_DELAY_SECONDS));
                continue;
            }

            // Open socket
            int status = amqp_socket_open(socket,
                                          m_host.toUtf8().constData(),
                                          m_port);
            if (status != AMQP_STATUS_OK) {
                qWarning() << "Failed to open RabbitMQ socket, status:"
                           << status << ", retrying...";
                amqp_destroy_connection(m_connection);
                m_connection = nullptr;
                retryCount++;
                std::this_thread::sleep_for(
                    std::chrono::seconds(RECONNECT_DELAY_SECONDS));
                continue;
            }

            // Login
            amqp_rpc_reply_t login_reply = amqp_login(
                m_connection,
                "/", // vhost
                0,   // channel max
                131072, // frame max
                0,  // heartbeat
                AMQP_SASL_METHOD_PLAIN,
                m_username.toUtf8().constData(),
                m_password.toUtf8().constData()
                );

            if (login_reply.reply_type != AMQP_RESPONSE_NORMAL) {
                qWarning() << "Failed to login to RabbitMQ, retrying...";
                amqp_destroy_connection(m_connection);
                m_connection = nullptr;
                retryCount++;
                std::this_thread::sleep_for(
                    std::chrono::seconds(RECONNECT_DELAY_SECONDS));
                continue;
            }

            // Open channel
            amqp_channel_open(m_connection, 1);
            amqp_rpc_reply_t channel_reply = amqp_get_rpc_reply(m_connection);
            if (channel_reply.reply_type != AMQP_RESPONSE_NORMAL) {
                qWarning() << "Failed to open RabbitMQ channel, retrying...";
                amqp_destroy_connection(m_connection);
                m_connection = nullptr;
                retryCount++;
                std::this_thread::sleep_for(
                    std::chrono::seconds(RECONNECT_DELAY_SECONDS));
                continue;
            }

            // Setup exchange and queues
            if (!setupExchange() || !setupQueues() || !bindQueues()) {
                disconnect();
                retryCount++;
                std::this_thread::sleep_for(
                    std::chrono::seconds(RECONNECT_DELAY_SECONDS));
                continue;
            }

            m_connected = true;
            emit connectionChanged(true);

            // Start worker thread for consuming messages
            m_threadRunning = true;
            m_workerThread = new QThread();
            this->moveToThread(m_workerThread);

            QObject::connect(m_workerThread, &QThread::started,
                             this, &RabbitMQHandler::workerThreadFunction);
            QObject::connect(m_workerThread, &QThread::finished,
                             this, [this]() {
                                 m_threadRunning = false;
                             });

            m_workerThread->start();

            qInfo() << "Successfully connected to RabbitMQ server at"
                    << m_host << ":" << m_port;
            return true;

        } catch (const std::exception& e) {
            qWarning() << "Exception during RabbitMQ connection:"
                       << e.what() << ", retrying...";

            if (m_connection) {
                amqp_destroy_connection(m_connection);
                m_connection = nullptr;
            }

            m_connected = false;
            retryCount++;
            std::this_thread::sleep_for(
                std::chrono::seconds(RECONNECT_DELAY_SECONDS));
        }
    }

    qCritical() << "Failed to establish a connection to RabbitMQ after"
                << MAX_RECONNECT_ATTEMPTS << "attempts.";
    return false;
}

void RabbitMQHandler::disconnect()
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected) {
        return;
    }

    m_threadRunning = false;

    // Stop worker thread
    if (m_workerThread) {
        if (m_workerThread->isRunning()) {
            m_workerThread->quit();
            m_workerThread->wait(2000); // Wait up to 2 seconds

            if (m_workerThread->isRunning()) {
                m_workerThread->terminate();
            }
        }

        delete m_workerThread;
        m_workerThread = nullptr;
    }

    // Disconnect from RabbitMQ
    if (m_connection) {
        // Close channel
        amqp_channel_close(m_connection, 1, AMQP_REPLY_SUCCESS);

        // Close connection
        amqp_connection_close(m_connection, AMQP_REPLY_SUCCESS);

        // Destroy connection
        amqp_destroy_connection(m_connection);
        m_connection = nullptr;
    }

    m_connected = false;
    emit connectionChanged(false);

    qInfo() << "Disconnected from RabbitMQ server";
}

bool RabbitMQHandler::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connected && m_connection != nullptr;
}

bool RabbitMQHandler::sendResponse(const QJsonObject& message)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || !m_connection) {
        qWarning() << "Cannot send response: not connected to RabbitMQ server";
        return false;
    }

    QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);

    int retries = MAX_SEND_COMMAND_RETRIES;
    while (retries > 0) {
        try {
            // Properties
            amqp_basic_properties_t props;
            props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
                           AMQP_BASIC_DELIVERY_MODE_FLAG |
                           AMQP_BASIC_MESSAGE_ID_FLAG;
            props.content_type = amqp_cstring_bytes("application/json");
            props.delivery_mode = 2; // persistent delivery mode

            // Generate message ID if not present
            QString messageId = message.contains("message_id") ?
                                    message["message_id"].toString() :
                                    QUuid::createUuid().toString();

            QByteArray messageIdBytes = messageId.toUtf8();
            props.message_id =
                amqp_bytes_malloc_dup(
                    amqp_cstring_bytes(messageIdBytes.constData()));

            // Publish message
            int status = amqp_basic_publish(
                m_connection,
                1, // channel
                amqp_cstring_bytes(m_exchangeName.toUtf8().constData()),
                amqp_cstring_bytes(m_responseRoutingKey.toUtf8().constData()),
                0, // mandatory
                0, // immediate
                &props,
                amqp_bytes_malloc_dup(amqp_cstring_bytes(data.constData()))
                );

            // Free allocated memory
            amqp_bytes_free(props.message_id);

            if (status != AMQP_STATUS_OK) {
                qWarning() << "Failed to publish message, status:"
                           << status << ", retrying...";
                retries--;
                QThread::msleep(1000); // Wait 1 second before retrying
                continue;
            }

            qDebug() << "Published response to"
                     << m_responseRoutingKey
                     << "with size"
                     << data.size() << "bytes";
            return true;
        } catch (const std::exception& e) {
            qWarning() << "Exception during RabbitMQ publish:"
                       << e.what() << ", retrying...";
            retries--;
            QThread::msleep(1000); // Wait 1 second before retrying
        }
    }

    qCritical() << "Failed to publish message to RabbitMQ after"
                << MAX_SEND_COMMAND_RETRIES << "attempts.";
    return false;
}

bool RabbitMQHandler::setupExchange()
{
    try {
        // Declare exchange
        amqp_exchange_declare(
            m_connection,
            1, // channel
            amqp_cstring_bytes(m_exchangeName.toUtf8().constData()),
            amqp_cstring_bytes("topic"),
            0, // passive (false)
            1, // durable (true)
            0, // auto delete (false)
            0, // internal (false)
            amqp_empty_table
            );

        amqp_rpc_reply_t reply = amqp_get_rpc_reply(m_connection);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            qWarning() << "Failed to declare exchange";
            return false;
        }

        qDebug() << "Exchange declared:" << m_exchangeName;
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Exception during exchange setup:" << e.what();
        return false;
    }
}

bool RabbitMQHandler::setupQueues()
{
    try {
        // Declare command queue
        amqp_queue_declare(
            m_connection,
            1, // channel
            amqp_cstring_bytes(m_commandQueueName.toUtf8().constData()),
            0, // passive (false)
            1, // durable (true)
            0, // exclusive (false)
            0, // auto delete (false)
            amqp_empty_table
            );

        amqp_rpc_reply_t reply = amqp_get_rpc_reply(m_connection);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            qWarning() << "Failed to declare command queue";
            return false;
        }

        // Declare response queue
        amqp_queue_declare(
            m_connection,
            1, // channel
            amqp_cstring_bytes(m_responseQueueName.toUtf8().constData()),
            0, // passive (false)
            1, // durable (true)
            0, // exclusive (false)
            0, // auto delete (false)
            amqp_empty_table
            );

        reply = amqp_get_rpc_reply(m_connection);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            qWarning() << "Failed to declare response queue";
            return false;
        }

        qDebug() << "Queues declared:"
                 << m_commandQueueName
                 << "and"
                 << m_responseQueueName;
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Exception during queue setup:" << e.what();
        return false;
    }
}

bool RabbitMQHandler::bindQueues()
{
    try {
        // Bind command queue
        amqp_queue_bind(
            m_connection,
            1, // channel
            amqp_cstring_bytes(m_commandQueueName.toUtf8().constData()),
            amqp_cstring_bytes(m_exchangeName.toUtf8().constData()),
            amqp_cstring_bytes(m_commandRoutingKey.toUtf8().constData()),
            amqp_empty_table
            );

        amqp_rpc_reply_t reply = amqp_get_rpc_reply(m_connection);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            qWarning() << "Failed to bind command queue";
            return false;
        }

        // Bind response queue
        amqp_queue_bind(
            m_connection,
            1, // channel
            amqp_cstring_bytes(m_responseQueueName.toUtf8().constData()),
            amqp_cstring_bytes(m_exchangeName.toUtf8().constData()),
            amqp_cstring_bytes(m_responseRoutingKey.toUtf8().constData()),
            amqp_empty_table
            );

        reply = amqp_get_rpc_reply(m_connection);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            qWarning() << "Failed to bind response queue";
            return false;
        }

        qDebug() << "Queues bound to exchange with routing keys:"
                 << m_commandRoutingKey << "and" << m_responseRoutingKey;
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Exception during queue binding:" << e.what();
        return false;
    }
}

void RabbitMQHandler::startConsuming()
{
    try {
        // Start consuming from command queue
        amqp_basic_consume(
            m_connection,
            1, // channel
            amqp_cstring_bytes(m_commandQueueName.toUtf8().constData()),
            amqp_empty_bytes, // consumer tag (server-generated)
            0, // no local
            1, // no ack - auto acknowledge
            0, // exclusive
            amqp_empty_table
            );

        amqp_rpc_reply_t reply = amqp_get_rpc_reply(m_connection);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
            qWarning() << "Failed to start consuming from command queue";
            return;
        }

        qDebug() << "Started consuming from command queue:"
                 << m_commandQueueName;
    } catch (const std::exception& e) {
        qWarning() << "Exception during start consuming:" << e.what();
    }
}

void RabbitMQHandler::processReceivedMessages()
{
    try {
        // Set timeout for receiving messages
        struct timeval timeout;
        timeout.tv_sec = 1; // 1 second timeout
        timeout.tv_usec = 0;

        // Receive message
        amqp_envelope_t envelope;
        amqp_rpc_reply_t result =
            amqp_consume_message(m_connection,
                                 &envelope,
                                 &timeout,
                                 0);

        // Check if there's a message
        if (result.reply_type == AMQP_RESPONSE_NORMAL) {
            // Process the message
            if (envelope.message.body.len > 0) {
                // Convert to string
                QByteArray messageData(
                    static_cast<char*>(envelope.message.body.bytes),
                    envelope.message.body.len);

                // Parse JSON
                QJsonParseError error;
                QJsonDocument doc =
                    QJsonDocument::fromJson(messageData, &error);

                if (error.error == QJsonParseError::NoError &&
                    doc.isObject()) {
                    QJsonObject message = doc.object();

                    // Add message ID if available
                    if (envelope.message.properties._flags &
                        AMQP_BASIC_MESSAGE_ID_FLAG) {
                        QByteArray messageId(
                            static_cast<char*>(
                                envelope.message.properties.message_id.bytes),
                            envelope.message.properties.message_id.len);
                        message["message_id"] = QString::fromUtf8(messageId);
                    }

                    // Queue the message for processing
                    QMutexLocker commandQueueLocker(&m_commandQueueMutex);
                    m_commandQueue.enqueue(message);
                    m_commandQueueCondition.wakeOne();

                    qDebug() << "Received message with routing key:"
                             << QByteArray(
                                    static_cast<char*>(
                                        envelope.routing_key.bytes),
                                    envelope.routing_key.len);
                }
            }

            // Release the envelope
            amqp_destroy_envelope(&envelope);
        } else if (result.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
                   result.library_error == AMQP_STATUS_TIMEOUT) {
            // Timeout - no message available, which is normal
        } else {
            // Other error
            qWarning() << "Error receiving message, reply type:"
                       << result.reply_type;

            // If connection is lost, try to reconnect
            if (result.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
                result.library_error == AMQP_STATUS_CONNECTION_CLOSED) {

                // Disconnect and try to reconnect
                disconnect();

                // Wait a bit before reconnecting
                QThread::sleep(5);

                // Try to reconnect
                connect(m_host, m_port, m_username, m_password);
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception during message processing:" << e.what();
    }
}

void RabbitMQHandler::workerThreadFunction()
{
    try {
        // Start consuming messages
        startConsuming();

        // Process messages in a loop
        while (m_threadRunning) {
            processReceivedMessages();

            // Process queued messages
            QMutexLocker commandQueueLocker(&m_commandQueueMutex);
            while (!m_commandQueue.isEmpty()) {
                QJsonObject message = m_commandQueue.dequeue();
                commandQueueLocker.unlock();

                // Emit the message
                emit commandReceived(message);

                commandQueueLocker.relock();
            }

            // Wait for more messages or timeout
            m_commandQueueCondition.wait(&m_commandQueueMutex,
                                         100); // 100ms timeout
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception in worker thread:" << e.what();
    }

    qDebug() << "Worker thread terminating";
}

} // namespace TerminalSim
