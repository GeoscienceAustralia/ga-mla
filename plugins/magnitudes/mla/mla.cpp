#define SEISCOMP_COMPONENT MLa

#include "mla.h"

#include <seiscomp/logging/log.h>
#include <seiscomp/geo/feature.h>
#include <seiscomp/math/geo.h>

#include <vector>
#include <string>
#include <math.h>

/*
Maps the name of a region to the member function which is used to
calculate the magnitude for that region.
*/
std::map<std::string, Magnitude_MLA::MagCalc> Magnitude_MLA::regionToCalcMap {
    {std::string("West"), &Magnitude_MLA::computeMagWest},
    {std::string("East"), &Magnitude_MLA::computeMagEast},
    {std::string("South"), &Magnitude_MLA::computeMagSouth},
};

ADD_SC_PLUGIN(
        ( "MLa magnitude. Calculates magnitude based on universal formulae "
        "MLa=c0_log10(Amp)+c1*log10(delta*c3+c4)+c5*(delta+c6), "
        "where coefficients c1...6 vary based on epicentral location."),
        "Geoscience Australia", 0, 0, 2)

// Register the amplitude processor.
IMPLEMENT_SC_CLASS_DERIVED(Amplitude_MLA, AmplitudeProcessor, "Amplitude_MLA");
REGISTER_AMPLITUDEPROCESSOR(Amplitude_MLA, GA_ML_AUS_AMP_TYPE);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  MLa AMPLITUDE PROCESSOR.

Amplitude_MLA::Amplitude_MLA(const std::string& type)
    : Seiscomp::Processing::AmplitudeProcessor_MLv()
{
    // Change max distance to 11 degrees.
    setMaxDist(11);
    this->_type = type;
}

Amplitude_MLA::Amplitude_MLA(const Seiscomp::Core::Time& trigger, const std::string& type)
    : Seiscomp::Processing::AmplitudeProcessor_MLv(trigger)
{
    // Change max distance to 11 degrees.
    setMaxDist(11);
    this->_type = type;
}

bool Amplitude_MLA::setup(const Seiscomp::Processing::Settings &settings)
{
    if ( !AmplitudeProcessor_MLv::setup(settings) ) {
        return false;
    }

    std::string filterString;
    try {
        std::string cfgName = "amplitudes." + _type + ".filter";
        filterString = settings.getString(cfgName);
    }
    catch(...) {
        filterString = defaultFilter();
    }

    if (!filterString.empty()) {
        SEISCOMP_DEBUG("Initializing %s with filter %s", _type.c_str(), filterString.c_str());
        _preFilter = filterString; // ML has built-in prefiltering; just turn it on
    } else {
        SEISCOMP_DEBUG("Initializing %s with no filter", _type.c_str());
    }

    return true;
}

int Amplitude_MLA::capabilities() const
{
    // To get the correct calculation, we need to ensure the base MLv class
    // uses the absolute maximum calculation option it has. However, MLv can be
    // configured to use another option, through the use of capabilities. To
    // stop the MLa from being configured, change all the capabilities so that
    // it doesn't allow for this processor to be configured.
    return NoCapability;
}

Seiscomp::Processing::AmplitudeProcessor::IDList
    Amplitude_MLA::capabilityParameters(Capability cap) const
{
    // To get the correct calculation, we need to ensure the base MLv class
    // uses the absolute maximum calculation option it has. However, MLv can be
    // configured to use another option, through the use of capabilities. To
    // stop the MLa from being configured, change all the capabilities so that
    // it doesn't allow for this processor to be configured.
    return Seiscomp::Processing::AmplitudeProcessor::IDList();
}

bool Amplitude_MLA::setParameter(Capability cap, const std::string &value)
{
    // To get the correct calculation, we need to ensure the base MLv class
    // uses the absolute maximum calculation option it has. However, MLv can be
    // configured to use another option, through the use of capabilities. To
    // stop the MLa from being configured, change all the capabilities so that
    // it doesn't allow for this processor to be configured.
    return false;
}

bool Amplitude_MLA::computeAmplitude(const Seiscomp::DoubleArray &data,
        size_t i1, size_t i2,
        size_t si1, size_t si2,
        double offset,
        AmplitudeIndex *dt, AmplitudeValue *amplitude,
        double *period, double *snr)
{
    bool retVal = Seiscomp::Processing::AmplitudeProcessor_MLv::computeAmplitude(
        data,
        i1, i2,
        si1, si2,
        offset,
        dt, amplitude,
        period, snr);

    // If the base class calculation was correct, divide the amplitude value
    // by half to get the zero to peak value.
    if (retVal)
    {
        amplitude->value *= 0.5;
    }

    return retVal;
}


