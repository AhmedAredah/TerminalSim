// terminal_graph_server.cpp
#include "terminal_graph_server.h"

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
#include <atomic>
#include <stdexcept>
#include <sys/time.h>

#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>

#include "common/LogCategories.h"

namespace TerminalSim {

namespace
{

static const int MAX_RECONNECT_ATTEMPTS = 5;
static const int RECONNECT_DELAY_SECONDS = 5;
static const std::string EXCHANGE_NAME = "CargoNetSim.Exchange";
static const std::string HEALTH_COMMAND_QUEUE_NAME =
    "CargoNetSim.CommandQueue.TerminalSim.Health";
static const std::string HEALTH_RESPONSE_QUEUE_NAME =
    "CargoNetSim.ResponseQueue.TerminalSim.Health";
static const std::string HEALTH_RECEIVING_ROUTING_KEY =
    "CargoNetSim.Command.Health.TerminalSim";
static const std::string HEALTH_PUBLISHING_ROUTING_KEY =
    "CargoNetSim.Response.Health.TerminalSim";

} // namespace

class TerminalSimHealthControlPlane final : public QThread
{
public:
    TerminalSimHealthControlPlane(const std::string &hostname,
                                  int                port,
                                  const QString     &username,
                                  const QString     &password,
                                  const QString     &serverId,
                                  QObject           *parent = nullptr)
        : QThread(parent)
        , mHostname(hostname)
        , mPort(port)
        , mUsername(username)
        , mPassword(password)
        , mServerId(serverId)
    {
    }

    ~TerminalSimHealthControlPlane() override
    {
        stopAndWait();
    }

    void stopAndWait()
    {
        mStopRequested.store(true);
        requestInterruption();
        if (isRunning())
            wait();
        cleanupConnection();
    }

protected:
    void run() override
    {
        while (!mStopRequested.load() && !isInterruptionRequested())
        {
            if (!connectIfNeeded())
            {
                if (mStopRequested.load() || isInterruptionRequested())
                    break;

                QThread::sleep(RECONNECT_DELAY_SECONDS);
                continue;
            }

            qInfo() << "TerminalSim health control plane initialized."
                    << "Awaiting health commands from"
                    << mHostname.c_str() << ":" << mPort;

            consumeLoop();
            cleanupConnection();

            if (!mStopRequested.load() && !isInterruptionRequested())
            {
                qDebug() << "Attempting to reconnect TerminalSim health lane...";
                QThread::sleep(RECONNECT_DELAY_SECONDS);
            }
        }
    }

private:
    bool connectIfNeeded()
    {
        cleanupConnection();

        int attempt = 0;
        while (attempt < MAX_RECONNECT_ATTEMPTS
               && !mStopRequested.load()
               && !isInterruptionRequested())
        {
            mConnection = amqp_new_connection();
            amqp_socket_t *socket = amqp_tcp_socket_new(mConnection);
            if (!socket)
            {
                qCritical() << "Error: Unable to create RabbitMQ health socket.";
                cleanupConnection();
            }
            else
            {
                const int status =
                    amqp_socket_open(socket, mHostname.c_str(), mPort);
                if (status == AMQP_STATUS_OK)
                {
                    const amqp_rpc_reply_t loginRes =
                        amqp_login(mConnection, "/", 0, 131072, 0,
                                   AMQP_SASL_METHOD_PLAIN,
                                   mUsername.toStdString().c_str(),
                                   mPassword.toStdString().c_str());
                    if (loginRes.reply_type == AMQP_RESPONSE_NORMAL)
                    {
                        amqp_channel_open(mConnection, 1);
                        if (amqp_get_rpc_reply(mConnection).reply_type
                            == AMQP_RESPONSE_NORMAL
                            && declareHealthTopology()
                            && startConsumingHealthQueue())
                        {
                            return true;
                        }
                    }
                    else
                    {
                        qCritical() << "Error: RabbitMQ health login failed.";
                    }
                }
                else
                {
                    qCritical() << "Error: Failed to open RabbitMQ health socket on"
                                << mHostname.c_str() << ":" << mPort;
                }
                cleanupConnection();
            }

            ++attempt;
            if (attempt < MAX_RECONNECT_ATTEMPTS)
                QThread::sleep(RECONNECT_DELAY_SECONDS);
        }

        return false;
    }

