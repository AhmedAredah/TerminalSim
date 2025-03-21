#pragma once

#include <random>
#include <QString>
#include <QVariantMap>
#include <containerLib/container.h>

namespace TerminalSim {

/**
 * @brief Provides statistical distributions for container dwell time
 */
class ContainerDwellTime {
public:
    /**
     * @brief Generates a dwell time based on Gamma distribution
     * @param shape Shape parameter (k) for the Gamma distribution
     * @param scale Scale parameter (θ) for the Gamma distribution
     * @return Dwell time in seconds
     */
    static double gammaDistributionDwellTime(double shape, double scale);
    
    /**
     * @brief Generates a dwell time based on Exponential distribution
     * @param scale Mean dwell time (λ) for the Exponential distribution
     * @return Dwell time in seconds
     */
    static double exponentialDistributionDwellTime(double scale);
    
    /**
     * @brief Generates a dwell time based on Normal distribution
     * @param mean Mean dwell time
     * @param stdDev Standard deviation of the dwell time
     * @return Dwell time in seconds
     */
    static double normalDistributionDwellTime(double mean, double stdDev);
    
    /**
     * @brief Generates a dwell time based on Lognormal distribution
     * @param mean Mean of the underlying normal distribution
     * @param sigma Standard deviation of the underlying normal distribution
     * @return Dwell time in seconds
     */
    static double lognormalDistributionDwellTime(double mean, double sigma);
    
    /**
     * @brief Calculates departure time based on arrival time and dwell time
     * @param arrivalTime Time of arrival in seconds
     * @param method Distribution method
     *        ("gamma", "exponential", "normal", "lognormal")
     * @param params Additional parameters for the distribution
     * @return Departure time in seconds
     */
    static double getDepartureTime(double arrivalTime, const QString& method, 
                                  const QVariantMap& params);

private:
    // Random number generators
    static std::mt19937& getGenerator();
};

} // namespace TerminalSim
