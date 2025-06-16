#define SEISCOMP_COMPONENT EQNAMER

#include <seiscomp/config/config.h>
#include <seiscomp/config/strings.h>
#include <seiscomp/core/exceptions.h>
#include <seiscomp/core/plugin.h>
#include <seiscomp/core/strings.h>
#include <seiscomp/datamodel/comment.h>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/datamodel/eventdescription.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/types.h>
#include <seiscomp/geo/feature.h>
#include <seiscomp/geo/featureset.h>
#include <seiscomp/io/archive/xmlarchive.h>
#include <seiscomp/logging/log.h>
#include <seiscomp/math/coord.h>
#include <seiscomp/math/geo.h>
#include <seiscomp/plugins/events/eventprocessor.h>
#include <seiscomp/processing/regions.h>
#include <seiscomp/system/environment.h>
#include <seiscomp/utils/replace.h>

#include <algorithm>
#include <cmath>

using Seiscomp::Environment;
using Seiscomp::Core::toString;
using Seiscomp::DataModel::Comment;
using Seiscomp::DataModel::Event;
using Seiscomp::DataModel::EventDescription;
using Seiscomp::DataModel::EventDescriptionIndex;
using Seiscomp::DataModel::FINAL;
using Seiscomp::DataModel::Magnitude;
using Seiscomp::DataModel::Origin;
using Seiscomp::DataModel::OriginPtr;
using Seiscomp::DataModel::REGION_NAME;
using Seiscomp::DataModel::REVIEWED;
using Seiscomp::Geo::GeoFeature;
using Seiscomp::IO::XMLArchive;
using Seiscomp::Math::Geo::CityD;
using Seiscomp::Processing::Regions;

ADD_SC_PLUGIN("Earthquake Namer", "Anthony Carapetis <anthony.carapetis@ga.gov.au>", 0, 0, 2)

struct CityRel {
    double distDeg;
    double azi;
    std::string name;
};

struct Resolver : public Seiscomp::Util::VariableResolver {
    const double& _dist;
    const std::string& _name;
    std::string _dir;

    Resolver(const double& dist, const double& azi, const std::string& name)
        : _dist(dist)
        , _name(name)
    {
        if (azi < 22.5 || azi > 360.0 - 22.5)
            _dir = "N";
        else if (azi >= 22.5 && azi <= 90.0 - 22.5)
            _dir = "NE";
        else if (azi > 90.0 - 22.5 && azi < 90.0 + 22.5)
            _dir = "E";
        else if (azi >= 90.0 + 22.5 && azi <= 180.0 - 22.5)
            _dir = "SE";
        else if (azi > 180.0 - 22.5 && azi < 180.0 + 22.5)
            _dir = "S";
        else if (azi >= 180.0 + 22.5 && azi <= 270.0 - 22.5)
            _dir = "SW";
        else if (azi > 270.0 - 22.5 && azi < 270.0 + 22.5)
            _dir = "W";
        else if (azi >= 270.0 + 22.5 && azi <= 360.0 - 22.5)
            _dir = "NW";
        else
            _dir = "?";
    }

    bool resolve(std::string& variable) const
    {
        if (VariableResolver::resolve(variable))
            return true;

        if (variable == "dist")
            variable = toString(std::round(_dist));
        else if (variable == "dir")
            variable = _dir;
        else if (variable == "poi")
            variable = _name;
        else
            return false;

        return true;
    }
};

const std::string getAttr(const GeoFeature& f, const std::string& key) {
    const auto& attrs = f.attributes();
    auto it = attrs.find(key);
    if (it != attrs.end()) {
        return it->second;
    }
    return "";
}

std::string crustTypePrefix(const GeoFeature& f) {
    const std::string t = getAttr(f, "Crust_Type");
    if (t == "Coastal") {
        return "Coastal, ";
    } else if (t == "Oceanic") {
        return "Offshore, ";
    } else {
        return "";
    }
}

const std::string getFeatureName(const GeoFeature& f) {
    const auto& attrs = f.attributes();
    auto it = attrs.find("Primary_ID");
    if (it == attrs.end()) {
        it = attrs.find("name");
    }
    if (it != attrs.end()) {
        return (*it).second;
    }
    return "";
}

class EQNamer : public Seiscomp::Client::EventProcessor {
protected:
    std::vector<CityD> _cities;
    Regions _staticRegions;
    Regions _dynamicRegions;
    std::string _approximateMessage;
    std::string _preciseMessage;

    std::string nameEvent(Event* event)
    {
        OriginPtr o = Origin::Find(event->preferredOriginID());
        return nameOrigin(o.get(), event->publicID().c_str());
    }

