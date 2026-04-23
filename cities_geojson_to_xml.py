#!/usr/bin/env -S uv run
"""Convert our cities/places GeoJSON to seiscomp's city XML format.

Usage: ./cities_geojson_to_xml.py <eqnames_points.json >eqnames.xml"""

from lxml import etree
import sys
import geojson
import logging

logger = logging.getLogger(__name__)


def geojson_to_xml(collection: geojson.FeatureCollection) -> bytes:
    root = etree.Element("seiscomp")
    for feature in collection.features:
        city = etree.SubElement(root, "City")
        lon, lat = feature.geometry.coordinates
        props = feature.properties
        if props["Type"] == "capital":
            city.attrib["category"] = "C"

        if country := str(props.get("Country") or "").strip():
            city.attrib["countryID"] = country

        name = props["name"]
        if country == "Australia":
            state = str(props.get("State") or "").strip()
            if state:
                name += f", {state}"
        etree.SubElement(city, "name").text = name
        etree.SubElement(city, "population").text = str(props["population"])
        etree.SubElement(city, "latitude").text = str(lat)
        etree.SubElement(city, "longitude").text = str(lon)
    return etree.tostring(etree.ElementTree(root), pretty_print=True)


if __name__ == "__main__":
    sys.stdout.buffer.write(geojson_to_xml(geojson.load(sys.stdin)))
