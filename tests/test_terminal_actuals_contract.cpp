#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <containerLib/container.h>

#include "server/command_processor.h"
#include "terminal/terminal_graph.h"

using namespace TerminalSim;

namespace
{

QVariantMap makeTerminalSpec(const QString &id,
                            int            maxCapacity = 10,
                            double         dwellScaleSeconds = 3600.0,
                            double         fixedFees = 5.0,
                            bool           enableSystemDynamics = true)
{
    QVariantMap terminal;
    terminal[QStringLiteral("terminal_names")] = QStringList{id};
    terminal[QStringLiteral("display_name")] = id;

    QVariantMap interfaces;
    interfaces[QString::number(
        static_cast<int>(TerminalInterface::LAND_SIDE))] =
        QVariantList{
            static_cast<int>(TransportationMode::Truck),
            static_cast<int>(TransportationMode::Train)};
    terminal[QStringLiteral("terminal_interfaces")] = interfaces;

    QVariantMap dwellParams;
    dwellParams[QStringLiteral("scale")] = dwellScaleSeconds;
    QVariantMap dwell;
    dwell[QStringLiteral("method")] = QStringLiteral("exponential");
    dwell[QStringLiteral("parameters")] = dwellParams;

    QVariantMap cost;
    cost[QStringLiteral("fixed_fees")] = fixedFees;
    cost[QStringLiteral("customs_fees")] = 0.0;
    cost[QStringLiteral("risk_factor")] = 0.0;

    QVariantMap systemDynamics;
    systemDynamics[QStringLiteral("enabled")] = enableSystemDynamics;
    systemDynamics[QStringLiteral("critical_utilization")] = 0.7;
    systemDynamics[QStringLiteral("max_service_rate")] = 100.0;
    systemDynamics[QStringLiteral("ship_arrival_penalty")] = 7200.0;
    systemDynamics[QStringLiteral("truck_arrival_penalty")] = 600.0;
    systemDynamics[QStringLiteral("train_arrival_penalty")] = 3600.0;

    QVariantMap customConfig;
    customConfig[QStringLiteral("capacity")] =
        QVariantMap{{QStringLiteral("max_capacity"), maxCapacity}};
    customConfig[QStringLiteral("dwell_time")] = dwell;
    customConfig[QStringLiteral("cost")] = cost;
    customConfig[QStringLiteral("customs")] =
        QVariantMap{{QStringLiteral("probability"), 0.0},
                    {QStringLiteral("delay_mean"), 0.0},
                    {QStringLiteral("delay_variance"), 0.0}};
    customConfig[QStringLiteral("system_dynamics")] = systemDynamics;
    terminal[QStringLiteral("custom_config")] = customConfig;
    return terminal;
}

ContainerCore::Container makeContainer(const QString &id)
{
    ContainerCore::Container container;
    container.setContainerID(id);
    return container;
}

ContainerCore::Container makeTrackedContainer(const QString &id,
                                              const QString &executionId,
                                              const QString &pathIdentity,
                                              const QString &scenarioTerminalId,
                                              int            terminalSequenceIndex,
                                              int            segmentIndex,
                                              const QString &vehicleMode,
                                              const QString &vehicleId)
{
    ContainerCore::Container container = makeContainer(id);
    using Hauler = ContainerCore::Container::HaulerType;
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("execution_id"),
                                executionId);
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("path_identity"),
                                pathIdentity);
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("scenario_terminal_id"),
                                scenarioTerminalId);
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("runtime_terminal_id"),
                                scenarioTerminalId + QStringLiteral("_runtime"));
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("terminal_sequence_index"),
                                terminalSequenceIndex);
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("segment_index"),
                                segmentIndex);
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("vehicle_mode"),
                                vehicleMode);
    container.addCustomVariable(Hauler::noHauler,
                                QStringLiteral("vehicle_id"),
                                vehicleId);
    return container;
}

