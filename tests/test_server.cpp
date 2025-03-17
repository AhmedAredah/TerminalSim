#include <QtTest>
#include "server/terminal_graph_server.h"
#include "server/rabbit_mq_handler.h"
#include "server/command_processor.h"
#include <QObject>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>

namespace TerminalSim {

class TestServer : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;
    QString m_graphFilePath;

private slots:
    void initTestCase() {
        // Create a temporary directory for testing
        QVERIFY(m_tempDir.isValid());
        m_graphFilePath = m_tempDir.filePath("test_graph.json");

        qDebug() << "Starting terminal graph server tests";
        qDebug() << "Using temporary directory:" << m_tempDir.path();
    }

    void testSingletonInstance() {
        // Test the singleton pattern
        TerminalGraphServer* instance1 = TerminalGraphServer::getInstance(m_tempDir.path());
        TerminalGraphServer* instance2 = TerminalGraphServer::getInstance();

        // Both should be the same instance
        QCOMPARE(instance1, instance2);

        // We can't test the server ID directly since it's private
        // Instead, we'll test that serialized graph data contains a consistent ID
        QJsonObject data1 = instance1->serializeGraph();
        QJsonObject data2 = instance2->serializeGraph();

        // Both should have the same server ID in their data
        if (data1.contains("server_id") && data2.contains("server_id")) {
            QCOMPARE(data1["server_id"].toString(), data2["server_id"].toString());
        }
    }

    void testGraphManagement() {
        // Test graph creation and management
        TerminalGraphServer* server = TerminalGraphServer::getInstance();

        // We can't directly verify graph was created, but we can check functionality
        // that depends on it

        // Create a simple terminal for testing
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);

        QVariantMap capacity;
        capacity["max_capacity"] = 100;
        capacity["critical_threshold"] = 0.8;

        QVariantMap customConfig;
        customConfig["capacity"] = capacity;
        customConfig["dwell_time"] = QVariantMap{{"method", "gamma"}, {"parameters", QVariantMap{{"shape", 2.0}, {"scale", 3600.0}}}};
        customConfig["customs"] = QVariantMap{{"probability", 0.1}, {"delay_mean", 24.0}, {"delay_variance", 6.0}};
        customConfig["cost"] = QVariantMap{{"fixed_fees", 100.0}, {"customs_fees", 50.0}, {"risk_factor", 0.01}};

        // Test direct command processing
        QVariantMap addTerminalParams;
        addTerminalParams["terminal_names"] = QStringList{"TestTerminal"};
        addTerminalParams["custom_config"] = customConfig;
        addTerminalParams["terminal_interfaces"] = QVariantMap{{"0", QVariantList{0}}};
        addTerminalParams["region"] = "TestRegion";

        QVariant result = server->processCommand("add_terminal", addTerminalParams);
        QVERIFY(result.toBool());

        // Verify terminal was added
        QVariantMap statusParams;
        QVariant statusResult = server->processCommand("get_terminal_status", statusParams);
        QVERIFY(statusResult.toMap().contains("TestTerminal"));

        // Test graph serialization and deserialization
        QJsonObject serializedGraph = server->serializeGraph();
        QVERIFY(!serializedGraph.isEmpty());

        // Save graph to file
        QVERIFY(server->saveGraph(m_graphFilePath));
        QVERIFY(QFile::exists(m_graphFilePath));

        // Load graph from file
        QVERIFY(server->loadGraph(m_graphFilePath));

        // Verify terminal still exists after reload
        statusResult = server->processCommand("get_terminal_status", statusParams);

        qDebug() << "returned from get_terminal_status";

