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
                            bool           enableSystemDynamics = true,
                            double         maxServiceRate = 100.0)
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
    systemDynamics[QStringLiteral("max_service_rate")] = maxServiceRate;
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
                                              const QString &canonicalPathKey,
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
                                QStringLiteral("canonical_path_key"),
                                canonicalPathKey);
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
        QCOMPARE(result.value(QStringLiteral("canonical_path_key")).toString(),
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

    void test_criteria_dequeue_filters_path_and_records_pickup()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1"),
                                           10,
                                           1.0,
                                           5.0,
                                           false));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        QList<ContainerCore::Container> containers;
        ContainerCore::Container path1ToT2 = makeTrackedContainer(
            QStringLiteral("path1_t2"),
            QStringLiteral("exec-criteria"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            1,
            QStringLiteral("Train"),
            QStringLiteral("train-1"));
        path1ToT2.addDestination(QStringLiteral("T2"));
        containers.append(path1ToT2);

        ContainerCore::Container path2ToT2 = makeTrackedContainer(
            QStringLiteral("path2_t2"),
            QStringLiteral("exec-criteria"),
            QStringLiteral("path-2"),
            QStringLiteral("T1"),
            0,
            1,
            QStringLiteral("Train"),
            QStringLiteral("train-2"));
        path2ToT2.addDestination(QStringLiteral("T2"));
        containers.append(path2ToT2);

        ContainerCore::Container path1ToT3 = makeTrackedContainer(
            QStringLiteral("path1_t3"),
            QStringLiteral("exec-criteria"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            1,
            QStringLiteral("Train"),
            QStringLiteral("train-1"));
        path1ToT3.addDestination(QStringLiteral("T3"));
        containers.append(path1ToT3);

        terminal->addContainers(containers, 100.0,
                                TransportationMode::Train);

        const QJsonArray pathFilter{
            QJsonObject{
                {QStringLiteral("hauler"), QStringLiteral("noHauler")},
                {QStringLiteral("key"), QStringLiteral("canonical_path_key")},
                {QStringLiteral("value"), QStringLiteral("path-1")}}};

        const QJsonObject response = processor.processJsonCommand(
            command(QStringLiteral("dequeue_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")},
                             {QStringLiteral("custom_variables"),
                              pathFilter},
                             {QStringLiteral("sort_by"),
                              QStringLiteral("leaving_time")}}}}));

        QCOMPARE(response.value(QStringLiteral("event")).toString(),
                 QStringLiteral("containersFetched"));
        QVERIFY(response.value(QStringLiteral("success")).toBool());
        const QJsonArray dequeued =
            response.value(QStringLiteral("result")).toArray();
        QCOMPARE(dequeued.size(), 1);
        QCOMPARE(dequeued.first().toObject()
                     .value(QStringLiteral("containerID"))
                     .toString(),
                 QStringLiteral("path1_t2"));

        QCOMPARE(terminal->getContainersByNextDestination(
                     QStringLiteral("T2")).size(),
                 1);
        QCOMPARE(terminal->getContainersByNextDestination(
                     QStringLiteral("T3")).size(),
                 1);

        const QJsonObject resultsResponse = processor.processJsonCommand(
            command(QStringLiteral("get_terminal_execution_results"),
                    QJsonObject{
                        {QStringLiteral("execution_id"),
                         QStringLiteral("exec-criteria")},
                        {QStringLiteral("terminal_ids"),
                         QJsonArray{QJsonValue(QStringLiteral("T1"))}},
                        {QStringLiteral("canonical_path_keys"),
                         QJsonArray{QJsonValue(QStringLiteral("path-1"))}}}));
        QVERIFY(resultsResponse.value(QStringLiteral("success")).toBool());

        const QJsonArray results =
            resultsResponse.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("results"))
                .toArray();
        QCOMPARE(results.size(), 1);
        const QJsonObject actuals = results.first().toObject();
        QCOMPARE(actuals.value(QStringLiteral("total_dropped_containers"))
                     .toInt(),
                 2);
        QCOMPARE(actuals.value(QStringLiteral("total_picked_containers"))
                     .toInt(),
                 1);
        QCOMPARE(actuals.value(QStringLiteral("pickup_events")).toInt(), 1);
    }

    void test_reservation_commit_and_release_are_idempotent()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1"),
                                           10,
                                           1.0,
                                           5.0,
                                           false));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        QList<ContainerCore::Container> containers;
        ContainerCore::Container path1 = makeTrackedContainer(
            QStringLiteral("path1_t2"),
            QStringLiteral("exec-reserve"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            1,
            QStringLiteral("Train"),
            QStringLiteral("train-1"));
        path1.addDestination(QStringLiteral("T2"));
        containers.append(path1);

        ContainerCore::Container path2 = makeTrackedContainer(
            QStringLiteral("path2_t2"),
            QStringLiteral("exec-reserve"),
            QStringLiteral("path-2"),
            QStringLiteral("T1"),
            0,
            1,
            QStringLiteral("Train"),
            QStringLiteral("train-2"));
        path2.addDestination(QStringLiteral("T2"));
        containers.append(path2);

        terminal->addContainers(containers, 100.0,
                                TransportationMode::Train);

        const QJsonArray path1Filter{
            QJsonObject{
                {QStringLiteral("hauler"), QStringLiteral("noHauler")},
                {QStringLiteral("key"), QStringLiteral("canonical_path_key")},
                {QStringLiteral("value"), QStringLiteral("path-1")}}};

        const QJsonObject reserveResponse = processor.processJsonCommand(
            command(QStringLiteral("reserve_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-1")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")},
                             {QStringLiteral("custom_variables"),
                              path1Filter}}}}));
        QCOMPARE(reserveResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("containersReserved"));
        QVERIFY(reserveResponse.value(QStringLiteral("success")).toBool());
        const QJsonObject reserveResult =
            reserveResponse.value(QStringLiteral("result")).toObject();
        QCOMPARE(reserveResult.value(QStringLiteral("state")).toString(),
                 QStringLiteral("active"));
        QCOMPARE(reserveResult.value(QStringLiteral("container_count")).toInt(),
                 1);
        QCOMPARE(terminal->getContainerCount(), 2);

        const QJsonObject blockedDequeue = processor.processJsonCommand(
            command(QStringLiteral("dequeue_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")},
                             {QStringLiteral("custom_variables"),
                              path1Filter}}}}));
        QVERIFY(blockedDequeue.value(QStringLiteral("success")).toBool());
        QCOMPARE(blockedDequeue.value(QStringLiteral("result"))
                     .toArray()
                     .size(),
                 0);

        const QJsonObject commitResponse = processor.processJsonCommand(
            command(QStringLiteral("commit_container_reservation"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-1")}}));
        QCOMPARE(commitResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("containerReservationCommitted"));
        QVERIFY(commitResponse.value(QStringLiteral("success")).toBool());
        QCOMPARE(commitResponse.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("container_count"))
                     .toInt(),
                 1);
        QCOMPARE(terminal->getContainerCount(), 1);

        const QJsonObject duplicateCommit = processor.processJsonCommand(
            command(QStringLiteral("commit_container_reservation"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-1")}}));
        QVERIFY(duplicateCommit.value(QStringLiteral("success")).toBool());
        QCOMPARE(duplicateCommit.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("container_count"))
                     .toInt(),
                 1);
        QCOMPARE(terminal->getContainerCount(), 1);

        const QJsonArray path2Filter{
            QJsonObject{
                {QStringLiteral("hauler"), QStringLiteral("noHauler")},
                {QStringLiteral("key"), QStringLiteral("canonical_path_key")},
                {QStringLiteral("value"), QStringLiteral("path-2")}}};

        const QJsonObject reserveRelease = processor.processJsonCommand(
            command(QStringLiteral("reserve_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-2")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")},
                             {QStringLiteral("custom_variables"),
                              path2Filter}}}}));
        QVERIFY(reserveRelease.value(QStringLiteral("success")).toBool());
        QCOMPARE(reserveRelease.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("container_count"))
                     .toInt(),
                 1);

        const QJsonObject releaseResponse = processor.processJsonCommand(
            command(QStringLiteral("release_container_reservation"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-2")}}));
        QCOMPARE(releaseResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("containerReservationReleased"));
        QVERIFY(releaseResponse.value(QStringLiteral("success")).toBool());
        QCOMPARE(releaseResponse.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("released_count"))
                     .toInt(),
                 1);
        QCOMPARE(terminal->getContainerCount(), 1);

        const QJsonObject resultsResponse = processor.processJsonCommand(
            command(QStringLiteral("get_terminal_execution_results"),
                    QJsonObject{
                        {QStringLiteral("execution_id"),
                         QStringLiteral("exec-reserve")},
                        {QStringLiteral("terminal_ids"),
                         QJsonArray{QJsonValue(QStringLiteral("T1"))}},
                        {QStringLiteral("canonical_path_keys"),
                         QJsonArray{QJsonValue(QStringLiteral("path-1")),
                                    QJsonValue(QStringLiteral("path-2"))}}}));
        QVERIFY(resultsResponse.value(QStringLiteral("success")).toBool());
        const QJsonArray results =
            resultsResponse.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("results"))
                .toArray();
        QCOMPARE(results.size(), 2);

        int path1Picked = -1;
        int path2Picked = -1;
        for (const QJsonValue &value : results) {
            const QJsonObject result = value.toObject();
            if (result.value(QStringLiteral("canonical_path_key")).toString()
                == QStringLiteral("path-1")) {
                path1Picked =
                    result.value(QStringLiteral("total_picked_containers"))
                        .toInt();
            }
            if (result.value(QStringLiteral("canonical_path_key")).toString()
                == QStringLiteral("path-2")) {
                path2Picked =
                    result.value(QStringLiteral("total_picked_containers"))
                        .toInt();
            }
        }
        QCOMPARE(path1Picked, 1);
        QCOMPARE(path2Picked, 0);
    }

    void test_preload_does_not_count_as_runtime_arrival_or_actual()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1")));

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);
        terminal->updateSystemDynamics(0.0, 3600.0);

        QList<ContainerCore::Container> preloadContainers;
        preloadContainers.append(makeTrackedContainer(
            QStringLiteral("seed-1"),
            QStringLiteral("exec-preload"),
            QStringLiteral("path-preload"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-seed")));
        preloadContainers.append(makeTrackedContainer(
            QStringLiteral("seed-2"),
            QStringLiteral("exec-preload"),
            QStringLiteral("path-preload"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-seed")));

        terminal->addContainers(preloadContainers, -1.0,
                                TransportationMode::Train);

        QCOMPARE(terminal->getContainerCount(), 2);
        const QJsonObject state =
            terminal->getSystemDynamicsState()
                .value(QStringLiteral("state"))
                .toObject();
        QCOMPARE(state.value(QStringLiteral("arrivals_this_step")).toInt(),
                 0);

        const QJsonArray actuals =
            terminal->getTerminalExecutionResults(
                QStringLiteral("exec-preload"));
        QCOMPARE(actuals.size(), 0);
    }

    void test_reservation_commit_records_explicit_operation_time()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1"),
                                           10,
                                           1.0,
                                           5.0,
                                           false));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        ContainerCore::Container container = makeTrackedContainer(
            QStringLiteral("timed-pickup"),
            QStringLiteral("exec-timed-pickup"),
            QStringLiteral("path-timed"),
            QStringLiteral("T1"),
            0,
            1,
            QStringLiteral("Train"),
            QStringLiteral("train-timed"));
        container.addDestination(QStringLiteral("T2"));
        terminal->addContainers(QList<ContainerCore::Container>{container},
                                100.0,
                                TransportationMode::Train);

        const QJsonObject reserveResponse = processor.processJsonCommand(
            command(QStringLiteral("reserve_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-timed")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")}}}}));
        QVERIFY(reserveResponse.value(QStringLiteral("success")).toBool());

        const QJsonObject commitResponse = processor.processJsonCommand(
            command(QStringLiteral("commit_container_reservation"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-timed")},
                        {QStringLiteral("operation_time"), 1234.5}}));
        QVERIFY(commitResponse.value(QStringLiteral("success")).toBool());

        const QJsonArray actuals =
            terminal->getTerminalExecutionResults(
                QStringLiteral("exec-timed-pickup"));
        QCOMPARE(actuals.size(), 1);
        const QJsonArray records =
            actuals.first()
                .toObject()
                .value(QStringLiteral("raw_batch_records"))
                .toArray();

        bool foundPickup = false;
        for (const QJsonValue &recordValue : records) {
            const QJsonObject record = recordValue.toObject();
            if (record.value(QStringLiteral("event_type")).toString()
                == QStringLiteral("pickup_departure")) {
                foundPickup = true;
                QCOMPARE(record.value(QStringLiteral("event_time"))
                             .toDouble(),
                         1234.5);
            }
        }
        QVERIFY(foundPickup);
    }

    void test_reservations_consume_remaining_service_capacity()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1"),
                                           10,
                                           1.0,
                                           5.0,
                                           true,
                                           1.0));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        ContainerCore::Container path1 = makeTrackedContainer(
            QStringLiteral("capacity-path-1"),
            QStringLiteral("exec-capacity"),
            QStringLiteral("path-1"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-1"));
        path1.addDestination(QStringLiteral("T2"));

        ContainerCore::Container path2 = makeTrackedContainer(
            QStringLiteral("capacity-path-2"),
            QStringLiteral("exec-capacity"),
            QStringLiteral("path-2"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-2"));
        path2.addDestination(QStringLiteral("T2"));

        terminal->addContainers(
            QList<ContainerCore::Container>{path1, path2},
            -1.0,
            TransportationMode::Train);
        terminal->updateSystemDynamics(3600.0, 3600.0);

        const QJsonArray path1Filter{
            QJsonObject{
                {QStringLiteral("hauler"), QStringLiteral("noHauler")},
                {QStringLiteral("key"), QStringLiteral("canonical_path_key")},
                {QStringLiteral("value"), QStringLiteral("path-1")}}};

        const QJsonObject reserveResponse = processor.processJsonCommand(
            command(QStringLiteral("reserve_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("reservation_id"),
                         QStringLiteral("res-capacity")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")},
                             {QStringLiteral("custom_variables"),
                              path1Filter}}}}));
        QVERIFY(reserveResponse.value(QStringLiteral("success")).toBool());
        QCOMPARE(reserveResponse.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("container_count"))
                     .toInt(),
                 1);

        QCOMPARE(terminal->getRemainingServiceCapacity(), 0);

        const QJsonArray path2Filter{
            QJsonObject{
                {QStringLiteral("hauler"), QStringLiteral("noHauler")},
                {QStringLiteral("key"), QStringLiteral("canonical_path_key")},
                {QStringLiteral("value"), QStringLiteral("path-2")}}};
        const QJsonObject blockedDequeue = processor.processJsonCommand(
            command(QStringLiteral("dequeue_containers"),
                    QJsonObject{
                        {QStringLiteral("terminal_id"),
                         QStringLiteral("T1")},
                        {QStringLiteral("criteria"),
                         QJsonObject{
                             {QStringLiteral("next_destination"),
                              QStringLiteral("T2")},
                             {QStringLiteral("custom_variables"),
                              path2Filter}}}}));
        QVERIFY(blockedDequeue.value(QStringLiteral("success")).toBool());
        QCOMPARE(blockedDequeue.value(QStringLiteral("result"))
                     .toArray()
                     .size(),
                 0);
    }

    void test_fractional_service_capacity_carries_between_sd_steps()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1"),
                                           10,
                                           1.0,
                                           5.0,
                                           true,
                                           1.0));

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        terminal->updateSystemDynamics(0.0, 900.0);
        QJsonObject state =
            terminal->getSystemDynamicsState()
                .value(QStringLiteral("state"))
                .toObject();
        QCOMPARE(state.value(QStringLiteral("service_capacity_this_step"))
                     .toInt(),
                 0);
        QVERIFY(qAbs(state.value(
                         QStringLiteral("service_capacity_carryover_teu"))
                         .toDouble()
                     - 0.25)
                < 0.000001);

        terminal->updateSystemDynamics(900.0, 900.0);
        state = terminal->getSystemDynamicsState()
                    .value(QStringLiteral("state"))
                    .toObject();
        QCOMPARE(state.value(QStringLiteral("service_capacity_this_step"))
                     .toInt(),
                 0);
        QVERIFY(qAbs(state.value(
                         QStringLiteral("service_capacity_carryover_teu"))
                         .toDouble()
                     - 0.5)
                < 0.000001);

        terminal->updateSystemDynamics(1800.0, 900.0);
        state = terminal->getSystemDynamicsState()
                    .value(QStringLiteral("state"))
                    .toObject();
        QCOMPARE(state.value(QStringLiteral("service_capacity_this_step"))
                     .toInt(),
                 0);
        QVERIFY(qAbs(state.value(
                         QStringLiteral("service_capacity_carryover_teu"))
                         .toDouble()
                     - 0.75)
                < 0.000001);

        terminal->updateSystemDynamics(2700.0, 900.0);
        state = terminal->getSystemDynamicsState()
                    .value(QStringLiteral("state"))
                    .toObject();
        QCOMPARE(state.value(QStringLiteral("service_capacity_this_step"))
                     .toInt(),
                 1);
        QVERIFY(qAbs(state.value(
                         QStringLiteral("service_capacity_carryover_teu"))
                         .toDouble())
                < 0.000001);
    }

    void test_reset_runtime_state_contract()
    {
        TerminalGraph graph;
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T1")));
        graph.addTerminal(makeTerminalSpec(QStringLiteral("T2")));
        CommandProcessor processor(&graph);

        auto *terminal = graph.getTerminal(QStringLiteral("T1"));
        QVERIFY(terminal != nullptr);

        ContainerCore::Container container = makeTrackedContainer(
            QStringLiteral("reset-container"),
            QStringLiteral("exec-reset"),
            QStringLiteral("path-reset"),
            QStringLiteral("T1"),
            0,
            0,
            QStringLiteral("Train"),
            QStringLiteral("train-reset"));
        terminal->addContainers(QList<ContainerCore::Container>{container},
                                100.0,
                                TransportationMode::Train);
        QCOMPARE(terminal->getContainerCount(), 1);
        QCOMPARE(terminal->getTerminalExecutionResults(
                     QStringLiteral("exec-reset")).size(),
                 1);

        const QJsonObject resetResponse = processor.processJsonCommand(
            command(QStringLiteral("reset_runtime_state"),
                    QJsonObject{}));
        QCOMPARE(resetResponse.value(QStringLiteral("event")).toString(),
                 QStringLiteral("runtimeStateReset"));
        QVERIFY(resetResponse.value(QStringLiteral("success")).toBool());
        QCOMPARE(resetResponse.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("terminals_reset"))
                     .toInt(),
                 2);

        QCOMPARE(graph.getTerminalCount(), 2);
        QVERIFY(graph.getTerminal(QStringLiteral("T1")) != nullptr);
        QVERIFY(graph.getTerminal(QStringLiteral("T2")) != nullptr);
        QCOMPARE(terminal->getContainerCount(), 0);
        QCOMPARE(terminal->getTerminalExecutionResults(
                     QStringLiteral("exec-reset")).size(),
                 0);

        const QJsonObject state =
            terminal->getSystemDynamicsState()
                .value(QStringLiteral("state"))
                .toObject();
        QCOMPARE(state.value(QStringLiteral("arrivals_this_step")).toInt(),
                 0);
        QCOMPARE(state.value(QStringLiteral("departures_this_step")).toInt(),
                 0);
    }
};

QTEST_MAIN(TerminalActualsContractTest)
#include "test_terminal_actuals_contract.moc"
