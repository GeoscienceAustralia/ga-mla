#define SEISCOMP_COMPONENT MAGSELECT

#include <seiscomp/core/exceptions.h>
#include <seiscomp/core/plugin.h>
#include <seiscomp/config/config.h>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/datamodel/magnitude.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/logging/log.h>
#include <seiscomp/plugins/events/eventprocessor.h>
#include <seiscomp/utils/leparser.h>

#include <stdexcept>
#include <string>
#include <vector>

ADD_SC_PLUGIN(
    "Rule-based preferred magnitude type selector for scevent.",
    "Geoscience Australia", 0, 1, 0)

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
namespace {

/*
 * Evaluation context that exposes origin and reference-magnitude attributes
 * to LeParser V2 conditions.
 *
 * Available keys (double):
 *   mag / magnitude  — value of the reference magnitude type
 *   stations         — station count of the reference magnitude type
 *   depth            — origin depth in km
 *   lat / latitude   — origin latitude
 *   lon / longitude  — origin longitude
 *
 * Throws Core::ValueException when a key is valid but has no value set.
 * Throws std::runtime_error for unknown keys.
 */
class MagKeyValueContext : public Seiscomp::Utils::V2::LeKeyValueContext {
    public:
        MagKeyValueContext(const Seiscomp::DataModel::Origin *origin,
                           const std::string &referenceType)
            : _origin(origin), _refMag(nullptr) {
            for ( size_t i = 0; i < origin->magnitudeCount(); ++i ) {
                auto *m = origin->magnitude(i);
                if ( m->type() == referenceType ) {
                    _refMag = m;
                    break;
                }
            }
        }

        double getDouble(std::string_view key) const override {
            if ( key == "mag" || key == "magnitude" ) {
                if ( !_refMag ) throw Seiscomp::Core::ValueException();
                return _refMag->magnitude().value();
            }
            if ( key == "stations" ) {
                if ( !_refMag ) throw Seiscomp::Core::ValueException();
                return static_cast<double>(_refMag->stationCount());
            }
            if ( key == "depth" ) {
                return _origin->depth().value();
            }
            if ( key == "lat" || key == "latitude" ) {
                return _origin->latitude().value();
            }
            if ( key == "lon" || key == "longitude" ) {
                return _origin->longitude().value();
            }
            throw std::runtime_error(std::string("unknown key: ") + std::string(key));
        }

        std::string getString(std::string_view key) const override {
            throw std::runtime_error(std::string("unknown key: ") + std::string(key));
        }

    private:
        const Seiscomp::DataModel::Origin    *_origin;
        const Seiscomp::DataModel::Magnitude *_refMag;
};

} // namespace
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
class MagSelectProcessor : public Seiscomp::Client::EventProcessor {

    struct SelectorRule {
        Seiscomp::Utils::V2::LeExpressionPtr expression;
        std::string                           magnitudeType;
    };

    public:

        bool setup(const Seiscomp::Config::Config &config) override {
            try {
                _referenceType = config.getString("magselect.referenceType");
            }
            catch ( ... ) {
                SEISCOMP_ERROR("magselect: magselect.referenceType is not configured");
                return false;
            }

            std::vector<std::string> ruleNames;
            try {
                ruleNames = config.getStrings("magselect.rules");
            }
            catch ( ... ) {
                SEISCOMP_ERROR("magselect: magselect.rules is not configured");
                return false;
            }

            Seiscomp::Utils::V2::LeKeyValueFactory factory;
            auto symbols = Seiscomp::Utils::V2::LeParser::DefaultSymbols();
            symbols.reserved = Seiscomp::Utils::V2::LeKeyValueFactory::Reserved();
            Seiscomp::Utils::V2::LeParser parser(&factory, &symbols);

            for ( const auto &name : ruleNames ) {
                const std::string base = "magselect.rules." + name;

                std::string condStr;
                try {
                    condStr = config.getString(base + ".condition");
                }
                catch ( ... ) {
                    SEISCOMP_WARNING("magselect: rule '%s' has no condition, skipping",
                                     name.c_str());
                    continue;
                }

                std::string magType;
                try {
                    magType = config.getString(base + ".magnitudeType");
                }
                catch ( ... ) {
                    SEISCOMP_WARNING("magselect: rule '%s' has no magnitudeType, skipping",
                                     name.c_str());
                    continue;
                }

                Seiscomp::Utils::V2::LeExpression *expr = nullptr;
                try {
                    expr = parser.parse(condStr);
                }
                catch ( const std::exception &e ) {
                    SEISCOMP_WARNING("magselect: rule '%s' invalid condition '%s': %s",
                                     name.c_str(), condStr.c_str(), e.what());
                    continue;
                }

                SelectorRule rule;
                rule.expression = expr;
                rule.magnitudeType = magType;
                _rules.emplace_back(std::move(rule));

                SEISCOMP_INFO("magselect: rule '%s': [%s] -> %s",
                              name.c_str(), condStr.c_str(), magType.c_str());
            }

            SEISCOMP_INFO("magselect: %d rule(s) loaded, reference type: %s",
                          static_cast<int>(_rules.size()), _referenceType.c_str());
            return !_rules.empty();
        }

        /*
         * Walks the rule list in order. Builds an evaluation context from the
         * origin using _referenceType as the source for 'mag' and 'stations'.
         * Returns the first Magnitude on the origin whose type matches a
         * passing rule, or nullptr to let scevent fall back to its magTypes
         * priority list.
         */
        Seiscomp::DataModel::Magnitude *preferredMagnitude(
                const Seiscomp::DataModel::Origin *origin) override {
            if ( _rules.empty() || !origin ) return nullptr;

            MagKeyValueContext ctx(origin, _referenceType);

            for ( const auto &rule : _rules ) {
                if ( !rule.expression ) continue;

                try {
                    if ( !rule.expression->eval(&ctx) ) continue;
                }
                catch ( ... ) {
                    continue;
                }

                for ( size_t i = 0; i < origin->magnitudeCount(); ++i ) {
                    auto *mag = origin->magnitude(i);
                    if ( mag->type() == rule.magnitudeType ) {
                        SEISCOMP_DEBUG("magselect: selected %s for origin %s",
                                       rule.magnitudeType.c_str(),
                                       origin->publicID().c_str());
                        return mag;
                    }
                }

                SEISCOMP_WARNING(
                    "magselect: rule matched but type '%s' not found on origin %s — "
                    "trying next rule",
                    rule.magnitudeType.c_str(), origin->publicID().c_str());
            }

            return nullptr;
        }

        bool process(Seiscomp::DataModel::Event *, bool,
                     const Journal &) override {
            return false;
        }

    private:
        std::string               _referenceType;
        std::vector<SelectorRule> _rules;
};

REGISTER_EVENTPROCESSOR(MagSelectProcessor, "MagSelect");
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