// END MLa AMPLITUDE PROCESSOR
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  MLa MAGNITUDE PROCESSOR.

// Register the magnitude processor.
REGISTER_MAGNITUDEPROCESSOR(Magnitude_MLA, GA_ML_AUS_MAG_TYPE);

Magnitude_MLA::Magnitude_MLA(const std::string& type)
    : Seiscomp::Processing::MagnitudeProcessor(type) {}

std::string Magnitude_MLA::amplitudeType() const
{
    return GA_ML_AUS_AMP_TYPE;
}

Seiscomp::Processing::MagnitudeProcessor::Status Magnitude_MLA::computeMagnitude(
      double amplitudeValue, // in millimetres
      const std::string &unit,
      double period,         // in seconds
      double snr,
      double delta,          // in degrees
      double depth,          // in kilometres
      const Seiscomp::DataModel::Origin *hypocenter,
      const Seiscomp::DataModel::SensorLocation *receiver,
      const Seiscomp::DataModel::Amplitude *amplitude,
      const Seiscomp::Processing::MagnitudeProcessor::Locale *locale,
      double &value)
{
    // The calculation used depends on which of the three MLa regions the
    // origin falls within. Thus you must use this magnitude processor with a
    // region file containing regions named West, East and North.
    if (!locale) {
        SEISCOMP_INFO("Hypocenter not in any MLa region");
        return DistanceOutOfRange;
    }

    MagCalc calcFunction;
    try {
        calcFunction = regionToCalcMap.at(locale->name);
    }
    catch(...) {
        SEISCOMP_ERROR("Unknown MLa region name %s", locale->name.c_str());
        return DistanceOutOfRange;
    }
    return (this->*calcFunction)(amplitudeValue, period, delta, depth, value);
}

double Magnitude_MLA::distance(double delta, double depth)
{
    double deltaKms = Seiscomp::Math::Geo::deg2km(delta);
    return sqrt(pow(depth, 2) + pow(deltaKms, 2));
}

// Calculates the ml magnitude for the west region (Western Australia).
// @param amplitude: Amplitude of the seismic event (in millimetres).
// @param period: (in seconds).
// @param delta: (in degrees).
// @param depth: the depth of the epicentre of the event (in kms).
// @param value: The result of the calculation.
Seiscomp::Processing::MagnitudeProcessor::Status Magnitude_MLA::computeMagWest(
      double amplitude,   // in millimetres
      double period,      // in seconds
      double delta,       // in degrees
      double depth,       // in kilometres
      double &value)
{
    double r = Magnitude_MLA::distance(delta, depth);
    value = (
            log10(amplitude) + (1.137 * log10(r)) +
            (0.000657 * r) + 0.66);
    return OK;
}

// Calculates the ml magnitude for the east region (Eastern Australia).
// @param amplitude: Amplitude of the seismic event (in millimetres).
// @param period: (in seconds).
// @param delta: (in degrees).
// @param depth: the depth of the epicentre of the event (in kms).
// @param value: The result of the calculation.
Seiscomp::Processing::MagnitudeProcessor::Status Magnitude_MLA::computeMagEast(
      double amplitude,   // in millimetres
      double period,      // in seconds
      double delta,       // in degrees
      double depth,       // in kilometres
      double &value)
{
    double r = Magnitude_MLA::distance(delta, depth);
    value = (
            log10(amplitude) + (1.34 * log10(r / 100)) +
            (0.00055 * (r - 100)) + 3.13);
    return OK;
}

// Calculates the ml magnitude for the south region (Flinders Ranges).
// @param amplitude: Amplitude of the seismic event (in millimetres).
// @param period: (in seconds).
// @param delta: (in degrees).
// @param depth: the depth of the epicentre of the event (in kms).
// @param value: The result of the calculation.
Seiscomp::Processing::MagnitudeProcessor::Status Magnitude_MLA::computeMagSouth(
      double amplitude,   // in millimetres
      double period,      // in seconds
      double delta,       // in degrees
      double depth,       // in kilometres
      double &value)
{
    double r = Magnitude_MLA::distance(delta, depth);
    value = (
            log10(amplitude) + (1.1 * log10(r)) +
            (0.0013 * r) + 0.7);
    return OK;
}

// END MLa MAGNITUDE PROCESSOR
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
