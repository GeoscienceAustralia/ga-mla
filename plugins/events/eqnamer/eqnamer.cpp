#define SEISCOMP_COMPONENT EQNAMER

#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>

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
#include <vector>

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
    std::string country;
};

struct Resolver : public Seiscomp::Util::VariableResolver {
    const int& _dist;
    const std::string& _name;
    const std::string& _poiCountry;
    const std::string& _epiCountry;
    std::string _dir;

    Resolver(const int& dist, const double& azi, const std::string& name,
        const std::string& poiCountry, const std::string& epiCountry)
        : _dist(dist)
        , _name(name)
        , _poiCountry(poiCountry)
        , _epiCountry(epiCountry)
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
            variable = toString(_dist);
        else if (variable == "dir")
            variable = _dir;
        else if (variable == "poi")
            variable = _name;
        else if (variable == "poi_country")
            variable = _poiCountry;
        else if (variable == "epi_country")
            variable = _epiCountry;
        else
            return false;

        return true;
    }
};

const std::string getAttr(const GeoFeature& f, const std::string& key)
{
    const auto& attrs = f.attributes();
    auto it = attrs.find(key);
    if (it != attrs.end()) {
        return it->second;
    }
    return "";
}

std::string crustTypeLabel(const GeoFeature& f)
{
    const std::string t = getAttr(f, "Crust_Type");
    if (t == "Coastal")
        return "Coastal";
    else if (t == "Oceanic")
        return "Offshore";
    return "";
}

static std::string getFeatureName(const GeoFeature& f)
{
    const auto& attrs = f.attributes();
    auto it = attrs.find("Primary_ID");
    if (it == attrs.end())
        it = attrs.find("name");
    if (it != attrs.end())
        return it->second;
    return "";
}

struct TemplatePair {
    std::vector<std::string> approximate;
    std::vector<std::string> precise;
};

struct TemplateSet {
    TemplatePair homeCountry;
    TemplatePair sameCountry;
    TemplatePair differentCountry;
};

static std::string getStringOrDefault(
    const Seiscomp::Config::Config& config, const std::string& key, const std::string& def)
{
    try {
        return config.getString(key);
    } catch (...) {
        return def;
    }
}

static std::vector<std::string> getStringsOrDefault(const Seiscomp::Config::Config& config,
    const std::string& key, const std::vector<std::string> def)
{
    try {
        return config.getStrings(key);
    } catch (...) {
        return def;
    }
}

class EQNamer : public Seiscomp::Client::EventProcessor {
protected:
    std::vector<CityD> _cities;
    Regions _staticRegions;
    Regions _dynamicRegions;
    Regions _countries;

    std::string _homeCountry;
    TemplateSet _templates;

    std::string countryFor(double lat, double lon) const
    {
        if (_countries.featureSet.features().empty())
            return "";
        if (auto f = _countries.find(lat, lon))
            return getAttr(*f, "CNTRY_NAME");
        return "";
    }

    const std::vector<std::string>& selectTemplate(
        bool precise, const std::string& epiCountry, const std::string& poiCountry) const
    {
        const TemplatePair* pair = nullptr;

        if (!_homeCountry.empty() && epiCountry == _homeCountry && poiCountry == _homeCountry)
            pair = &_templates.homeCountry;
        else if (epiCountry == poiCountry)
            pair = &_templates.sameCountry;
        else
            pair = &_templates.differentCountry;

        return precise ? pair->precise : pair->approximate;
    }

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

            const std::string crust = crustTypeLabel(*f);
            return nameByNearestCity(lat, lon, crust, precise);
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

    std::string cityRelativeDescription(const CityRel& cr, const std::string& epiCountry,
        const std::string& crustLabel, bool precise)
    {
        int distkm = Seiscomp::Math::Geo::deg2km(cr.distDeg);
        SEISCOMP_DEBUG("dist = %i km", distkm);
        const auto& templ = selectTemplate(precise, epiCountry, cr.country);
        const std::string ec = epiCountry.empty() ? crustLabel
            : crustLabel.empty()                  ? epiCountry
                                                  : crustLabel + " " + epiCountry;
        const auto resolver = Resolver(distkm, cr.azi, cr.name, cr.country, ec);
        std::string s = "";
        bool first = true;
        for (const std::string& templPart : templ) {
            std::string part = Seiscomp::Util::replace(templPart, resolver);
            if (!part.empty()) {
                if (first) {
                    first = false;
                } else {
                    s += ", ";
                }
                s += part;
            }
        }

        SEISCOMP_DEBUG("'%s', epi='%s', poi='%s' => %s", crustLabel, epiCountry, cr.country, s);
        return s;
    }

    std::string nameByNearestCity(
        double lat, double lon, const std::string& crustLabel, bool precise)
    {
        double dist, azi;
        const std::string epiCountry = countryFor(lat, lon);
        const CityD* city = nearestCity(lat, lon, 9999999, 0, _cities, &dist, &azi);
        const CityRel cityRel = { dist, azi, city->name(), city->countryID() };
        return cityRelativeDescription(cityRel, epiCountry, crustLabel, precise);
    }

    std::string nearbyCitiesString(Event* event, size_t count = 4)
    {
        OriginPtr o = Origin::Find(event->preferredOriginID());
        const double lat = o->latitude().value();
        const double lon = o->longitude().value();
        const std::string epiCountry = countryFor(lat, lon);

        std::vector<CityRel> rels;
        rels.reserve(_cities.size());

        for (const CityD& city : _cities) {
            double dist, azi1, azi2;
            Seiscomp::Math::Geo::delazi(lat, lon, city.lat, city.lon, &dist, &azi1, &azi2);
            rels.push_back({ dist, azi2, city.name(), city.countryID() });
        }

        if (count > rels.size())
            count = rels.size();

        std::partial_sort(rels.begin(), rels.begin() + count, rels.end(),
            [](const CityRel& x, const CityRel& y) { return x.distDeg < y.distDeg; });

        std::string ret;
        for (size_t i = 0; i < count; ++i) {
            ret += cityRelativeDescription(rels[i], epiCountry, "", true) + "\n";
        }

        return ret;
    }

