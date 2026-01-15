#!/usr/bin/env python3
import argparse
import json
import sys
from urllib.parse import quote

import pandas as pd

GEOJSONIO_PREFIX = "http://geojson.io/#data=data:application/json,"
MAX_POINTS = 100


def group_to_geojson_url(group: pd.DataFrame) -> str:
    """
    Build a geojson.io URL containing a FeatureCollection of Points.
    Each point has properties: { "finalRegionName": <value> }.
    """
    features = []
    for lon, lat, final_name in zip(
        group["longitude"], group["latitude"], group["finalRegionName"]
    ):
        features.append(
            {
                "type": "Feature",
                "geometry": {"type": "Point", "coordinates": [float(lon), float(lat)]},
                "properties": {"finalRegionName": final_name},
            }
        )

    fc = {"type": "FeatureCollection", "features": features}

    # Compact JSON reduces URL size; percent-encode for geojson.io fragment
    payload = json.dumps(fc, separators=(",", ":"), ensure_ascii=False)
    return GEOJSONIO_PREFIX + quote(payload, safe="")


def main():
    ap = argparse.ArgumentParser(
        description="Aggregate named CSV by oldRegionName+initialRegionName and emit geojson.io URLs."
    )
    ap.add_argument(
        "input_csv",
        help="Input CSV (must include longitude, latitude, oldRegionName, initialRegionName, finalRegionName)",
    )
    ap.add_argument(
        "-o",
        "--output_csv",
        default="-",
        help="Output CSV path (default: stdout)",
    )
    ap.add_argument(
        "--seed",
        type=int,
        default=None,
        help="Random seed for reproducible sampling when groups have > 100 points",
    )
    args = ap.parse_args()

    df = pd.read_csv(args.input_csv)

    # Minimal sanity check (fail loudly if required columns missing)
    required = [
        "longitude",
        "latitude",
        "oldRegionName",
        "initialRegionName",
        "finalRegionName",
    ]
    missing = [c for c in required if c not in df.columns]
    if missing:
        raise SystemExit(f"Missing required columns: {missing}")

    out_rows = []

    # Iterate groups (sort=False keeps group iteration order based on appearance, but we sort later anyway)
    for (old_name, init_name), g in df.groupby(
        ["oldRegionName", "initialRegionName"], sort=False
    ):
        count = int(len(g))

        # Sample up to 100 points at random for the GeoJSON payload
        if count > MAX_POINTS:
            g2 = g.sample(n=MAX_POINTS, random_state=args.seed)
        else:
            g2 = g

        url = group_to_geojson_url(g2)

        out_rows.append(
            {
                "oldRegionName": old_name,
                "initialRegionName": init_name,
                "count": count,
                "url": url,
            }
        )

    out_df = pd.DataFrame(
        out_rows, columns=["oldRegionName", "initialRegionName", "count", "url"]
    )

    # Sort by count descending
    out_df = out_df.sort_values("count", ascending=False, kind="mergesort")

    # Write output
    if args.output_csv in ("-", "/dev/stdout"):
        out_df.to_csv(sys.stdout, index=False)
    else:
        out_df.to_csv(args.output_csv, index=False)


if __name__ == "__main__":
    main()
