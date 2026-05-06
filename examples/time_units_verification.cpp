// Post-unification verification for customs-seconds and deltaT-seconds.
// See docs/superpowers/plans/2026-04-17-time-unit-unification.md.
//
// Build: cmake --build build --target time_units_verification
// Run:   ./time_units_verification
// Pass:  stdout ends with "ALL CHECKS PASSED".

#include "terminal/terminal.h"
#include "common/common.h"

#include <QCoreApplication>
#include <iostream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <containerLib/container.h>

#include <cmath>
#include <cstdlib>

using namespace TerminalSim;

static int g_failures = 0;

static void check(bool ok, const std::string& what, double expected, double actual)
{
    if (ok) {
        std::cout << "[PASS] " << what
                  << " expected=" << expected
                  << " actual="   << actual
                  << std::endl;
    } else {
        std::cerr << "[FAIL] " << what
                  << " expected=" << expected
                  << " actual="   << actual
                  << std::endl;
        ++g_failures;
    }
}

// Extract the noHauler "time" custom variable from the first container held
// by the terminal. The custom variables are serialised under the integer key
// for HaulerType; noHauler = 4 (see containerLib/container.h).
static double extractNoHaulerTime(const QJsonArray& containers)
{
    if (containers.isEmpty()) {
        return std::nan("");
    }
    const QJsonObject container = containers.first().toObject();
    const QJsonObject customVars =
        container.value(QStringLiteral("customVariables")).toObject();
    const QString noHaulerKey =
        QString::number(static_cast<int>(
            ContainerCore::Container::HaulerType::noHauler));
    const QJsonObject haulerVars = customVars.value(noHaulerKey).toObject();
    const QJsonValue timeValue = haulerVars.value(QStringLiteral("time"));
    return timeValue.isDouble() ? timeValue.toDouble() : std::nan("");
}

// Check 1: customs.delay_mean in seconds produces a seconds-scale delay.
// With probability = 1.0 and variance = 0 (stdDev fallback = 1 second),
// a container arriving at t=0 with mean=3600 should accumulate ~3600 seconds
// of terminal time (plus a tiny noise from the fallback stdDev).
static void check_customs_seconds_scale()
{
    QMap<TerminalInterface, QSet<TransportationMode>> interfaces{
        {TerminalInterface::LAND_SIDE, {TransportationMode::Truck}}
    };

    QVariantMap customs;
    customs["probability"]     = 1.0;
    customs["delay_mean"]      = 3600.0;  // seconds (post-unification)
    customs["delay_variance"]  = 0.0;     // seconds^2

    Terminal t("TestA", "TestA",
               interfaces,
               /*modeNetworkAliases=*/{},
               /*capacity=*/{},
               /*dwellTime=*/{},
               customs,
               /*cost=*/{},
               /*systemDynamics=*/{},
               /*pathToTerminalFolder=*/QString());

    ContainerCore::Container c;
    c.setContainerID("C1");

    t.addContainer(c, /*addingTime=*/0.0, TransportationMode::Truck);

    // Ask for every container whose departure time is >= 0 (i.e. all of them).
    const QJsonArray held = t.getContainersByDepatingTime(0.0, ">=");
    const double totalTime = extractNoHaulerTime(held);

    // Tolerance of 10 seconds accommodates the stdDev=1.0 fallback noise.
    const bool ok = std::isfinite(totalTime)
                 && std::fabs(totalTime - 3600.0) < 10.0;
    check(ok, "customs.delay_mean=3600s yields ~3600s dwell",
          3600.0, totalTime);
}

// Check 2: updateSystemDynamics(currentTime=0, deltaT=3600) with
// maxServiceRate=100 TEU/hour should produce totalCapacityThisStep = 100 TEU
// (not 360000 TEU as would happen if deltaT were still multiplied directly
// without the /3600 bridge).
static void check_delta_t_seconds_scale()
{
    QMap<TerminalInterface, QSet<TransportationMode>> interfaces{
        {TerminalInterface::LAND_SIDE, {TransportationMode::Truck}}
    };

    QVariantMap sd;
    sd["enabled"]              = true;
    sd["max_service_rate"]     = 100.0;  // TEU/hour
    sd["critical_utilization"] = 0.0;    // any inventory engages congestion
                                         // (here inventory is zero anyway)

    Terminal t("TestB", "TestB",
               interfaces,
               /*modeNetworkAliases=*/{},
               /*capacity=*/{},
               /*dwellTime=*/{},
               /*customs=*/{},
               /*cost=*/{},
               sd,
               /*pathToTerminalFolder=*/QString());

    t.updateSystemDynamics(/*currentTime=*/0.0, /*deltaT=*/3600.0);

    const int remaining = t.getRemainingServiceCapacity();

    // Expected after deltaT=3600s (= 1 hour) with rate 100 TEU/hr: 100 TEU.
    // Tolerance of +/- 1 absorbs integer truncation inside capacityThisStep().
    const bool ok = std::abs(remaining - 100) <= 1;
    check(ok, "deltaT=3600s x 100 TEU/h => 100 TEU capacity",
          100.0, static_cast<double>(remaining));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    check_customs_seconds_scale();
    check_delta_t_seconds_scale();

    if (g_failures == 0) {
        std::cout << "ALL CHECKS PASSED" << std::endl;
        return EXIT_SUCCESS;
    }
    std::cerr << g_failures << " check(s) FAILED" << std::endl;
    return EXIT_FAILURE;
}