    bool _setup(const Seiscomp::Config::Config& config)
    {
        std::string citiesPath;
        try {
            citiesPath
                = Environment::Instance()->absolutePath(config.getString("eqnamer.citiesPath"));
        } catch (...) {
            SEISCOMP_ERROR("Must configure eqnamer.citiesPath");
            return false;
        }

        std::string regionsPath;
        try {
            regionsPath
                = Environment::Instance()->absolutePath(config.getString("eqnamer.regionsPath"));
        } catch (...) {
            SEISCOMP_ERROR("Must configure eqnamer.regionsPath");
            return false;
        }

        std::string countriesPath;
        try {
            countriesPath
                = Environment::Instance()->absolutePath(config.getString("eqnamer.countriesPath"));
        } catch (...) {
            SEISCOMP_ERROR("Must configure eqnamer.countriesPath");
            return false;
        }

        _homeCountry = getStringOrDefault(config, "eqnamer.homeCountry", "");

        _templates.homeCountry.approximate = getStringsOrDefault(
            config, "eqnamer.template.homeCountry.approximate", { "Near @poi@" });
        _templates.homeCountry.precise = getStringsOrDefault(
            config, "eqnamer.template.homeCountry.precise", { "@dist@ km @dir@ of @poi@" });

        _templates.sameCountry.approximate = getStringsOrDefault(
            config, "eqnamer.template.sameCountry.approximate", { "@epi_country@", "Near @poi@" });
        _templates.sameCountry.precise
            = getStringsOrDefault(config, "eqnamer.template.sameCountry.precise",
                { "@epi_country@", "@dist@ km @dir@ of @poi@" });

        _templates.differentCountry.approximate
            = getStringsOrDefault(config, "eqnamer.template.differentCountry.approximate",
                { "@epi_country@", "Near @poi@", "@poi_country@" });
        _templates.differentCountry.precise
            = getStringsOrDefault(config, "eqnamer.template.differentCountry.precise",
                { "@epi_country@", "@dist@ km @dir@ of @poi@", "@poi_country@" });

        XMLArchive ar;
        if (!ar.open(citiesPath.c_str())) {
            SEISCOMP_ERROR("EQNamer: Could not read cities XML from '%s'", citiesPath);
            return false;
        }
        ar >> NAMED_OBJECT("City", _cities);
        ar.close();
        SEISCOMP_INFO("EQNamer: loaded %d cities", (int)_cities.size());

        const Regions* all_countries = Regions::load(countriesPath);
        if (!all_countries || all_countries->featureSet.features().empty()) {
            SEISCOMP_ERROR("EQNamer: no country features loaded - is countriesPath set correctly?");
            return false;
        }
        for (GeoFeature* f : all_countries->featureSet.features())
            _countries.featureSet.addFeature(f);
        const_cast<std::vector<GeoFeature*>&>(all_countries->featureSet.features()).clear();
        SEISCOMP_INFO("EQNamer: loaded %d countries", (int)_countries.featureSet.features().size());

        const Regions* all_regions = Regions::load(regionsPath);
        if (!all_regions || all_regions->featureSet.features().empty()) {
            SEISCOMP_ERROR("EQNamer: no features loaded - is regionsPath set correctly?");
            return false;
        }

        for (GeoFeature* f : all_regions->featureSet.features()) {
            const auto& attrs = f->attributes();
            auto it = attrs.find("Dynamic");
            if (it != attrs.end()) {
                if (it->second == "Dynamic")
                    _dynamicRegions.featureSet.addFeature(f);
                else
                    _staticRegions.featureSet.addFeature(f);
            }
        }

        const_cast<std::vector<GeoFeature*>&>(all_regions->featureSet.features()).clear();

        SEISCOMP_INFO("EQNamer: loaded %d static regions, %d dynamic regions",
            (int)_staticRegions.featureSet.features().size(),
            (int)_dynamicRegions.featureSet.features().size());

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

    bool _process(Event* event, bool isNewEvent, const Journal& journal)
    {
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
        } catch (...) {
        }

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
                SEISCOMP_INFO("EQNamer::process(%s): adding new nearby places:\n%s",
                    event->publicID().c_str(), nc);
                nearbyPlaces = new Comment;
                nearbyPlaces->setId("nearby places");
                nearbyPlaces->setText(nc);
                if (!event->add(nearbyPlaces)) {
                    SEISCOMP_ERROR(
                        "EQNamer::process(%s): error adding nearby places comment to event!",
                        event->publicID().c_str());
                }
            } else {
                SEISCOMP_INFO("EQNamer::process(%s): updating nearby places:\n%s",
                    event->publicID().c_str(), nc);
                nearbyPlaces->setText(nc);
                nearbyPlaces->update();
            }
        } else {
            if (nearbyPlaces) {
                SEISCOMP_INFO("EQNamer::process(%s): origin not reviewed or final, removing "
                              "existing nearby places",
                    event->publicID().c_str());
                nearbyPlaces->detach();
            } else {
                SEISCOMP_INFO(
                    "EQNamer::process(%s): origin not reviewed or final, not setting nearby places",
                    event->publicID().c_str());
            }
        }

        return false;
    }

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

    Magnitude* preferredMagnitude(const Origin* origin) { return nullptr; }
};

REGISTER_EVENTPROCESSOR(EQNamer, "EQNAMER");