    bool declareHealthTopology()
    {
        amqp_exchange_declare(
            mConnection, 1,
            amqp_cstring_bytes(EXCHANGE_NAME.c_str()),
            amqp_cstring_bytes("topic"), 0, 1, 0, 0,
            amqp_empty_table);
        if (amqp_get_rpc_reply(mConnection).reply_type
            != AMQP_RESPONSE_NORMAL)
        {
            qCritical() << "Error: Unable to declare RabbitMQ health exchange.";
            return false;
        }

        amqp_queue_declare(
            mConnection, 1,
            amqp_cstring_bytes(HEALTH_COMMAND_QUEUE_NAME.c_str()),
            0, 1, 0, 0, amqp_empty_table);
        if (amqp_get_rpc_reply(mConnection).reply_type
            != AMQP_RESPONSE_NORMAL)
        {
            qCritical() << "Error: Unable to declare RabbitMQ health command queue.";
            return false;
        }

        amqp_queue_bind(
            mConnection, 1,
            amqp_cstring_bytes(HEALTH_COMMAND_QUEUE_NAME.c_str()),
            amqp_cstring_bytes(EXCHANGE_NAME.c_str()),
            amqp_cstring_bytes(HEALTH_RECEIVING_ROUTING_KEY.c_str()),
            amqp_empty_table);
        if (amqp_get_rpc_reply(mConnection).reply_type
            != AMQP_RESPONSE_NORMAL)
        {
            qCritical() << "Error: Unable to bind RabbitMQ health command queue.";
            return false;
        }

        return true;
    }

    bool startConsumingHealthQueue()
    {
        amqp_basic_consume(
            mConnection, 1,
            amqp_cstring_bytes(HEALTH_COMMAND_QUEUE_NAME.c_str()),
            amqp_empty_bytes, 0, 0, 0, amqp_empty_table);
        if (amqp_get_rpc_reply(mConnection).reply_type
            != AMQP_RESPONSE_NORMAL)
        {
            qCritical() << "Error: Failed to start consuming from the TerminalSim"
                           " health queue.";
            return false;
        }

        return true;
    }

    void consumeLoop()
    {
        while (!mStopRequested.load() && !isInterruptionRequested())
        {
            if (!mConnection)
                return;

            amqp_maybe_release_buffers(mConnection);
            amqp_envelope_t envelope;
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;

            const amqp_rpc_reply_t res =
                amqp_consume_message(mConnection, &envelope, &timeout, 0);
            if (res.reply_type == AMQP_RESPONSE_NORMAL)
            {
                amqp_basic_ack(mConnection, 1, envelope.delivery_tag, 0);

                const QByteArray messageData(
                    static_cast<char *>(envelope.message.body.bytes),
                    envelope.message.body.len);
                const QJsonDocument doc =
                    QJsonDocument::fromJson(messageData);
                processHealthCommand(doc.object());
                amqp_destroy_envelope(&envelope);
            }
            else if (res.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION
                     && res.library_error == AMQP_STATUS_TIMEOUT)
            {
                QThread::msleep(50);
            }
            else
            {
                qCritical() << "Error receiving message from TerminalSim health lane."
                            << "Type:" << res.reply_type;
                return;
            }
        }
    }

    void processHealthCommand(const QJsonObject &jsonMessage)
    {
        const QString command =
            jsonMessage.value("command").toString();

        QJsonObject response;
        populateSharedResponseFields(response, jsonMessage);

        if (command == "ping")
        {
            response["event"] = "pingResponse";
            response["success"] = true;

            QJsonObject result;
            result["status"] = "ok";
            result["timestamp"] =
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            const QJsonObject params =
                jsonMessage.value("params").toObject();
            if (params.contains("echo"))
                result["echo"] = params.value("echo");

            response["result"] = result;
        }
        else if (command == "checkConnection")
        {
            response["event"] = "connectionStatus";
            response["host"] = "TerminalSim";
            response["status"] = "connected";
            response["success"] = true;
        }
        else if (command.isEmpty())
        {
            response["event"] = "connectionStatus";
            response["status"] = "invalid";
            response["success"] = false;
            response["error"] =
                "Missing 'command' field in the health message";
        }
        else
        {
            response["event"] =
                command == "ping" ? "pingResponse" : "connectionStatus";
            response["status"] = "unsupported";
            response["success"] = false;
            response["error"] =
                "Unsupported health command: " + command;
        }

        publish(response);
    }

