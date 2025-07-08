#!/usr/bin/env -S uv run
"""Convert our cities/places GeoJSON to seiscomp's city XML format.

Usage: ./cities_geojson_to_xml.py <eqnames_points.json >eqnames.xml"""

from lxml import etree
import sys
import geojson


def geojson_to_xml(collection: geojson.FeatureCollection) -> bytes:
    root = etree.Element("seiscomp")
    for feature in collection.features:
        city = etree.SubElement(root, "City")
        lon, lat = feature.geometry.coordinates
        props = feature.properties
        if props["Type"] == "capital":
            city.attrib["category"] = "C"

        city.attrib["countryID"] = props["Country"]
        etree.SubElement(city, "name").text = props["FINAL_name"]
        etree.SubElement(city, "population").text = str(props["population"])
        etree.SubElement(city, "latitude").text = str(lat)
        etree.SubElement(city, "longitude").text = str(lon)
    return etree.tostring(etree.ElementTree(root), pretty_print=True)


if __name__ == "__main__":
    sys.stdout.buffer.write(geojson_to_xml(geojson.load(sys.stdin)))
