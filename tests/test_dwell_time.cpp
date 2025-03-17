#include <QtTest>
#include "dwell_time/container_dwell_time.h"
#include <QObject>
#include <QVariantMap>
#include <QList>
#include <cmath>

namespace TerminalSim {

class TestDwellTime : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        // Initialize resources needed for all tests
        qDebug() << "Starting container dwell time tests";
    }

    void testGammaDistribution() {
        // Test the Gamma distribution with various parameters
        // We can't test for exact values since it's random, but we can check statistical properties

        // Parameters: shape=2.0, scale=24*3600 (24 hours in seconds)
        double shape = 2.0;
        double scale = 24.0 * 3600.0;

        // Generate multiple samples
        const int numSamples = 1000;
        QList<double> samples;

        for (int i = 0; i < numSamples; ++i) {
            double value = ContainerDwellTime::gammaDistributionDwellTime(shape, scale);
            samples.append(value);

            // Each sample should be positive
            QVERIFY(value > 0.0);
        }

        // Calculate mean and check if it's close to the expected value
        // Expected mean of gamma distribution = shape * scale
        double expectedMean = shape * scale;
        double sum = 0.0;
        for (double sample : samples) {
            sum += sample;
        }
        double actualMean = sum / numSamples;

        // Allow for statistical variation (within 20% of expected mean)
        QVERIFY(std::abs(actualMean - expectedMean) < 0.2 * expectedMean);
    }

    void testExponentialDistribution() {
        // Test the Exponential distribution

        // Parameter: scale=2*24*3600 (2 days in seconds)
        double scale = 2.0 * 24.0 * 3600.0;

        // Generate multiple samples
        const int numSamples = 1000;
        QList<double> samples;

        for (int i = 0; i < numSamples; ++i) {
            double value = ContainerDwellTime::exponentialDistributionDwellTime(scale);
            samples.append(value);

            // Each sample should be positive
            QVERIFY(value > 0.0);
        }

        // Calculate mean and check if it's close to the expected value
        // Expected mean of exponential distribution = scale
        double expectedMean = scale;
        double sum = 0.0;
        for (double sample : samples) {
            sum += sample;
        }
        double actualMean = sum / numSamples;

        // Allow for statistical variation (within 20% of expected mean)
        QVERIFY(std::abs(actualMean - expectedMean) < 0.2 * expectedMean);
    }

    void testNormalDistribution() {
        // Test the Normal distribution

        // Parameters: mean=2*24*3600 (2 days in seconds), stdDev=0.5*24*3600 (12 hours in seconds)
        double mean = 2.0 * 24.0 * 3600.0;
        double stdDev = 0.5 * 24.0 * 3600.0;

        // Generate multiple samples
        const int numSamples = 1000;
        QList<double> samples;

        for (int i = 0; i < numSamples; ++i) {
            double value = ContainerDwellTime::normalDistributionDwellTime(mean, stdDev);
            samples.append(value);

            // Each sample should be non-negative
            QVERIFY(value >= 0.0);
        }

        // Calculate mean and check if it's close to the expected value
        double sum = 0.0;
        for (double sample : samples) {
            sum += sample;
        }
        double actualMean = sum / numSamples;

        // Allow for statistical variation (within 20% of expected mean)
        // Note: The actual mean might be slightly higher than the specified mean
        // because we truncate negative values at zero
        QVERIFY(std::abs(actualMean - mean) < 0.2 * mean);
    }

    void testLognormalDistribution() {
        // Test the Lognormal distribution

        // Parameters: mean=log(2*24*3600), sigma=0.25
        double meanParam = std::log(2.0 * 24.0 * 3600.0);
        double sigmaParam = 0.25;

        // Generate multiple samples
        const int numSamples = 1000;
        QList<double> samples;

        for (int i = 0; i < numSamples; ++i) {
            double value = ContainerDwellTime::lognormalDistributionDwellTime(meanParam, sigmaParam);
            samples.append(value);

            // Each sample should be positive
            QVERIFY(value > 0.0);
        }

        // Calculate mean and check if it's close to the expected value
        // Expected mean of lognormal distribution = exp(meanParam + sigmaParam^2/2)
        double expectedMean = std::exp(meanParam + sigmaParam * sigmaParam / 2.0);
        double sum = 0.0;
        for (double sample : samples) {
            sum += sample;
        }
        double actualMean = sum / numSamples;

        // Allow for statistical variation (within 25% of expected mean)
        // Lognormal distribution can have higher variance
        QVERIFY(std::abs(actualMean - expectedMean) < 0.25 * expectedMean);
    }

    void testGetDepartureTime() {
        // Test the departure time calculation

        double arrivalTime = 1000.0; // 1000 seconds since epoch

        // Test with Gamma distribution
        QVariantMap gammaParams;
        gammaParams["shape"] = 2.0;
        gammaParams["scale"] = 3600.0; // 1 hour in seconds

        double gammaDeparture = ContainerDwellTime::getDepartureTime(arrivalTime, "gamma", gammaParams);

        // Departure time should be after arrival time
        QVERIFY(gammaDeparture > arrivalTime);

        // Test with normal distribution
        QVariantMap normalParams;
        normalParams["mean"] = 7200.0; // 2 hours in seconds
        normalParams["std_dev"] = 1800.0; // 30 minutes in seconds

        double normalDeparture = ContainerDwellTime::getDepartureTime(arrivalTime, "normal", normalParams);

        // Departure time should be after arrival time
        QVERIFY(normalDeparture > arrivalTime);

        // Test with invalid distribution name (should fall back to gamma)
        double invalidDeparture = ContainerDwellTime::getDepartureTime(arrivalTime, "invalid_name", gammaParams);

        // Should still produce a valid departure time
        QVERIFY(invalidDeparture > arrivalTime);
    }

    void testParameterValidation() {
        // Test parameter validation for distributions

        // Gamma distribution with invalid parameters
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 ContainerDwellTime::gammaDistributionDwellTime(-1.0, 3600.0)
                                 );

        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 ContainerDwellTime::gammaDistributionDwellTime(2.0, -3600.0)
                                 );

        // Exponential distribution with invalid parameters
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 ContainerDwellTime::exponentialDistributionDwellTime(-3600.0)
                                 );

        // Normal distribution with invalid parameters
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 ContainerDwellTime::normalDistributionDwellTime(3600.0, -1800.0)
                                 );

        // Lognormal distribution with invalid parameters
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 ContainerDwellTime::lognormalDistributionDwellTime(std::log(3600.0), -0.25)
                                 );
    }

    void testDefaultParameters() {
        // Test departure time calculation with default parameters

        double arrivalTime = 1000.0;

        // Call without parameters should still work
        double departure = ContainerDwellTime::getDepartureTime(arrivalTime, "gamma", QVariantMap());

        // Departure time should be after arrival time
        QVERIFY(departure > arrivalTime);
    }

    void cleanupTestCase() {
        // Clean up resources used by tests
        qDebug() << "Container dwell time tests completed";
    }
};

} // namespace TerminalSim

QTEST_MAIN(TerminalSim::TestDwellTime)
#include "test_dwell_time.moc"