    std::string nameOrigin(Origin* o, const char* const evid)
    {
        const double lat = o->latitude().value();
        const double lon = o->longitude().value();

        if (const auto f = _dynamicRegions.find(lat, lon)) {
            bool precise;
            std::string statusStr;
            try {
                const auto status = o->evaluationStatus();
                precise = status == REVIEWED || status == FINAL;
                statusStr = status.toString();
            } catch (...) {
                precise = false;
                statusStr = "blank";
            }
            SEISCOMP_INFO(
                "EQNamer::process(%s): Status is %s, naming by nearest city with precise=%s", evid,
                statusStr, precise ? "true" : "false");
            return crustTypePrefix(*f) + nameByNearestCity(lat, lon, precise);
        }

        SEISCOMP_INFO("EQNamer::process(%s): Naming by polygon", evid);
        if (auto region = _staticRegions.find(lat, lon)) {
            return getFeatureName(*region);
        } else {
            SEISCOMP_ERROR(
                "EQNamer::process(%s): No polygon containing %0.1f, %0.1f", evid, lon, lat);

            return "Unknown Region";
        }
    }

    std::string nearbyCitiesString(Event* event, size_t count = 4)
    {
        OriginPtr o = Origin::Find(event->preferredOriginID());
        const double lat = o->latitude().value();
        const double lon = o->longitude().value();

        std::vector<CityRel> rels;
        rels.reserve(_cities.size());

        for (CityD city : _cities) {
            double dist, azi1, azi2;
            Seiscomp::Math::Geo::delazi(lat, lon, city.lat, city.lon, &dist, &azi1, &azi2);
            rels.push_back({ dist, azi2, city.name() });
        }

        // Move the `count` closest entries to the front
        std::partial_sort(rels.begin(), rels.begin() + count, rels.end(),
            [](CityRel& x, CityRel& y) { return x.distDeg < y.distDeg; });

        std::string ret = "";
        for (size_t i = 0; i < count; i++) {
            ret += cityRelativeDescription(rels[i], true) + "\n";
        }

        return ret;
    }

    std::string cityRelativeDescription(CityRel cr, bool precise)
    {
        int distkm = Seiscomp::Math::Geo::deg2km(cr.distDeg);
        const std::string& templ = precise ? _preciseMessage : _approximateMessage;
        return Seiscomp::Util::replace(templ, Resolver(distkm, cr.azi, cr.name));
    }

    std::string nameByNearestCity(double lat, double lon, bool precise)
    {
        double dist, azi;
        const CityD* city = nearestCity(lat, lon, 9999999, 0, _cities, &dist, &azi);
        return cityRelativeDescription({ dist, azi, city->name() }, precise);
    }

    bool _setup(const Seiscomp::Config::Config& config)
    {
        std::string citiesPath;
        try {
            citiesPath = Environment::Instance()->absolutePath(config.getString("eqnamer.citiesPath"));
        } catch (...) {
            SEISCOMP_ERROR("Must configure eqnamer.citiesPath");
            return false;
        }

        std::string regionsPath;
        try {
            regionsPath = Environment::Instance()->absolutePath(config.getString("eqnamer.regionsPath"));
        } catch (...) {
            SEISCOMP_ERROR("Must configure eqnamer.regionsPath");
            return false;
        }

        try {
            _approximateMessage = config.getString("eqnamer.approximateMessage");
        } catch (...) {
            _approximateMessage = "Near @poi@";
        }

        try {
            _preciseMessage = config.getString("eqnamer.preciseMessage");
        } catch (...) {
            _preciseMessage = "@dist@km @dir@ of @poi@";
        }

        XMLArchive ar;
        if (!ar.open(citiesPath.c_str())) {
            SEISCOMP_ERROR("EQNamer: Could not read cities XML from '%s'", citiesPath);
            return false;
        }
        ar >> NAMED_OBJECT("City", _cities);
        ar.close();
        SEISCOMP_INFO("EQNamer: loaded %d cities", _cities.size());

        const Regions* all_regions = Regions::load(regionsPath);
        if (all_regions->featureSet.features().size() == 0) {
            SEISCOMP_ERROR("EQNamer: no features loaded - is regionsPath set correctly?");
            return false;
        }

        // Split the featuresets into two collections: the dynamic polygons where
        // we will name by nearest city, and the other polygons whose names we use.
        for (GeoFeature* f : all_regions->featureSet.features()) {
            const auto& attrs = f->attributes();
            auto it = attrs.find("Dynamic");
            if (it != attrs.end()) {
                if (it->second == "Dynamic") {
                    _dynamicRegions.featureSet.addFeature(f);
                } else {
                    _staticRegions.featureSet.addFeature(f);
                }
            }
        }

        // GeoFeatureSet.addFeature assumes ownership of each feature, with ~FeatureSet
        // calling delete on every GeoFeature* in its vector. But GeoFeature doesn't
        // have a copy constructor, so couldn't work out how to copy features from
        // all_regions into the two other collections? Thus each feature is now "owned"
        // by two different featuresets that will both try to free it at teardown time,
        // leading to a double-free.
        // Hacking around this by reaching into GeoFeatureSet's guts to clear the
        // all_regions vector for now.
        const_cast<std::vector<GeoFeature*>&>(all_regions->featureSet.features()).clear();

        SEISCOMP_INFO("EQNamer: loaded %d static regions, %d dynamic regions",
            _staticRegions.featureSet.features().size(), _dynamicRegions.featureSet.features().size());

        return true;
    }

public:
    EQNamer() { }
    bool setup(const Seiscomp::Config::Config& config)
    {
        try {
            return _setup(config);
        } catch (Seiscomp::Core::GeneralException& ex) {
            SEISCOMP_ERROR("Unexpected exception initializing eqnamer: %s", ex.what());
            return false;
        } catch (...) {
            SEISCOMP_ERROR("Unknown exception initializing eqnamer");
            return false;
        }
    }

