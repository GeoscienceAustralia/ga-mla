#include "seiscomp/geo/feature.h"
#include "seiscomp/math/geo.h"
#define SEISCOMP_COMPONENT EQNAMER

#include <seiscomp/config/config.h>
#include <seiscomp/config/strings.h>
#include <seiscomp/core/plugin.h>
#include <seiscomp/core/strings.h>
#include <seiscomp/datamodel/comment.h>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/datamodel/eventdescription.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/io/archive/xmlarchive.h>
#include <seiscomp/logging/log.h>
#include <seiscomp/math/coord.h>
#include <seiscomp/plugins/events/eventprocessor.h>
#include <seiscomp/processing/regions.h>
#include <seiscomp/utils/replace.h>

using Seiscomp::Client::EventProcessor;
using Seiscomp::Core::toString;
using Seiscomp::DataModel::Event;
using Seiscomp::DataModel::EventDescription;
using Seiscomp::DataModel::EventDescriptionIndex;
using Seiscomp::DataModel::Origin;
using Seiscomp::DataModel::OriginPtr;
using Seiscomp::DataModel::REGION_NAME;
using Seiscomp::Geo::GeoFeature;
using Seiscomp::IO::XMLArchive;
using Seiscomp::Math::Geo::CityD;
using Seiscomp::Processing::Regions;

ADD_SC_PLUGIN("Earthquake Namer", "Geoscience Australia", 0, 0, 1)

inline EventDescription * eventRegionDescription(Event *ev) {
    return ev->eventDescription(EventDescriptionIndex(REGION_NAME));
}
struct PoiResolver : public Seiscomp::Util::VariableResolver {
    PoiResolver(const double& dist, const std::string& dir, const std::string& name)
    : _dist(dist), _dir(dir), _name(name) {}

    bool resolve(std::string& variable) const {
        if ( VariableResolver::resolve(variable) )
            return true;

        if ( variable == "dist" )
            variable = toString(_dist);
        else if ( variable == "dir" )
            variable = _dir;
        else if ( variable == "poi" )
            variable = _name;
        else
            return false;

        return true;
    }

    const double& _dist;
    const std::string& _dir;
    const std::string& _name;
};

class EQNamer : public EventProcessor {
private:
    std::vector<CityD> _cities;
    const Regions* _regions;

    std::string nameEvent(Event *event) {
        OriginPtr o = Origin::Find(event->preferredOriginID());
        const double lat = o->latitude().value();
        const double lon = o->longitude().value();
        const GeoFeature* region = _regions->find(lat, lon);
        if (!region) {
            SEISCOMP_ERROR("EQNamer::process(%s): Could not find region for point %0.1f, %0.1f",
                           event->publicID().c_str(), lon, lat);

            return "Unknown Region";
        }
        if (region->name() == "null_value") {
            SEISCOMP_INFO("EQNamer::process(%s): Naming by nearest city", event->publicID().c_str());
            return nameByNearestCity(lat, lon);
        } else {
            SEISCOMP_INFO("EQNamer::process(%s): Naming by polygon", event->publicID().c_str());
            return region->name();
        }
    }

    std::string nameByNearestCity(double lat, double lon) {
	double dist, azi;
	const CityD* city = nearestCity(lat, lon, 9999999, 0, _cities, &dist, &azi);

	dist = (int)Seiscomp::Math::Geo::deg2km(dist);
	std::string dir;

	if ( azi < 22.5 || azi > 360.0-22.5 ) dir = "N";
	else if ( azi >= 22.5 && azi <= 90.0-22.5 ) dir = "NE";
	else if ( azi > 90.0-22.5 && azi < 90.0+22.5 ) dir = "E";
	else if ( azi >= 90.0+22.5 && azi <= 180.0-22.5 ) dir = "SE";
	else if ( azi > 180.0-22.5 && azi < 180.0+22.5 ) dir = "S";
	else if ( azi >= 180.0+22.5 && azi <= 270.0-22.5 ) dir = "SW";
	else if ( azi > 270.0-22.5 && azi < 270.0+22.5 ) dir = "W";
	else if ( azi >= 270.0+22.5 && azi <= 360.0-22.5 ) dir = "NW";
	else dir = "?";

        return Seiscomp::Util::replace("@dist@ km @dir@ from @poi@", PoiResolver(dist, dir, city->name()));
    }

public:
    EQNamer() {}
    bool setup(const Seiscomp::Config::Config &config) {
        XMLArchive ar;
        if (!ar.open("/home/anthony/bitbucket/neac-infrastructure/seiscomp/share/cities_au.xml")) {
            SEISCOMP_ERROR("EQNamer: Could not find cities.xml");
            return false;
        }
        ar >> NAMED_OBJECT("City", _cities);
        ar.close();

        std::sort(_cities.begin(), _cities.end(), [](CityD &x, CityD &y) {
            return x.population() < y.population();
        });

        SEISCOMP_INFO("EQNamer: loaded %d cities", _cities.size());

        _regions = Regions::load("/home/anthony/data/eqnames/polygons.geojson");
        SEISCOMP_INFO("EQNamer: loaded %d regions", _regions->featureSet.features().size());
        return true;
    }

    bool process(Event *event, const Journal &journal) {
        EventDescription* regionDesc = eventRegionDescription(event);
        if (regionDesc) {
            SEISCOMP_INFO("EQNamer::process(%s): existing region name is '%s'", event->publicID().c_str(), regionDesc->text().c_str());
        } else {
            SEISCOMP_INFO("EQNamer::process(%s): no existing region name", event->publicID().c_str());
            regionDesc = new EventDescription("", REGION_NAME);
            event->add(regionDesc);
        }
        const std::string name = nameEvent(event);
        SEISCOMP_INFO("EQNamer::process(%s): setting region name to '%s'",  event->publicID().c_str(), name);
        regionDesc->setText(name);
        return false;
    }
};

REGISTER_EVENTPROCESSOR(EQNamer, "EQNAMER");