    void populateSharedResponseFields(QJsonObject &response,
                                      const QJsonObject &request) const
    {
        if (request.contains("request_id"))
            response["request_id"] = request.value("request_id");
        if (request.contains("commandId"))
            response["commandId"] = request.value("commandId");
        if (request.contains("message_id"))
            response["message_id"] = request.value("message_id");
        if (request.contains("params"))
            response["params"] = request.value("params");
        if (request.contains("replyRoutingKey"))
            response["replyRoutingKey"] =
                request.value("replyRoutingKey");

        response["server_id"] = mServerId;
        response["processed_timestamp"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        response["timestamp"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }

    void publish(const QJsonObject &message)
    {
        if (!mConnection)
        {
            qCritical() << "Cannot publish RabbitMQ health message;"
                           " connection is null.";
            return;
        }

        QByteArray messageData = QJsonDocument(message).toJson();
        amqp_bytes_t messageBytes;
        messageBytes.len = static_cast<size_t>(messageData.size());
        messageBytes.bytes = messageData.data();

        amqp_basic_properties_t props;
        props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG
                       | AMQP_BASIC_DELIVERY_MODE_FLAG;
        props.content_type = amqp_cstring_bytes("application/json");
        props.delivery_mode = 2;

        const int publishStatus = amqp_basic_publish(
            mConnection, 1,
            amqp_cstring_bytes(EXCHANGE_NAME.c_str()),
            amqp_cstring_bytes(
                message.value("replyRoutingKey")
                    .toString(QString::fromStdString(
                        HEALTH_PUBLISHING_ROUTING_KEY))
                    .toUtf8()
                    .constData()),
            0, 0, &props, messageBytes);
        if (publishStatus != AMQP_STATUS_OK)
        {
            qCritical() << "Failed to publish TerminalSim health message.";
        }
    }

    void cleanupConnection()
    {
        if (!mConnection)
            return;

        amqp_channel_close(mConnection, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(mConnection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(mConnection);
        mConnection = nullptr;
    }

    std::string             mHostname;
    int                     mPort = 0;
    QString                 mUsername;
    QString                 mPassword;
    QString                 mServerId;
    std::atomic_bool        mStopRequested{false};
    amqp_connection_state_t mConnection = nullptr;
};

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
    m_healthControlPlane(nullptr),
    m_commandProcessor(nullptr),
    m_serverId(QUuid::createUuid().toString())
{
    qCDebug(lcServer) << "Terminal Graph Server created with ID:" << m_serverId
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

    if (m_healthControlPlane) {
        m_healthControlPlane->stopAndWait();
        delete m_healthControlPlane;
        m_healthControlPlane = nullptr;
    }
    
    // Delete graph
    delete m_graph;
    m_graph = nullptr;
    
    qCDebug(lcServer) << "Terminal Graph Server destroyed";
    
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

    if (m_healthControlPlane)
    {
        m_healthControlPlane->stopAndWait();
        delete m_healthControlPlane;
        m_healthControlPlane = nullptr;
    }

    m_healthControlPlane = new TerminalSimHealthControlPlane(
        rabbitMQHost.toStdString(), rabbitMQPort, rabbitMQUser,
        rabbitMQPassword, m_serverId, this);
    m_healthControlPlane->start();
    
    // Connect to RabbitMQ
    bool connected = m_rabbitMQHandler->connect(
        rabbitMQHost, 
        rabbitMQPort, 
        rabbitMQUser, 
        rabbitMQPassword
        );
    
    if (connected) {
        
        qCDebug(lcServer) << "Terminal Graph Server initialized "
                            "and connected to RabbitMQ at"
                         << rabbitMQHost << ":" << rabbitMQPort;
    } else {
        qCWarning(lcServer) << "Failed to connect to RabbitMQ at"
                            << rabbitMQHost << ":" << rabbitMQPort;
    }
    
    return connected;
}

void TerminalGraphServer::shutdown()
{
    QMutexLocker locker(&m_mutex);
    
    qCDebug(lcServer) << "Shutting down Terminal Graph Server...";
    
    // Disconnect from RabbitMQ
    if (m_rabbitMQHandler) {
        m_rabbitMQHandler->disconnect();
    }

    if (m_healthControlPlane)
    {
        m_healthControlPlane->stopAndWait();
        delete m_healthControlPlane;
        m_healthControlPlane = nullptr;
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
        qCWarning(lcServer) << "Cannot process command: command processor is null";
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
        qCWarning(lcServer) << "Cannot process message: command processor or "
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
        if (message.contains("replyRoutingKey")) {
            response["replyRoutingKey"] = message["replyRoutingKey"];
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
        if (message.contains("replyRoutingKey")) {
            response["replyRoutingKey"] = message["replyRoutingKey"];
        }

        response["processed_timestamp"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    
    // Emit signal for monitoring
    emit messageSending(response);
    
    // Send response
    m_rabbitMQHandler->sendResponse(
        response,
        message.value("replyRoutingKey").toString());
}

} // namespace TerminalSim