    bool _process(Event* event, bool isNewEvent, const Journal& journal) {
        EventDescription* regionDesc = event->eventDescription(EventDescriptionIndex(REGION_NAME));
        if (regionDesc) {
            SEISCOMP_INFO("EQNamer::process(%s): existing region name is '%s'",
                          event->publicID().c_str(), regionDesc->text().c_str());
        } else {
            SEISCOMP_INFO(
                "EQNamer::process(%s): no existing region name", event->publicID().c_str());
            regionDesc = new EventDescription("", REGION_NAME);
            event->add(regionDesc);
        }
        const std::string name = nameEvent(event);
        SEISCOMP_INFO(
            "EQNamer::process(%s): setting region name to '%s'", event->publicID().c_str(), name);
        regionDesc->setText(name);

        bool reviewed = false;
        try {
            OriginPtr o = Origin::Find(event->preferredOriginID());
            const auto status = o->evaluationStatus();
            reviewed = status == REVIEWED || status == FINAL;
        } catch (...) {}

        Comment* nearbyPlaces = NULL;
        for (size_t i = 0; i < event->commentCount(); i++) {
            auto comment = event->comment(i);
            if (comment->id() == "nearby places") {
                nearbyPlaces = comment;
                break;
            }
        }

        if (reviewed) {
            const std::string nc = nearbyCitiesString(event);
            if (!nearbyPlaces) {
                SEISCOMP_INFO("EQNamer::process(%s): adding new nearby places:\n%s", event->publicID().c_str(), nc);
                nearbyPlaces = new Comment;
                nearbyPlaces->setId("nearby places");
                nearbyPlaces->setText(nc);
                if (!event->add(nearbyPlaces)) {
                    SEISCOMP_ERROR("EQNamer::process(%s): error adding nearby places comment to event!", event->publicID().c_str());
                }
            } else {
                SEISCOMP_INFO("EQNamer::process(%s): updating nearby places:\n%s", event->publicID().c_str(), nc);
                nearbyPlaces->setText(nc);
                nearbyPlaces->update();
            }
        } else {
            if (nearbyPlaces) {
                SEISCOMP_INFO("EQNamer::process(%s): origin not reviewed or final, removing existing nearby places", event->publicID().c_str());
                nearbyPlaces->detach();
            } else {
                SEISCOMP_INFO("EQNamer::process(%s): origin not reviewed or final, not setting nearby places", event->publicID().c_str());
            }
        }

        return false; // true means event needs updating
    }

    // isNewEvent added in seiscomp7, keep this old signature for compatibility
    bool process(Event* event, const Journal& journal) { return process(event, false, journal); }

    bool process(Event* event, bool isNewEvent, const Journal& journal)
    {
        try {
            return _process(event, isNewEvent, journal);
        } catch (Seiscomp::Core::GeneralException& ex) {
            SEISCOMP_ERROR("Unexpected exception processing event: %s", ex.what());
            return false;
        } catch (...) {
            SEISCOMP_ERROR("Unknown exception processing event");
            return false;
        }
    }

    Magnitude *preferredMagnitude(const Origin *origin)
    {
        return nullptr;
    }
};

REGISTER_EVENTPROCESSOR(EQNamer, "EQNAMER");
