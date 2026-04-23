#!/usr/bin/env python

import sys
import json
import logging

logger = logging.getLogger(__name__)


def trunc(x):
    if isinstance(x, list):
        return [trunc(y) for y in x]
    if isinstance(x, float):
        return round(x, 6)
    return x


data = json.load(sys.stdin)
fs = []
for feature in data["features"]:
    try:
        fs.append(
            {
                **feature,
                "geometry": {
                    **feature["geometry"],
                    "coordinates": [
                        trunc(x) for x in feature["geometry"]["coordinates"]
                    ],
                },
            }
        )
    except TypeError:
        logger.exception(f"Error processing {feature.get('properties')}")
        logger.error("Skipping this polygon and and continuing.")
json.dump({**data, "features": fs}, sys.stdout)
