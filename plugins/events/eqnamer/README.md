# eqnamer

**eqnamer** is an scevent plugin that applies NEAC-specific logic to set the region name
of earthquakes.

## Input data

You need three input datasets:

1. A point dataset consisting of "places" (e.g. cities) in the seiscomp cities.xml
   format. You can convert a GeoJSON FeatureCollection of Points into this format using
   the script ./cities_geojson_to_xml.py. The features should have the following
   properties:

   - `name`: the place name (will be used verbatim in eqnamer outputs)
   - `latitude`: the latitude in degrees N
   - `longitude`: the longitude in degrees E
   - `Country`: name of the containing country

   and optionally

   - `State`, which will be appended to name (in the format `{name} {State}`) *only*
     when Country = 'Australia'.

   Not currently used by eqnamer but could be in future:

   - `Type` set to "Capital" to denote capital cities/important places
   - `population`: human population

2. A polygon dataset in seiscomp-compatible geoJSON format. This should be preprocessed
   with the script ./preprocess_polygons.sh, which

   - Subdivides any very long edges, preventing bugs due to SeisComP's handling of
     longitude wrapping
   - Truncates useless decimal places to minimize filesize.

   The polygons should have properties:

   - `Primary_ID` or `name`: The name of the polygon. For static polygons, this is used
     as the region name.
   - `Dynamic`: A value 'Dynamic' or 'Static' determining whether the dynamic naming
     logic should be used for events inside this polygon
   - `Crust_Type`: A special string (e.g. 'Coastal' or 'Offshore') to be prepended to the
     epicentre country name when using dynamic naming.

3. A countries dataset in seiscomp-compatible geoJSON format.
   Likewise, should be preprocessed with the script, and the polygons should have
   properties:

   - `CNTRY_NAME`: The name of the country

## Configuration

There are a few configuration options that should be set in your `scevent.cfg`,
following this example:

```
plugins = eqnamer, ...
eqnamer {
    citiesPath = @ROOTDIR@/share/eqnamer/cities.xml
    regionsPath = @ROOTDIR@/share/eqnamer/polygons.geojson
    countriesPath = @ROOTDIR@/share/eqnamer/countries.geojson
    homeCountry = "Australia"

    template {
        homeCountry {
            approximate = "Near @poi@"
            precise = "@dist@ km @dir@ of @poi@"
        }
        sameCountry {
            approximate = "@epi_country@", "Near @poi@"
            precise = "@epi_country@", "@dist@ km @dir@ of @poi@"
        }
        differentCountry {
            approximate = "@epi_country@", "Near @poi@, @poi_country@"
            precise = "@epi_country@", "@dist@ km @dir@ of @poi@", "@poi_country@"
        }
    }
}
```
