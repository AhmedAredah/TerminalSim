#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <stdexcept>

#include "terminal/terminal_graph.h"

using namespace TerminalSim;

namespace {

bool nearlyEqual(double lhs, double rhs)
{
    return qAbs(lhs - rhs) <= 1e-9 * qMax(1.0, qMax(qAbs(lhs), qAbs(rhs)));
}

QVariantMap makeTerminal(const QString &id, double handlingSeconds, double fixedFees)
{
    QVariantMap terminal;
    terminal[QStringLiteral("terminal_names")] = QStringList{id};
    terminal[QStringLiteral("display_name")] = id;

    QVariantMap interfaces;
    interfaces[QString::number(static_cast<int>(TerminalInterface::LAND_SIDE))] =
        QVariantList{
            static_cast<int>(TransportationMode::Train)
        };
    interfaces[QString::number(static_cast<int>(TerminalInterface::SEA_SIDE))] =
        QVariantList{
            static_cast<int>(TransportationMode::Ship)
        };
    terminal[QStringLiteral("terminal_interfaces")] = interfaces;

    QVariantMap dwellParams;
    dwellParams[QStringLiteral("scale")] =
        handlingSeconds > 0.0 ? handlingSeconds : 1.0;
    QVariantMap dwell;
    dwell[QStringLiteral("method")] = QStringLiteral("exponential");
    dwell[QStringLiteral("parameters")] = dwellParams;

    QVariantMap cost;
    cost[QStringLiteral("fixed_fees")] = fixedFees;

    QVariantMap customConfig;
    customConfig[QStringLiteral("capacity")] =
        QVariantMap{{QStringLiteral("max_capacity"), 1000}};
    customConfig[QStringLiteral("dwell_time")] = dwell;
    customConfig[QStringLiteral("cost")] = cost;
    terminal[QStringLiteral("custom_config")] = customConfig;
    return terminal;
}

QVariantMap makeRoute(const QString &id, const QString &from, const QString &to)
{
    return QVariantMap{
        {QStringLiteral("route_id"), id},
        {QStringLiteral("start_terminal"), from},
        {QStringLiteral("end_terminal"), to},
        {QStringLiteral("mode"), static_cast<int>(TransportationMode::Train)},
        {QStringLiteral("attributes"),
         QVariantMap{
             {QStringLiteral("cost"), 10.0},
             {QStringLiteral("travelTime"), 5.0},
             {QStringLiteral("distance"), 1.0},
             {QStringLiteral("carbonEmissions"), 1.0},
             {QStringLiteral("risk"), 1.0},
             {QStringLiteral("energyConsumption"), 1.0}
         }}
    };
}

} // namespace