QJsonObject command(const QString &name, const QJsonObject &params)
{
    return QJsonObject{
        {QStringLiteral("command"), name},
        {QStringLiteral("params"), params},
        {QStringLiteral("request_id"), QStringLiteral("req-1")}};
}

} // namespace

class TerminalActualsContractTest : public QObject
{
    Q_OBJECT

private slots:
    void test_get_system_dynamics_state_uses_distinct_event_name()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1")));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        QList<ContainerCore::Container> seedContainers;
        for (int i = 0; i < 8; ++i)
        {
            seedContainers.append(makeContainer(
                QStringLiteral("seed_%1").arg(i)));
        }
        terminal->addContainers(seedContainers, -1.0,
                                TransportationMode::Truck);
        terminal->updateSystemDynamics(3600.0, 3600.0);

        const QJsonObject response = processor.processJsonCommand(
            command(QStringLiteral("get_system_dynamics_state"),
                    QJsonObject{{QStringLiteral("terminal_id"),
                                 QStringLiteral("T1")}}));

        QCOMPARE(response.value(QStringLiteral("event")).toString(),
                 QStringLiteral("systemDynamicsState"));
        QVERIFY(response.value(QStringLiteral("success")).toBool());

        const QJsonObject result =
            response.value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("terminal_id")).toString(),
                 QStringLiteral("T1"));
        QVERIFY(result.contains(QStringLiteral("state")));
        QVERIFY(result.contains(QStringLiteral("parameters")));
        QVERIFY(result.contains(QStringLiteral("remaining_service_capacity")));
    }

    void test_batch_runtime_state_and_projection_commands()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1")));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        QList<ContainerCore::Container> seedContainers;
        for (int i = 0; i < 8; ++i)
        {
            seedContainers.append(makeContainer(
                QStringLiteral("seed_%1").arg(i)));
        }
        terminal->addContainers(seedContainers, -1.0,
                                TransportationMode::Train);
        terminal->updateSystemDynamics(7200.0, 3600.0);

        const QJsonArray terminalIds{
            QJsonValue(QStringLiteral("T1"))};

        const QJsonObject stateResponse = processor.processJsonCommand(
            command(QStringLiteral("get_terminals_runtime_state"),
                    QJsonObject{{QStringLiteral("terminal_ids"),
                                 terminalIds}}));
        QCOMPARE(stateResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("terminalRuntimeState"));
        QVERIFY(stateResponse.value(QStringLiteral("success")).toBool());
        const QJsonArray stateResults =
            stateResponse.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("results"))
                .toArray();
        QCOMPARE(stateResults.size(), 1);
        QCOMPARE(stateResults.first().toObject()
                     .value(QStringLiteral("terminal_id"))
                     .toString(),
                 QStringLiteral("T1"));

        const QJsonObject projectionResponse = processor.processJsonCommand(
            command(QStringLiteral("get_terminals_runtime_projections"),
                    QJsonObject{{QStringLiteral("terminal_ids"),
                                 terminalIds}}));
        QCOMPARE(projectionResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("terminalRuntimeProjections"));
        QVERIFY(projectionResponse.value(QStringLiteral("success")).toBool());
        const QJsonArray projectionResults =
            projectionResponse.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("results"))
                .toArray();
        QCOMPARE(projectionResults.size(), 1);

        const QJsonObject projections =
            projectionResults.first().toObject();
        QVERIFY(projections.contains(QStringLiteral("ship")));
        QVERIFY(projections.contains(QStringLiteral("truck")));
        QVERIFY(projections.contains(QStringLiteral("train")));

        const QJsonObject trainProjection =
            projections.value(QStringLiteral("train")).toObject();
        QVERIFY(trainProjection.value(
                     QStringLiteral("expected_total_handling_seconds"))
                    .toDouble()
                > 0.0);
        QVERIFY(trainProjection.value(QStringLiteral("delay_multiplier"))
                    .toDouble()
                >= 1.0);
    }

    void test_arrival_side_execution_results_are_recorded_and_cleared()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1")));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        QList<ContainerCore::Container> exec1Containers;
        exec1Containers.append(makeTrackedContainer(
            QStringLiteral("c1"),
            QStringLiteral("exec-1"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-1")));
        exec1Containers.append(makeTrackedContainer(
            QStringLiteral("c2"),
            QStringLiteral("exec-1"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-1")));
        exec1Containers.append(makeTrackedContainer(
            QStringLiteral("c3"),
            QStringLiteral("exec-1"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-1")));
        terminal->addContainers(exec1Containers, 100.0,
                                TransportationMode::Train);

        QList<ContainerCore::Container> exec2Containers;
        exec2Containers.append(makeTrackedContainer(
            QStringLiteral("c4"),
            QStringLiteral("exec-2"),
            QStringLiteral("path-2"),
            QStringLiteral("T1"),
            1,
            1,
            QStringLiteral("Truck"),
            QStringLiteral("truck-4")));
        terminal->addContainers(exec2Containers, 200.0,
                                TransportationMode::Truck);

        const QJsonArray terminalIds{
            QJsonValue(QStringLiteral("T1"))};

        const QJsonObject resultsResponse = processor.processJsonCommand(
            command(QStringLiteral("get_terminal_execution_results"),
                    QJsonObject{
                        {QStringLiteral("execution_id"),
                         QStringLiteral("exec-1")},
                        {QStringLiteral("terminal_ids"), terminalIds}}));
        QCOMPARE(resultsResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("terminalExecutionResults"));
        QVERIFY(resultsResponse.value(QStringLiteral("success")).toBool());

        const QJsonArray results =
            resultsResponse.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("results"))
                .toArray();
        QCOMPARE(results.size(), 1);

        const QJsonObject result = results.first().toObject();
        QCOMPARE(result.value(QStringLiteral("execution_id")).toString(),
                 QStringLiteral("exec-1"));
        QCOMPARE(result.value(QStringLiteral("path_identity")).toString(),
                 QStringLiteral("path-1"));
        QCOMPARE(result.value(QStringLiteral("scenario_terminal_id")).toString(),
                 QStringLiteral("T1"));
        QCOMPARE(result.value(
                     QStringLiteral("total_dropped_containers")).toInt(),
                 3);
        QCOMPARE(result.value(QStringLiteral("arrival_events")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("pickup_events")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("raw_batch_records"))
                     .toArray()
                     .size(),
                 1);
        QCOMPARE(result.value(QStringLiteral("actual_direct_cost_usd"))
                     .toDouble(),
                 15.0);

        const QJsonObject clearResponse = processor.processJsonCommand(
            command(QStringLiteral("clear_terminal_execution_results"),
                    QJsonObject{
                        {QStringLiteral("execution_id"),
                         QStringLiteral("exec-1")},
                        {QStringLiteral("terminal_ids"), terminalIds}}));
        QCOMPARE(clearResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("terminalExecutionResultsCleared"));
        QVERIFY(clearResponse.value(QStringLiteral("success")).toBool());
        QCOMPARE(clearResponse.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("records_cleared"))
                     .toInt(),
                 1);

        const QJsonObject exec1AfterClear = processor.processJsonCommand(
            command(QStringLiteral("get_terminal_execution_results"),
                    QJsonObject{
                        {QStringLiteral("execution_id"),
                         QStringLiteral("exec-1")},
                        {QStringLiteral("terminal_ids"), terminalIds}}));
        QCOMPARE(exec1AfterClear.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("results"))
                     .toArray()
                     .size(),
                 0);

        const QJsonObject exec2StillPresent = processor.processJsonCommand(
            command(QStringLiteral("get_terminal_execution_results"),
                    QJsonObject{
                        {QStringLiteral("execution_id"),
                         QStringLiteral("exec-2")},
                        {QStringLiteral("terminal_ids"), terminalIds}}));
        QCOMPARE(exec2StillPresent.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("results"))
                     .toArray()
                     .size(),
                 1);
    }
};

QTEST_MAIN(TerminalActualsContractTest)
#include "test_terminal_actuals_contract.moc"
