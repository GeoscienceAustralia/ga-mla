#define SEISCOMP_COMPONENT MLa

#include "mwpa.h"

#include <vector>
#include <string>
#include <math.h>

ADD_SC_PLUGIN(
		("Mwpa magnitude"), "Geoscience Australia", 0, 0, 1
);

IMPLEMENT_SC_CLASS_DERIVED(MagnitudeProcessor_Mwpa, Seiscomp::Processing::MagnitudeProcessor_Mwp, "MagnitudeProcessor_Mwpa");
REGISTER_MAGNITUDEPROCESSOR(MagnitudeProcessor_Mwpa, GA_MWP_AUS_MAG_TYPE);

// MagnitudeProcessor_Mwp constructor doesn't allow providing the type string,
// so we have this awful hack:
MagnitudeProcessor_Mwpa::MagnitudeProcessor_Mwpa()
	: MagnitudeProcessor_Mwp()
{
	_type = GA_MWP_AUS_MAG_TYPE;
}

// Since the only change from standard Mwp is the Mw estimation, we can just
// reuse the Mwp amplitudes:
std::string MagnitudeProcessor_Mwpa::amplitudeType() const {
	return "Mwp";
}

Seiscomp::Processing::MagnitudeProcessor::Status MagnitudeProcessor_Mwpa::estimateMw(
		double magnitude,
		double &Mw_estimate,
		double &Mw_stdError)
{
	// from linear regression of GA catalogue:
	const double a = 0.7, b = 1.65;
	Mw_estimate = a * magnitude + b;

	// sadly, we don't have any uncertainty information for the Mwp input, so
	// we can't give a reasonable uncertainty for the output.
	// The Mwp->Mw regression alone has stddev of 0.12.
	// We just use the same kludge as Mw(Mwp):
	Mw_stdError = 0.4;

	return Seiscomp::Processing::MagnitudeProcessor::OK;
}