class PathFoundContractTest : public QObject
{
    Q_OBJECT

private slots:
    void test_total_path_cost_reconciles_edge_and_terminal_totals()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 3600.0, 20.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 3600.0, 30.0));
        graph.addRoute(QStringLiteral("AB"),
                       QStringLiteral("A"),
                       QStringLiteral("B"),
                       TransportationMode::Train,
                       makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                           .value(QStringLiteral("attributes")).toMap());

        const QList<Path> paths = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("B"),
            1,
            TransportationMode::Train,
            true);
        QCOMPARE(paths.size(), 1);

        const Path &path = paths.first();
        QVERIFY(nearlyEqual(path.totalPathCost,
                            path.totalEdgeCosts + path.totalTerminalCosts));
        QVERIFY(nearlyEqual(path.rankingCost, path.totalPathCost));
    }

    void test_same_mode_skip_marks_middle_terminal_not_destination_terminal()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 1800.0, 25.0));
        graph.addTerminal(makeTerminal(QStringLiteral("C"), 0.0, 0.0));

        graph.addRoute(QStringLiteral("AB"),
                       QStringLiteral("A"),
                       QStringLiteral("B"),
                       TransportationMode::Train,
                       makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                           .value(QStringLiteral("attributes")).toMap());
        graph.addRoute(QStringLiteral("BC"),
                       QStringLiteral("B"),
                       QStringLiteral("C"),
                       TransportationMode::Train,
                       makeRoute(QStringLiteral("BC"), QStringLiteral("B"), QStringLiteral("C"))
                           .value(QStringLiteral("attributes")).toMap());

        const QList<Path> paths = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("C"),
            1,
            TransportationMode::Train,
            true);
        QCOMPARE(paths.size(), 1);

        const QList<QVariantMap> terminals = paths.first().terminalsInPath;
        QCOMPARE(terminals.size(), 3);
        QCOMPARE(terminals[1].value(QStringLiteral("terminal")).toString(),
                 QStringLiteral("B"));
        QVERIFY(terminals[1].value(QStringLiteral("costs_skipped")).toBool());
        QCOMPARE(terminals[1].value(QStringLiteral("skip_reason")).toString(),
                 QStringLiteral("same_mode_continuation"));
        QVERIFY(!terminals[2].value(QStringLiteral("costs_skipped")).toBool());
        QVERIFY(nearlyEqual(
            terminals[1]
                .value(QStringLiteral("weighted_terminal_total_contribution"))
                .toDouble(),
            0.0));
        QVERIFY(terminals[2]
                    .value(QStringLiteral("weighted_terminal_total_contribution"))
                    .toDouble()
                > 0.0);
    }

    void test_phase3_json_contract_contains_identity_and_explicit_breakdowns()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 1800.0, 25.0));
        graph.addTerminal(makeTerminal(QStringLiteral("C"), 2400.0, 35.0));

        graph.addRoute(QStringLiteral("AB"),
                       QStringLiteral("A"),
                       QStringLiteral("B"),
                       TransportationMode::Train,
                       makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                           .value(QStringLiteral("attributes")).toMap());
        graph.addRoute(QStringLiteral("BC"),
                       QStringLiteral("B"),
                       QStringLiteral("C"),
                       TransportationMode::Ship,
                       makeRoute(QStringLiteral("BC"), QStringLiteral("B"), QStringLiteral("C"))
                           .value(QStringLiteral("attributes")).toMap());

        const QList<Path> paths = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("C"),
            2,
            TransportationMode::Any,
            true);
        QCOMPARE(paths.size(), 1);

        const Path &path = paths.first();
        const QJsonObject json = path.toJson();

        QVERIFY(json.contains(QStringLiteral("path_uid")));
        QVERIFY(!json.value(QStringLiteral("path_uid")).toString().isEmpty());
        QCOMPARE(json.value(QStringLiteral("rank")).toInt(), 0);
        QCOMPARE(json.value(QStringLiteral("path_id")).toInt(), 1);
        QCOMPARE(json.value(QStringLiteral("start_terminal")).toString(),
                 QStringLiteral("A"));
        QCOMPARE(json.value(QStringLiteral("end_terminal")).toString(),
                 QStringLiteral("C"));
        QVERIFY(json.contains(QStringLiteral("ranking_cost")));
        QVERIFY(json.contains(QStringLiteral("weighted_edge_cost_total")));
        QVERIFY(json.contains(QStringLiteral("weighted_terminal_cost_total")));
        QVERIFY(json.contains(QStringLiteral("weighted_terminal_delay_total")));
        QVERIFY(json.contains(QStringLiteral("weighted_terminal_direct_cost_total")));
        QVERIFY(json.contains(QStringLiteral("raw_terminal_delay_total")));
        QVERIFY(json.contains(QStringLiteral("raw_terminal_cost_total")));

        const QJsonObject discoveryContext =
            json.value(QStringLiteral("discovery_context")).toObject();
        QCOMPARE(discoveryContext.value(QStringLiteral("start_terminal")).toString(),
                 QStringLiteral("A"));
        QCOMPARE(discoveryContext.value(QStringLiteral("end_terminal")).toString(),
                 QStringLiteral("C"));
        QCOMPARE(discoveryContext.value(QStringLiteral("requested_top_n")).toInt(),
                 2);
        QCOMPARE(
            discoveryContext
                .value(QStringLiteral("skip_same_mode_terminal_delays_and_costs"))
                .toBool(),
            true);

        const QJsonArray terminals =
            json.value(QStringLiteral("terminals_in_path")).toArray();
        QCOMPARE(terminals.size(), 3);
        for (int i = 0; i < terminals.size(); ++i)
        {
            const QJsonObject terminal = terminals[i].toObject();
            QCOMPARE(terminal.value(QStringLiteral("sequence_index")).toInt(), i);
            QVERIFY(terminal.contains(QStringLiteral("terminal_id")));
            QVERIFY(terminal.contains(
                QStringLiteral("weighted_terminal_delay_contribution")));
            QVERIFY(terminal.contains(
                QStringLiteral("weighted_terminal_cost_contribution")));
            QVERIFY(terminal.contains(
                QStringLiteral("weighted_terminal_total_contribution")));
        }

        const QJsonArray segments =
            json.value(QStringLiteral("segments")).toArray();
        QCOMPARE(segments.size(), 2);
        for (int i = 0; i < segments.size(); ++i)
        {
            const QJsonObject segment = segments[i].toObject();
            QCOMPARE(segment.value(QStringLiteral("sequence_index")).toInt(), i);
            QVERIFY(segment.contains(QStringLiteral("ranking_cost_contribution")));
            QVERIFY(segment.contains(QStringLiteral("weighted_edge_cost")));
            QVERIFY(segment.contains(
                QStringLiteral("weighted_terminal_cost_embedded_in_segment")));
            QVERIFY(nearlyEqual(
                segment.value(QStringLiteral("weight")).toDouble(),
                segment.value(QStringLiteral("ranking_cost_contribution"))
                    .toDouble()));
        }
    }

    void test_path_uid_is_deterministic_for_same_query()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 1800.0, 25.0));
        graph.addRoute(QStringLiteral("AB"),
                       QStringLiteral("A"),
                       QStringLiteral("B"),
                       TransportationMode::Train,
                       makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                           .value(QStringLiteral("attributes")).toMap());

        const QList<Path> first = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("B"),
            1,
            TransportationMode::Train,
            true);
        const QList<Path> second = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("B"),
            1,
            TransportationMode::Train,
            true);

        QCOMPARE(first.size(), 1);
        QCOMPARE(second.size(), 1);
        QCOMPARE(first.first().pathUid, second.first().pathUid);
    }

    void test_path_uid_does_not_depend_on_requested_top_n()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 1800.0, 25.0));
        graph.addRoute(QStringLiteral("AB"),
                       QStringLiteral("A"),
                       QStringLiteral("B"),
                       TransportationMode::Train,
                       makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                           .value(QStringLiteral("attributes")).toMap());

        const QList<Path> first = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("B"),
            1,
            TransportationMode::Train,
            true);
        const QList<Path> second = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("B"),
            5,
            TransportationMode::Train,
            true);

        QCOMPARE(first.size(), 1);
        QCOMPARE(second.size(), 1);
        QCOMPARE(first.first().pathUid, second.first().pathUid);
    }

    void test_unknown_route_attribute_is_rejected()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 0.0, 0.0));

        QVariantMap attrs =
            makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                .value(QStringLiteral("attributes")).toMap();
        attrs.insert(QStringLiteral("travellTime"), 99.0);

        QVERIFY_EXCEPTION_THROWN(
            graph.addRoute(QStringLiteral("AB"),
                           QStringLiteral("A"),
                           QStringLiteral("B"),
                           TransportationMode::Train,
                           attrs),
            std::invalid_argument);
    }

    void test_negative_route_attribute_is_rejected()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 0.0, 0.0));

        QVariantMap attrs =
            makeRoute(QStringLiteral("AB"), QStringLiteral("A"), QStringLiteral("B"))
                .value(QStringLiteral("attributes")).toMap();
        attrs[QStringLiteral("cost")] = -1.0;

        QVERIFY_EXCEPTION_THROWN(
            graph.addRoute(QStringLiteral("AB"),
                           QStringLiteral("A"),
                           QStringLiteral("B"),
                           TransportationMode::Train,
                           attrs),
            std::invalid_argument);
    }

    void test_add_routes_is_transactional_when_later_route_is_invalid()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminal(QStringLiteral("A"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("B"), 0.0, 0.0));
        graph.addTerminal(makeTerminal(QStringLiteral("C"), 0.0, 0.0));

        QVariantMap invalid =
            makeRoute(QStringLiteral("BC"), QStringLiteral("B"), QStringLiteral("C"));
        QVariantMap invalidAttrs =
            invalid.value(QStringLiteral("attributes")).toMap();
        invalidAttrs.insert(QStringLiteral("travellTime"), 99.0);
        invalid[QStringLiteral("attributes")] = invalidAttrs;

        QVERIFY_EXCEPTION_THROWN(
            graph.addRoutes({makeRoute(QStringLiteral("AB"),
                                       QStringLiteral("A"),
                                       QStringLiteral("B")),
                             invalid}),
            std::invalid_argument);

        const QList<Path> paths = graph.findTopNShortestPaths(
            QStringLiteral("A"),
            QStringLiteral("B"),
            1,
            TransportationMode::Train,
            true);
        QVERIFY(paths.isEmpty());
    }

    void test_strict_enum_parsing_rejects_invalid_values()
    {
        QCOMPARE(EnumUtils::stringToTransportationMode(QStringLiteral("2")),
                 TransportationMode::Train);
        QCOMPARE(EnumUtils::stringToTransportationMode(QStringLiteral("-1")),
                 TransportationMode::Any);
        QVERIFY_EXCEPTION_THROWN(
            EnumUtils::stringToTransportationMode(QStringLiteral("Rail")),
            std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(
            EnumUtils::stringToTerminalInterface(QStringLiteral("RAIL_SIDE")),
            std::invalid_argument);
    }
};

QTEST_MAIN(PathFoundContractTest)
#include "test_pathfound_contract.moc"
