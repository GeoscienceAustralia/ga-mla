#!/bin/bash
# Usage: ./preprocess_polygons.sh input.geojson output.geojson
# Requires GDAL installed
input="$1"
output="$2"

tmp="$(mktemp -d)"/tmp.geojson
ogr2ogr -segmentize 90 "$tmp" "$input"
./truncate_geojson_decimals.py <"$tmp" >"$output"
