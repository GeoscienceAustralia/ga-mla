/*
 * File:   mwpa.h
 */

#ifndef __MWPA_PLUGIN_H__
#define __MWPA_PLUGIN_H__

#define GA_MWP_AUS_MAG_TYPE "Mwpa"

#ifdef __MLA_SC3__
// SeisComP 3 includes
#include <seiscomp3/processing/magnitudeprocessor.h>
#include <seiscomp3/processing/magnitudes/Mwp.h>
#include <seiscomp3/core/plugin.h>
#else
// SeisComP >=4 includes
#include <seiscomp/processing/magnitudeprocessor.h>
#include <seiscomp/processing/magnitudes/Mwp.h>
#include <seiscomp/core/plugin.h>
#endif

#include <string>
#include <map>


class MagnitudeProcessor_Mwpa : public Seiscomp::Processing::MagnitudeProcessor_Mwp
{
	DECLARE_SC_CLASS(MagnitudeProcessor_Mwpa)

	public:
		MagnitudeProcessor_Mwpa();

		std::string amplitudeType() const override;
		Status estimateMw(double magnitude, double &Mw_estimate,
				double &Mw_stdError) override;
};

#endif /* __MWPA_PLUGIN_H__ */
