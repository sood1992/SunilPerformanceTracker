#!/usr/bin/env python3
import json
import sys
import requests

if len(sys.argv) < 2:
    print("Usage: python3 upload_walk.py <walk_file.json>")
    sys.exit(1)

file_path = sys.argv[1]

# Read the NDJSON file
with open(file_path, 'r') as f:
    lines = f.readlines()

# Parse metadata (first line)
meta = json.loads(lines[0])
print(f"Device ID: {meta.get('deviceId')}")
print(f"Start Time: {meta.get('startTime')}")

# Parse points (remaining lines)
points = []
for line in lines[1:]:
    line = line.strip()
    if line:
        try:
            points.append(json.loads(line))
        except:
            pass

print(f"Points: {len(points)}")

# Build payload
payload = {
    "deviceId": meta.get("deviceId"),
    "startTime": meta.get("startTime"),
    "points": points
}

if "startLat" in meta:
    payload["startLat"] = meta["startLat"]
if "startLon" in meta:
    payload["startLon"] = meta["startLon"]

payload_json = json.dumps(payload)
print(f"Payload size: {len(payload_json)} bytes")
print()
print("Uploading to Railway backend...")

# Upload
try:
    response = requests.post(
        "https://sunilperformancetracker-production.up.railway.app/api/walks/upload",
        headers={
            "Content-Type": "application/json",
            "X-Device-ID": meta.get("deviceId")
        },
        json=payload,
        timeout=60
    )
    print(f"HTTP Status: {response.status_code}")
    print(f"Response: {response.text[:500]}")
    
    if response.status_code in [200, 201]:
        print()
        print("✓ Upload successful!")
    else:
        print()
        print("✗ Upload failed")
except Exception as e:
    print(f"Error: {e}")