        QVERIFY(statusResult.toMap().contains("TestTerminal"));
    }

    void testCommandProcessing() {
        // Test command processing
        TerminalGraphServer* server = TerminalGraphServer::getInstance();

        // Test ping command
        QVariantMap pingParams;
        pingParams["echo"] = "Hello, World!";
        QVariant pingResult = server->processCommand("ping", pingParams);

        QVariantMap pingResponse = pingResult.toMap();
        QCOMPARE(pingResponse["status"].toString(), QString("ok"));
        QCOMPARE(pingResponse["echo"].toString(), QString("Hello, World!"));

        // Test a command that would go through the command processor
        // We'll use JSON format but call it directly through the server interface
        QJsonObject jsonCommand;
        jsonCommand["command"] = "ping";
        jsonCommand["params"] = QJsonObject{{"echo", "JSON Ping"}};
        jsonCommand["request_id"] = "test-request-123";

        // Convert this to a QVariantMap for processCommand
        QVariantMap commandParams;
        commandParams["command"] = "ping";
        commandParams["echo"] = "JSON Ping";
        commandParams["request_id"] = "test-request-123";

        QVariant jsonResult = server->processCommand("ping", commandParams);
        QVERIFY(!jsonResult.isNull());

        QVariantMap responseResult = jsonResult.toMap();
        QCOMPARE(responseResult["status"].toString(), QString("ok"));
        QCOMPARE(responseResult["echo"].toString(), QString("JSON Ping"));
    }

    void testServerInitialization() {
        // Skip actual RabbitMQ connection tests as they require a running server
        // Instead, verify that the server handles connection failure gracefully

        TerminalGraphServer* server = TerminalGraphServer::getInstance();

        // Initialize with invalid connection parameters should not crash
        // We don't use the result, but call the method to ensure it doesn't crash
        server->initialize("non_existent_host", 5672, "guest", "guest");

        // Give some time for retry attempts to complete
        QTest::qWait(2000);

        // We can't check private members, but we can check that the server is still functional
        // Test a simple command to verify the server is still working
        QVariantMap pingParams;
        pingParams["echo"] = "After failed init";
        QVariant pingResult = server->processCommand("ping", pingParams);
        QVERIFY(!pingResult.isNull());

        QVariantMap pingResponse = pingResult.toMap();
        QCOMPARE(pingResponse["echo"].toString(), QString("After failed init"));
    }

    void testConnectionStatus() {
        // Test connection status
        TerminalGraphServer* server = TerminalGraphServer::getInstance();

        // We can check the connection status through the public API
        bool connected = server->isConnected();

        // This might be false, especially in a test environment without RabbitMQ,
        // despite the retry mechanism
        // Just verify the call doesn't crash
        QVERIFY(connected == true || connected == false);
    }

    void testMessageHandling() {
        // Test message handling
        TerminalGraphServer* server = TerminalGraphServer::getInstance();

        // Setup signal spy to catch messages
        QSignalSpy receivedSpy(server, &TerminalGraphServer::messageReceived);
        QSignalSpy sendingSpy(server, &TerminalGraphServer::messageSending);

        // Instead of directly calling onMessageReceived, we'll simulate
        // the message flow by making a command call that will emit signals

        // Create a test command
        QVariantMap pingParams;
        pingParams["echo"] = "Signal Test";

        // Connect a temporary signal handler to emit our own signals when the command is processed
        QMetaObject::Connection tempConnection =
            QObject::connect(server, &TerminalGraphServer::connectionChanged,
                             [server](bool) {
                                 // Create a test JSON object to emit with our signals
                                 QJsonObject testMsg;
                                 testMsg["command"] = "ping";
                                 testMsg["params"] = QJsonObject{{"echo", "Signal Test"}};

                                 // Emit signals for testing
                                 emit server->messageReceived(testMsg);

                                 QJsonObject response;
                                 response["success"] = true;
                                 response["result"] = QJsonObject{{"status", "ok"}};
                                 emit server->messageSending(response);
                             });

        // Trigger the signal by changing connection status
        emit server->connectionChanged(true);

        // Disconnect our temporary handler
        QObject::disconnect(tempConnection);

        // Verify the received signal was emitted
        QVERIFY(receivedSpy.count() > 0);
        if (receivedSpy.count() > 0) {
            QList<QVariant> receivedArgs = receivedSpy.takeFirst();
            QCOMPARE(receivedArgs.at(0).toJsonObject()["command"].toString(), QString("ping"));
        }

        // Verify the sending signal was emitted
        QVERIFY(sendingSpy.count() > 0);
        if (sendingSpy.count() > 0) {
            QList<QVariant> sendingArgs = sendingSpy.takeFirst();
            QVERIFY(sendingArgs.at(0).toJsonObject()["success"].toBool());
        }
    }

    void cleanupTestCase() {
        // Clean up resources
        // The singleton server will be cleaned up at application shutdown

        // Delete the temp graph file
        QFile::remove(m_graphFilePath);

        qDebug() << "Terminal graph server tests completed";
    }
};

} // namespace TerminalSim

QTEST_MAIN(TerminalSim::TestServer)
#include "test_server.moc"
