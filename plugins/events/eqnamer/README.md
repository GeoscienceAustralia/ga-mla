# eqnamer

**eqnamer** is an scevent plugin that applies NEAC-specific logic to set the region name
of earthquakes.

## Input data

You need two input datasets:

1. A point dataset consisting of "places" (e.g. cities) in the seiscomp cities.xml
   format. You can convert a GeoJSON FeatureCollection of Points into this format using
   the script ./cities_geojson_to_xml.py. The features should have the following
   properties:

   - `FINAL_name`: the place name (will be used verbatim in eqnamer outputs)
   - `latitude`: the latitude in degrees N
   - `longitude`: the longitude in degrees E

   Not currently used by eqnamer but could be in future:

   - `Type` set to "Capital" to denote capital cities/important places
   - `Country`: name of the containing country
   - `population`: human population

2. A polygon dataset consisting of regions in seiscomp-compatible geoJSON format. This
   should be preprocessed with the script ./preprocess_polygons.sh, which

   - Subdivides any very long edges, preventing bugs due to SeisComP's handling of
     longitude wrapping
   - Truncates useless decimal places to minimize filesize.

## Configuration

There are a few configuration options that should be set in your `scevent.cfg`,
following this example:

```
eqnamer.citiesPath = @ROOTDIR@/share/eqnamer/cities.xml
eqnamer.regionsPath = @ROOTDIR@/share/eqnamer/polygons.geojson
eqnamer.approximateMessage = "Near @poi@"
eqnamer.preciseMessage = "@dist@km @dir@ of @poi@"
```
