#include "container_dwell_time.h"

#include <QDebug>
#include <random>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace TerminalSim {

// Get singleton random generator to ensure thread-safe
// random number generation
std::mt19937& ContainerDwellTime::getGenerator() {
    // Initialize with a good seed (system time)
    static std::mt19937 generator(
        static_cast<unsigned int>(
            std::chrono::system_clock::now().time_since_epoch().count()));
    return generator;
}

double
ContainerDwellTime::gammaDistributionDwellTime(double shape, double scale) {
    if (shape <= 0.0 || scale <= 0.0) {
        qWarning() << "Invalid parameters for gamma distribution: shape ="
                   << shape << ", scale =" << scale;
        throw std::invalid_argument("Shape and scale parameters must "
                                    "be positive for gamma distribution");
    }
    
    std::gamma_distribution<double> distribution(shape, scale);
    return distribution(getGenerator());
}

double ContainerDwellTime::exponentialDistributionDwellTime(double scale) {
    if (scale <= 0.0) {
        qWarning() << "Invalid parameter for exponential "
                      "distribution: scale ="
                   << scale;
        throw std::invalid_argument("Scale parameter must be positive for "
                                    "exponential distribution");
    }
    
    std::exponential_distribution<double> distribution(1.0 / scale);
    return distribution(getGenerator());
}

double
ContainerDwellTime::normalDistributionDwellTime(double mean, double stdDev) {
    if (stdDev <= 0.0) {
        qWarning() << "Invalid parameter for normal distribution: stdDev ="
                   << stdDev;
        throw std::invalid_argument("Standard deviation must be positive for"
                                    " normal distribution");
    }
    
    std::normal_distribution<double> distribution(mean, stdDev);
    
    // Ensure dwell time is non-negative (truncate distribution at zero)
    double result;
    do {
        result = distribution(getGenerator());
    } while (result < 0.0);
    
    return result;
}

double
ContainerDwellTime::lognormalDistributionDwellTime(double mean,
                                                   double sigma) {
    if (sigma <= 0.0) {
        qWarning() << "Invalid parameter for lognormal distribution: sigma ="
                   << sigma;
        throw std::invalid_argument("Sigma parameter must be positive for "
                                    "lognormal distribution");
    }
    
    std::lognormal_distribution<double> distribution(mean, sigma);
    return distribution(getGenerator());
}

double
ContainerDwellTime::getDepartureTime(double arrivalTime,
                                     const QString& method,
                                     const QVariantMap& params) {
    // Default values (about 2 days)
    const double defaultGammaShape =
        2.0;
    const double defaultGammaScale =
        24.0 * 3600.0;  // 24 hours in seconds
    
    const double defaultExpScale =
        2.0 * 24.0 * 3600.0;  // 2 days in seconds
    
    const double defaultNormalMean =
        2.0 * 24.0 * 3600.0;  // 2 days in seconds
    const double defaultNormalStdDev =
        0.5 * 24.0 * 3600.0;  // 0.5 days in seconds
    
    const double defaultLognormalMean =
        std::log(2.0 * 24.0 * 3600.0);  // log of 2 days in seconds
    const double defaultLognormalSigma =
        0.25;
    
    // Get dwell time based on specified distribution method
    double dwellTime = 0.0;
    
    if (method.compare("gamma", Qt::CaseInsensitive) == 0) {
        double shape = params.value("shape", defaultGammaShape).toDouble();
        double scale = params.value("scale", defaultGammaScale).toDouble();
        
        dwellTime = gammaDistributionDwellTime(shape, scale);
        
    } else if (method.compare("exponential", Qt::CaseInsensitive) == 0) {
        double scale = params.value("scale", defaultExpScale).toDouble();
        
        dwellTime = exponentialDistributionDwellTime(scale);
        
    } else if (method.compare("normal", Qt::CaseInsensitive) == 0) {
        double mean = params.value("mean", defaultNormalMean).toDouble();
        double stdDev = params.value("std_dev", defaultNormalStdDev).toDouble();
        
        dwellTime = normalDistributionDwellTime(mean, stdDev);
        
    } else if (method.compare("lognormal", Qt::CaseInsensitive) == 0) {
        double mean = params.value("mean", defaultLognormalMean).toDouble();
        double sigma = params.value("sigma", defaultLognormalSigma).toDouble();
        
        dwellTime = lognormalDistributionDwellTime(mean, sigma);
        
    } else {
        qWarning() << "Invalid distribution method:"
                   << method
                   << "- defaulting to gamma distribution";
        
        dwellTime =
            gammaDistributionDwellTime(defaultGammaShape, defaultGammaScale);
    }
    
    // Calculate departure time
    double departureTime = arrivalTime + dwellTime;
    
    qDebug() << "Container dwell time calculated:"
             << dwellTime/3600.0
             << "hours using method:"
             << method
             << "- Arrival time:"
             << arrivalTime
             << "- Departure time:"
             << departureTime;
    
    return departureTime;
}

} // namespace TerminalSim
