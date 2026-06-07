# magselect

**magselect** is an scevent plugin that selects the preferred magnitude type for each
event based on ordered, configurable rules. Each rule pairs a logical expression with a
target magnitude type. Rules are evaluated top-to-bottom against a reference magnitude;
the first matching rule wins. If no rule matches, scevent falls back to its standard
`eventAssociation.magTypes` priority list.

## Requirements

SeisComP ≥ 7.3.0 (SC API ≥ 17.3.0). The plugin uses LeParser V2
(`Seiscomp::Utils::V2`) which was introduced in that release. It will not
compile against earlier versions.

## How it works

At event processing time the plugin:

1. Looks up the configured reference magnitude type on the preferred origin.
2. Evaluates each rule's condition using the reference magnitude's value and station
   count (plus origin depth and location if needed).
3. Returns the first magnitude on the origin whose type matches a passing rule.
4. Returns `nullptr` if no rule matches, letting scevent handle magnitude selection
   through its normal priority mechanism.

## Condition keys

| Key | Description |
|---|---|
| `mag` / `magnitude` | Value of the reference magnitude |
| `stations` | Station count of the reference magnitude |
| `depth` | Origin depth (km) |
| `lat` / `latitude` | Origin latitude |
| `lon` / `longitude` | Origin longitude |

Logical operators (LeParser V2 syntax): `&&` (and), `||` (or), `!` (not).
Comparison operators: `<`, `<=`, `>`, `>=`, `==`, `!=`.

## Configuration

Add `magselect` to the plugins list and configure rules in `scevent.cfg`:

```
plugins = ${plugins}, magselect

# Magnitude type used to resolve 'mag' and 'stations' in rule conditions.
# Must be present on the origin. Required.
magselect.referenceType = MLa

# Ordered list of rule names (evaluated top-to-bottom, first match wins).
magselect.rules = large, medium, small, fallback

magselect.rules.large.condition = "mag >= 6.0 && stations >= 3"
magselect.rules.large.magnitudeType = MLa01

magselect.rules.medium.condition = "mag >= 4.0 && stations >= 3"
magselect.rules.medium.magnitudeType = MLa05

magselect.rules.small.condition = "mag < 4.0 && stations >= 3"
magselect.rules.small.magnitudeType = MLa075

# Catches events where mag is unavailable but station count is met.
magselect.rules.fallback.condition = "stations >= 3"
magselect.rules.fallback.magnitudeType = MLa075
```

### Notes

- `magselect.referenceType` is required. The plugin will refuse to start if it is
  missing from the configuration.
- The reference type should be an unfiltered (or lightly filtered) magnitude so that
  threshold comparisons are not biased by high-pass filter attenuation at larger
  magnitudes.
- Rules whose target magnitude type is not present on the origin are skipped with a
  warning, and evaluation continues to the next rule.
