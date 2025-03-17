#include <QtTest>
#include "terminal/terminal_graph.h"
#include "terminal/terminal.h"
#include "common/common.h"
#include <QObject>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace TerminalSim {

class TestTerminalGraph : public QObject
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

        qDebug() << "Starting terminal graph tests";
        qDebug() << "Using temporary directory:" << m_tempDir.path();
    }

    void testGraphCreation() {
        // Test creating a terminal graph
        TerminalGraph graph(m_tempDir.path());

        // Verify initial state
        QCOMPARE(graph.getTerminalCount(), 0);
        QVERIFY(graph.getAllTerminalNames().isEmpty());
    }

    void testTerminalManagement() {
        // Test terminal management in the graph
        TerminalGraph graph(m_tempDir.path());
        qDebug() << "Created graph instance";

        // Create a terminal interface configuration
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
        interfaces[TerminalInterface::SEA_SIDE].insert(TransportationMode::Ship);
        qDebug() << "Created interfaces configuration";

        // Create capacity configuration
        QVariantMap capacity;
        capacity["max_capacity"] = 1000;
        capacity["critical_threshold"] = 0.8;

        // Create dwell time configuration
        QVariantMap dwellTimeParams;
        dwellTimeParams["shape"] = 2.0;
        dwellTimeParams["scale"] = 24.0 * 3600.0;

        QVariantMap dwellTime;
        dwellTime["method"] = "gamma";
        dwellTime["parameters"] = dwellTimeParams;

        // Create customs configuration
        QVariantMap customs;
        customs["probability"] = 0.1;
        customs["delay_mean"] = 24.0;
        customs["delay_variance"] = 6.0;

        // Create cost configuration
        QVariantMap cost;
        cost["fixed_fees"] = 100.0;
        cost["customs_fees"] = 50.0;
        cost["risk_factor"] = 0.01;

        // Combine all configurations
        QVariantMap customConfig;
        customConfig["capacity"] = capacity;
        customConfig["dwell_time"] = dwellTime;
        customConfig["customs"] = customs;
        customConfig["cost"] = cost;
        qDebug() << "Created terminal configuration";

        // Add a terminal with aliases
        QStringList terminalNames = {"Terminal1", "T1", "Port1"};
        graph.addTerminal(terminalNames, customConfig, interfaces, "Region1");
        qDebug() << "Added Terminal1 with aliases";

        // Verify terminal was added
        QCOMPARE(graph.getTerminalCount(), 1);
        QVERIFY(graph.terminalExists("Terminal1"));
        QVERIFY(graph.terminalExists("T1"));
        QVERIFY(graph.terminalExists("Port1"));
        qDebug() << "Verified Terminal1 and aliases exist";

        // Verify aliases
        QStringList aliases = graph.getAliasesOfTerminal("Terminal1");
        QCOMPARE(aliases.size(), 3);
        QVERIFY(aliases.contains("Terminal1"));
        QVERIFY(aliases.contains("T1"));
        QVERIFY(aliases.contains("Port1"));
        qDebug() << "Verified Terminal1 aliases list";

        // Add another terminal
        graph.addTerminal(QStringList{"Terminal2"}, customConfig, interfaces, "Region2");
        qDebug() << "Added Terminal2";
        QCOMPARE(graph.getTerminalCount(), 2);

        // Add alias to existing terminal
        graph.addAliasToTerminal("Terminal2", "T2");
        qDebug() << "Added T2 alias to Terminal2";
        QVERIFY(graph.terminalExists("T2"));

        // Get terminal status - This might be the problem area
        qDebug() << "About to get terminal status";
        QVariantMap allStatus = graph.getTerminalStatus();
        qDebug() << "Got all terminal status";

        QCOMPARE(allStatus.size(), 2);
        QVERIFY(allStatus.contains("Terminal1"));
        QVERIFY(allStatus.contains("Terminal2"));
        qDebug() << "Verified terminal status contains both terminals";

        QVariantMap terminal1Status = graph.getTerminalStatus("Terminal1");
        qDebug() << "Got Terminal1 status";
        QCOMPARE(terminal1Status["container_count"].toInt(), 0);
        QCOMPARE(terminal1Status["max_capacity"].toInt(), 1000);
        qDebug() << "Verified Terminal1 status values";

        // Remove terminal - This might be the problem area
        qDebug() << "About to remove Terminal1 via T1 alias";
        bool removeResult = graph.removeTerminal("T1");
        qDebug() << "Remove terminal result:" << removeResult;
        QVERIFY(removeResult);

        qDebug() << "About to check terminal count after removal";
        int terminalCount = graph.getTerminalCount();
        qDebug() << "Terminal count after removal:" << terminalCount;
        QCOMPARE(terminalCount, 1);

        qDebug() << "Verifying terminals no longer exist";
        QVERIFY(!graph.terminalExists("Terminal1"));
        QVERIFY(!graph.terminalExists("T1"));
        QVERIFY(!graph.terminalExists("Port1"));
        qDebug() << "Verified terminals no longer exist";

        // Clear graph - This might be the problem area
        qDebug() << "About to clear graph";
        graph.clear();
        qDebug() << "Graph cleared";

        qDebug() << "Getting terminal count after clear";
        QCOMPARE(graph.getTerminalCount(), 0);
        qDebug() << "Verified terminal count is 0 after clear";
    }

    void testRouteManagement() {
        // Test route management in the graph
        TerminalGraph graph(m_tempDir.path());

        // Create terminals
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);
        interfaces[TerminalInterface::RAIL_SIDE].insert(TransportationMode::Train);

        QVariantMap customConfig;
        customConfig["capacity"] = QVariantMap{{"max_capacity", 1000}};

        graph.addTerminal(QStringList{"TerminalA"}, customConfig, interfaces, "RegionA");
        graph.addTerminal(QStringList{"TerminalB"}, customConfig, interfaces, "RegionA");
        graph.addTerminal(QStringList{"TerminalC"}, customConfig, interfaces, "RegionB");

        // Add routes
        QVariantMap routeAttributes;
        routeAttributes["distance"] = 100.0;
        routeAttributes["travellTime"] = 2.0;
        routeAttributes["cost"] = 50.0;

        graph.addRoute("Route1", "TerminalA", "TerminalB", TransportationMode::Truck, routeAttributes);

        // Verify route exists
        QVariantMap edge = graph.getEdgeByMode("TerminalA", "TerminalB", TransportationMode::Truck);
        QVERIFY(!edge.isEmpty());
        QCOMPARE(edge["distance"].toDouble(), 100.0);

        // Test changing route weight
        QVariantMap newAttributes;
        newAttributes["distance"] = 120.0;
        newAttributes["cost"] = 60.0;

        graph.changeRouteWeight("TerminalA", "TerminalB", TransportationMode::Truck, newAttributes);

        edge = graph.getEdgeByMode("TerminalA", "TerminalB", TransportationMode::Truck);
        QCOMPARE(edge["distance"].toDouble(), 120.0);
        QCOMPARE(edge["cost"].toDouble(), 60.0);

        // Test auto-connection methods
        graph.connectTerminalsByInterfaceModes();

        // Should create a Train connection between A and B
        edge = graph.getEdgeByMode("TerminalA", "TerminalB", TransportationMode::Train);
        QVERIFY(!edge.isEmpty());

        // Test region-based connections
        graph.connectTerminalsInRegionByMode("RegionA");

        // Test cross-region connections
        graph.connectRegionsByMode(TransportationMode::Truck);

        // Verify cross-region connection
        edge = graph.getEdgeByMode("TerminalA", "TerminalC", TransportationMode::Truck);
        QVERIFY(!edge.isEmpty());

        // Get routes between regions
        QList<QVariantMap> routes = graph.getRoutesBetweenRegions("RegionA", "RegionB");
        QVERIFY(!routes.isEmpty());

    }

    void testPathFinding() {
        // Test path finding in the graph
        TerminalGraph graph(m_tempDir.path());

        // Create a simple terminal network
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);

        QVariantMap customConfig;
        customConfig["capacity"] = QVariantMap{{"max_capacity", 1000}};

        graph.addTerminal(QStringList{"A"}, customConfig, interfaces, "Region1");
        graph.addTerminal(QStringList{"B"}, customConfig, interfaces, "Region1");
        graph.addTerminal(QStringList{"C"}, customConfig, interfaces, "Region1");
        graph.addTerminal(QStringList{"D"}, customConfig, interfaces, "Region2");

        // Add routes with different costs
        QVariantMap routeAB;
        routeAB["distance"] = 100.0;
        routeAB["cost"] = 50.0;
        graph.addRoute("AB", "A", "B", TransportationMode::Truck, routeAB);

        QVariantMap routeBC;
        routeBC["distance"] = 150.0;
        routeBC["cost"] = 75.0;
        graph.addRoute("BC", "B", "C", TransportationMode::Truck, routeBC);

        QVariantMap routeAC;
        routeAC["distance"] = 300.0;
        routeAC["cost"] = 200.0;
        graph.addRoute("AC", "A", "C", TransportationMode::Truck, routeAC);

        QVariantMap routeCD;
        routeCD["distance"] = 200.0;
        routeCD["cost"] = 100.0;
        graph.addRoute("CD", "C", "D", TransportationMode::Truck, routeCD);

        // Find shortest path
        QList<TerminalGraph::PathSegment> path = graph.findShortestPath("A", "C", TransportationMode::Truck);

        // Should go A -> B -> C as it's cheaper than A -> C direct
        QCOMPARE(path.size(), 2);
        QCOMPARE(path[0].from, QString("A"));
        QCOMPARE(path[0].to, QString("B"));
        QCOMPARE(path[1].from, QString("B"));
        QCOMPARE(path[1].to, QString("C"));

        // Find path within specific regions
        QList<TerminalGraph::PathSegment> regionPath = graph.findShortestPathWithinRegions(
            "A", "C", QStringList{"Region1"}, TransportationMode::Truck);

        // Should be the same path as before since all nodes are in Region1
        QCOMPARE(regionPath.size(), 2);

        // Find top N paths
        QList<TerminalGraph::Path> topPaths = graph.findTopNShortestPaths("A", "C", 2, TransportationMode::Truck);

        // Should have 2 paths: A->B->C and A->C
        QCOMPARE(topPaths.size(), 2);

        // First path should be A->B->C
        QCOMPARE(topPaths[0].segments.size(), 2);
        QCOMPARE(topPaths[0].segments[0].from, QString("A"));
        QCOMPARE(topPaths[0].segments[0].to, QString("B"));

        // Second path should be A->C direct
        QCOMPARE(topPaths[1].segments.size(), 1);
        QCOMPARE(topPaths[1].segments[0].from, QString("A"));
        QCOMPARE(topPaths[1].segments[0].to, QString("C"));
    }

    void testSerialization() {
        // Test graph serialization and deserialization
        TerminalGraph graph(m_tempDir.path());

        // Create a terminal
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);

        QVariantMap customConfig;
        customConfig["capacity"] = QVariantMap{{"max_capacity", 1000}};

        graph.addTerminal(QStringList{"TestTerminal", "TT"}, customConfig, interfaces, "TestRegion");

        // Add a route
        QVariantMap routeAttributes;
        routeAttributes["distance"] = 100.0;
        graph.addRoute("SelfLoop", "TestTerminal", "TestTerminal", TransportationMode::Truck, routeAttributes);

        qDebug() << "HEREEEEEE _ _ 1";
        // Serialize to JSON
        QJsonObject serialized = graph.serializeGraph();
        QVERIFY(!serialized.isEmpty());
        QVERIFY(serialized.contains("terminals"));
        QVERIFY(serialized["terminals"].toObject().contains("TestTerminal"));

        qDebug() << "HEREEEEEE _ _ 2";

        // Save to file
        graph.saveToFile(m_graphFilePath);
        QVERIFY(QFile::exists(m_graphFilePath));

        qDebug() << "HEREEEEEE _ _ 3";

        // Load from file into a new graph
        TerminalGraph* loadedGraph = TerminalGraph::loadFromFile(m_graphFilePath, m_tempDir.path());
        QVERIFY(loadedGraph != nullptr);

        // Verify loaded graph has the same terminal
        QCOMPARE(loadedGraph->getTerminalCount(), 1);
        QVERIFY(loadedGraph->terminalExists("TestTerminal"));
        QVERIFY(loadedGraph->terminalExists("TT"));

        // Clean up
        delete loadedGraph;
    }

    void testRegionOperations() {
        // Test region-based operations
        TerminalGraph graph(m_tempDir.path());

        // Create terminals in different regions
        QMap<TerminalInterface, QSet<TransportationMode>> interfaces;
        interfaces[TerminalInterface::LAND_SIDE].insert(TransportationMode::Truck);

        QVariantMap customConfig;
        customConfig["capacity"] = QVariantMap{{"max_capacity", 1000}};

        graph.addTerminal(QStringList{"TA1"}, customConfig, interfaces, "RegionA");
        graph.addTerminal(QStringList{"TA2"}, customConfig, interfaces, "RegionA");
        graph.addTerminal(QStringList{"TB1"}, customConfig, interfaces, "RegionB");
        graph.addTerminal(QStringList{"TB2"}, customConfig, interfaces, "RegionB");

        // Get terminals by region
        QStringList regionATerminals = graph.getTerminalsByRegion("RegionA");
        QCOMPARE(regionATerminals.size(), 2);
        QVERIFY(regionATerminals.contains("TA1"));
        QVERIFY(regionATerminals.contains("TA2"));

        // Connect terminals within region
        graph.connectTerminalsInRegionByMode("RegionA");

        // Verify connection was created
        QVariantMap edge = graph.getEdgeByMode("TA1", "TA2", TransportationMode::Truck);
        QVERIFY(!edge.isEmpty());

        // Connect regions
        graph.connectRegionsByMode(TransportationMode::Truck);

        // Should create edges between RegionA and RegionB terminals
        QList<QVariantMap> crossRegionRoutes = graph.getRoutesBetweenRegions("RegionA", "RegionB");
        QVERIFY(!crossRegionRoutes.isEmpty());
    }

    void cleanupTestCase() {
        // Clean up resources
        QFile::remove(m_graphFilePath);

        qDebug() << "Terminal graph tests completed";
    }
};

} // namespace TerminalSim

QTEST_MAIN(TerminalSim::TestTerminalGraph)
#include "test_terminal_graph.moc"
