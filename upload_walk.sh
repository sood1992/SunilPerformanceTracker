#!/bin/bash

# Usage: ./upload_walk.sh <walk_file.json>

if [ -z "$1" ]; then
    echo "Usage: ./upload_walk.sh <walk_file.json>"
    exit 1
fi

FILE="$1"

if [ ! -f "$FILE" ]; then
    echo "File not found: $FILE"
    exit 1
fi

# Read first line (metadata)
META=$(head -1 "$FILE")
DEVICE_ID=$(echo "$META" | jq -r '.deviceId')
START_TIME=$(echo "$META" | jq -r '.startTime')
START_LAT=$(echo "$META" | jq -r '.startLat // empty')
START_LON=$(echo "$META" | jq -r '.startLon // empty')

echo "Device ID: $DEVICE_ID"
echo "Start Time: $START_TIME"

# Build points array from remaining lines
POINTS=$(tail -n +2 "$FILE" | jq -s '.')

# Build final payload
PAYLOAD=$(jq -n \
    --arg deviceId "$DEVICE_ID" \
    --argjson startTime "$START_TIME" \
    --argjson points "$POINTS" \
    '{deviceId: $deviceId, startTime: $startTime, points: $points}')

# Add optional fields if present
if [ -n "$START_LAT" ]; then
    PAYLOAD=$(echo "$PAYLOAD" | jq --argjson lat "$START_LAT" '. + {startLat: $lat}')
fi
if [ -n "$START_LON" ]; then
    PAYLOAD=$(echo "$PAYLOAD" | jq --argjson lon "$START_LON" '. + {startLon: $lon}')
fi

POINT_COUNT=$(echo "$POINTS" | jq 'length')
PAYLOAD_SIZE=$(echo "$PAYLOAD" | wc -c)

echo "Points: $POINT_COUNT"
echo "Payload size: $PAYLOAD_SIZE bytes"
echo ""
echo "Uploading to Railway backend..."

# Upload
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \
    "https://sunilperformancetracker-production.up.railway.app/api/walks/upload" \
    -H "Content-Type: application/json" \
    -H "X-Device-ID: $DEVICE_ID" \
    -d "$PAYLOAD")

HTTP_CODE=$(echo "$RESPONSE" | tail -1)
BODY=$(echo "$RESPONSE" | head -n -1)

echo "HTTP Status: $HTTP_CODE"
echo "Response: $BODY"

if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "201" ]; then
    echo ""
    echo "✓ Upload successful!"
else
    echo ""
    echo "✗ Upload failed"
fi
