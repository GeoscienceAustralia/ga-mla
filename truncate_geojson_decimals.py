#!/usr/bin/env -S uv run

import sys
import json


def trunc(x):
    if isinstance(x, list):
        return [trunc(y) for y in x]
    if isinstance(x, float):
        return round(x, 6)
    return x


data = json.load(sys.stdin)
for feature in data["features"]:
    feature["geometry"]["coordinates"] = [
        trunc(x) for x in feature["geometry"]["coordinates"]
    ]
json.dump(data, sys.stdout)
