#define SEISCOMP_COMPONENT MLa

#include "mla.h"

#include <seiscomp/logging/log.h>
#include <seiscomp/geo/feature.h>
#include <seiscomp/datamodel/databasequery.h>
#include <seiscomp/datamodel/object.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/math/geo.h>
#include <seiscomp/client/application.h>

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

#define _T(name) (_db->convertColumnName(name))

// DatabaseQuery::queryObject is protected so this is the only way to use it
class MyQuery : public Seiscomp::DataModel::DatabaseQuery {
public:
    MyQuery(Seiscomp::IO::DatabaseInterface* i) : Seiscomp::DataModel::DatabaseQuery(i) {}
    Seiscomp::DataModel::Magnitude* getMLaForEvent(std::string evid) {
        std::string escaped_evid;
        if (!_db->escape(escaped_evid, evid)) {
            SEISCOMP_ERROR("Error escaping event ID '%s'", evid.c_str());
            return nullptr;
        }

        // Find the most recent MLa for this event
        const std::string q =
            "select PMagnitude." + _T("publicID") + ",Magnitude.*"
            " from Magnitude,PublicObject as PMagnitude,Origin,PublicObject as POrigin,Event,PublicObject as PEvent,OriginReference"
            " where Magnitude." + _T("type") + " = 'MLa'"
            " and Magnitude._parent_oid=Origin._oid"
            " and OriginReference." + _T("originID") + " = POrigin." + _T("publicID") +
            " and OriginReference._parent_oid=Event._oid"
            " and PEvent." + _T("publicID") + " = '" + escaped_evid + "'" +
            " and Magnitude._oid=PMagnitude._oid"
            " and Origin._oid=POrigin._oid"
            " and Event._oid=PEvent._oid"
            " order by Origin." + _T("creationInfo_creationTime") + " desc limit 1";

        const auto magnitude = Seiscomp::DataModel::Magnitude::Cast(
            queryObject(Seiscomp::DataModel::Magnitude::TypeInfo(), q)
        );

        if (!magnitude) {
            SEISCOMP_DEBUG("No existing MLa magnitude found for %s", escaped_evid.c_str());
        }

        return magnitude;
    }
};

#undef _T

// Register the amplitude processor.
IMPLEMENT_SC_CLASS_DERIVED(Amplitude_MLA, AmplitudeProcessor, "Amplitude_MLA");
REGISTER_AMPLITUDEPROCESSOR(Amplitude_MLA, GA_ML_AUS_AMP_TYPE);

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//  MLa AMPLITUDE PROCESSOR.

Amplitude_MLA::Amplitude_MLA(const std::string& type)
    : Seiscomp::Processing::AmplitudeProcessor_MLv()
{
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
        SEISCOMP_DEBUG("Initializing %s with default filter %s", _type.c_str(), filterString.c_str());
        _defaultPreFilter = filterString; // ML has built-in prefiltering; just turn it on
    } else {
        SEISCOMP_DEBUG("Initializing %s with no filter", _type.c_str());
    }

    double maxDist;
    if ( settings.getValue(maxDist, "amplitudes." + _type + ".maxDist") ) {
        setMaxDist(maxDist);
    } else {
        setMaxDist(11);
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

std::string Amplitude_MLA::chooseFilter()
{
    // If database access is available, we attempt to find a previously computed MLa
    // magnitude for the same event and use its value to select an appropriate
    // prefilter.
    const auto origin = environment().hypocenter;
    const auto df = _defaultPreFilter.c_str();
    if (!origin) {
        SEISCOMP_DEBUG("No origin in environment, using default filter %s", df);
        return df;
    }
    if (!SCCoreApp) {
        SEISCOMP_DEBUG("No SCCoreApp available for database access, using default filter %s", df);
        return df;
    }
    const auto db = SCCoreApp->database();
    if (!db) {
        SEISCOMP_DEBUG("No SCCoreApp->database() available for database access, using default filter %s", df);
        return df;
    }
    MyQuery query{db};

    const std::string originID = origin->publicID();
    const auto event = query.getEvent(originID);
    if (!event) {
        SEISCOMP_DEBUG("No event found for origin %s, using default filter %s", originID.c_str(), df);
        return df;
    }

    const std::string evid = event->publicID();
    const auto magnitude = query.getMLaForEvent(evid);
    if (!magnitude) {
        SEISCOMP_DEBUG("Could not find MLa for event %s, using default filter %s", evid.c_str(), df);
        return df;
    }

    const auto magvalue = magnitude->magnitude().value();
    SEISCOMP_DEBUG("Found existing MLa magnitude %s with value %f", magnitude->publicID().c_str(), magvalue);

    if (magvalue < 4) {
        return "BW_HP(3, 0.75)";
    } else if (magvalue < 6) {
        return "BW_HP(3, 0.5)";
    } else {
        return "BW_HP(3, 0.1)";
    }
}

bool Amplitude_MLA::computeAmplitude(const Seiscomp::DoubleArray &data,
        size_t i1, size_t i2,
        size_t si1, size_t si2,
        double offset,
        AmplitudeIndex *dt, AmplitudeValue *amplitude,
        double *period, double *snr)
{
    _preFilter = chooseFilter();
    SEISCOMP_DEBUG("Chose MLa prefilter %s", _preFilter.c_str());

    bool retVal = Seiscomp::Processing::AmplitudeProcessor_MLv::computeAmplitude(
        data,
        i1, i2,
        si1, si2,
        offset,
        dt, amplitude,
        period, snr);

    _preFilter = _defaultPreFilter;

    // If the base class calculation succeeded, divide the amplitude value
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
    : Seiscomp::Processing::MagnitudeProcessor(type)
    , _minSNR(2) {}

bool Magnitude_MLA::setup(const Seiscomp::Processing::Settings &settings)
{
    if (!MagnitudeProcessor::setup(settings)) {
        return false;
    }

    _minSNR = 2;

    std::string prefix = std::string("magnitudes.") + type() + ".";

    try { _minSNR = settings.getDouble(prefix + "minSNR"); }
    catch ( ... ) {}

    return true;
}

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
    // _validValue is returned when treatAsValidMagnitude() is called, which is a
    // follow-up check performed only when the returned status is not OK. We set this
    // flag if we're returning non-OK but want the stamag to still be created (just
    // marked as failed QC).
    _validValue = false;

    if ( amplitudeValue <= 0 )
	    return AmplitudeOutOfRange;    
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

    Seiscomp::Processing::MagnitudeProcessor::Status status = (this->*calcFunction)(amplitudeValue, period, delta, depth, value);

    if ( snr < _minSNR ) {
        // magtool logic is as follows:
        // 1. If status == OK, accept station magnitude with passedQC = true
        // 2. If status != OK but treatAsValidMagnitude(), accept station magnitude with passedQC = false
        // 3. If status != OK and !treatAsValidMagnitude(), exclude station magnitude entirely
        // When SNR check fails we want option 2, so we set the _validValue flag and return SNROutOfRange.
        status = SNROutOfRange;
        _validValue = true;
        SEISCOMP_DEBUG("%s SNR = %.1f is less than minSNR = %.1f.", type(), snr, _minSNR);
    } else {
        SEISCOMP_DEBUG("%s SNR = %.1f is greater than minSNR = %.1f.", type(), snr, _minSNR);
    }

    return status;
}

bool Magnitude_MLA::treatAsValidMagnitude() const {
    return _validValue;
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
