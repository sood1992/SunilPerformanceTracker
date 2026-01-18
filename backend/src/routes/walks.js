/**
 * Walk Routes
 *
 * API endpoints for managing walk data.
 */

import { Router } from 'express';
import { query } from '../db/init.js';

export const walkRoutes = Router();

/**
 * GET /api/walks
 * List all walks with optional filtering
 */
walkRoutes.get('/', async (req, res, next) => {
  try {
    const { device_id, limit = 50, offset = 0, from, to } = req.query;

    let sql = `
      SELECT
        id, device_id, filename, start_time, end_time,
        duration_seconds, total_distance_meters,
        avg_speed_kmh, max_speed_kmh,
        start_lat, start_lon, end_lat, end_lon,
        point_count, uploaded_at
      FROM walks
      WHERE 1=1
    `;
    const params = [];
    let paramIndex = 1;

    if (device_id) {
      sql += ` AND device_id = $${paramIndex++}`;
      params.push(device_id);
    }

    if (from) {
      sql += ` AND start_time >= $${paramIndex++}`;
      params.push(from);
    }

    if (to) {
      sql += ` AND start_time <= $${paramIndex++}`;
      params.push(to);
    }

    sql += ` ORDER BY start_time DESC LIMIT $${paramIndex++} OFFSET $${paramIndex++}`;
    params.push(parseInt(limit), parseInt(offset));

    const result = await query(sql, params);

    // Get total count (apply same filters as main query, excluding LIMIT/OFFSET)
    let countSql = 'SELECT COUNT(*) as total FROM walks WHERE 1=1';
    const countParams = [];
    let countParamIndex = 1;

    if (device_id) {
      countSql += ` AND device_id = $${countParamIndex++}`;
      countParams.push(device_id);
    }

    if (from) {
      countSql += ` AND start_time >= $${countParamIndex++}`;
      countParams.push(from);
    }

    if (to) {
      countSql += ` AND start_time <= $${countParamIndex++}`;
      countParams.push(to);
    }

    const countResult = await query(countSql, countParams);

    res.json({
      walks: result.rows.map(formatWalk),
      pagination: {
        total: parseInt(countResult.rows[0].total),
        limit: parseInt(limit),
        offset: parseInt(offset)
      }
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/walks/:id
 * Get a specific walk by ID
 */
walkRoutes.get('/:id', async (req, res, next) => {
  try {
    const { id } = req.params;

    const result = await query(
      'SELECT * FROM walks WHERE id = $1',
      [id]
    );

    if (result.rows.length === 0) {
      return res.status(404).json({ error: 'Walk not found' });
    }

    res.json(formatWalk(result.rows[0]));
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/walks/:id/points
 * Get GPS points for a specific walk
 */
walkRoutes.get('/:id/points', async (req, res, next) => {
  try {
    const { id } = req.params;
    const { simplify = 'false' } = req.query;

    // First check if walk exists
    const walkResult = await query('SELECT id FROM walks WHERE id = $1', [id]);
    if (walkResult.rows.length === 0) {
      return res.status(404).json({ error: 'Walk not found' });
    }

    let sql = `
      SELECT
        time_offset_seconds as t,
        latitude as lat,
        longitude as lon,
        altitude as alt,
        speed_kmh as speed,
        accel_x as ax,
        accel_y as ay,
        accel_z as az
      FROM walk_points
      WHERE walk_id = $1
      ORDER BY time_offset_seconds
    `;

    const result = await query(sql, [id]);

    // Optional: simplify path for map display
    let points = result.rows;
    if (simplify === 'true' && points.length > 500) {
      points = simplifyPath(points, 500);
    }

    res.json({
      walkId: parseInt(id),
      pointCount: points.length,
      points: points
    });
  } catch (error) {
    next(error);
  }
});

/**
 * POST /api/walks/upload
 * Upload walk data from device
 */
walkRoutes.post('/upload', async (req, res, next) => {
  try {
    const { deviceId, filename, startTime, startLat, startLon, points = [] } = req.body;

    // Validate required fields
    if (!deviceId || !startTime) {
      return res.status(400).json({
        error: 'Missing required fields',
        required: ['deviceId', 'startTime']
      });
    }

    // Validate deviceId format
    if (typeof deviceId !== 'string' || deviceId.length > 50) {
      return res.status(400).json({ error: 'Invalid deviceId format' });
    }

    // Validate startTime is a number
    if (typeof startTime !== 'number' || isNaN(startTime)) {
      return res.status(400).json({ error: 'startTime must be a valid timestamp' });
    }

    // Validate points array
    if (!Array.isArray(points)) {
      return res.status(400).json({ error: 'points must be an array' });
    }

    // Limit points to prevent DOS (max 100k points per walk)
    if (points.length > 100000) {
      return res.status(400).json({ error: 'Too many points (max 100000)' });
    }

    // Validate and filter points - only include valid ones
    const validPoints = points.filter(p => {
      if (!p || typeof p !== 'object') return false;
      if (typeof p.lat !== 'number' || isNaN(p.lat)) return false;
      if (typeof p.lon !== 'number' || isNaN(p.lon)) return false;
      if (p.lat < -90 || p.lat > 90) return false;
      if (p.lon < -180 || p.lon > 180) return false;
      return true;
    });

    // Update device last_seen
    await query(`
      INSERT INTO devices (device_id, last_seen)
      VALUES ($1, CURRENT_TIMESTAMP)
      ON CONFLICT (device_id)
      DO UPDATE SET last_seen = CURRENT_TIMESTAMP
    `, [deviceId]);

    // Calculate walk statistics from validated points
    let totalDistance = 0;
    let maxSpeed = 0;
    let avgSpeed = 0;
    let endLat = startLat;
    let endLon = startLon;
    let durationSeconds = 0;

    if (validPoints.length > 0) {
      // Calculate distance and speeds
      for (let i = 0; i < validPoints.length; i++) {
        const point = validPoints[i];

        if (point.spd > maxSpeed) {
          maxSpeed = point.spd;
        }
        avgSpeed += point.spd || 0;

        if (i > 0) {
          const prevPoint = validPoints[i - 1];
          const dist = haversineDistance(
            prevPoint.lat, prevPoint.lon,
            point.lat, point.lon
          );
          totalDistance += dist;
        }
      }

      avgSpeed = avgSpeed / validPoints.length;
      const lastPoint = validPoints[validPoints.length - 1];
      endLat = lastPoint.lat;
      endLon = lastPoint.lon;
      durationSeconds = lastPoint.t || 0;
    }

    // Insert walk record
    const walkResult = await query(`
      INSERT INTO walks (
        device_id, filename, start_time, end_time,
        duration_seconds, total_distance_meters,
        avg_speed_kmh, max_speed_kmh,
        start_lat, start_lon, end_lat, end_lon,
        point_count
      ) VALUES ($1, $2, to_timestamp($3), to_timestamp($4), $5, $6, $7, $8, $9, $10, $11, $12, $13)
      RETURNING id
    `, [
      deviceId,
      filename || null,
      startTime,
      startTime + durationSeconds,
      Math.round(durationSeconds),
      totalDistance,
      avgSpeed,
      maxSpeed,
      startLat || null,
      startLon || null,
      endLat || null,
      endLon || null,
      validPoints.length
    ]);

    const walkId = walkResult.rows[0].id;

    // Insert all validated points in batches
    if (validPoints.length > 0) {
      const batchSize = 100;
      for (let i = 0; i < validPoints.length; i += batchSize) {
        const batch = validPoints.slice(i, i + batchSize);
        const batchValues = batch.map((_, idx) => {
          const base = idx * 8;
          return `($1, $${base + 2}, $${base + 3}, $${base + 4}, $${base + 5}, $${base + 6}, $${base + 7}, $${base + 8}, $${base + 9})`;
        }).join(', ');

        const batchParams = [walkId];
        batch.forEach(p => {
          batchParams.push(
            p.t || 0,
            p.lat,
            p.lon,
            p.alt || null,
            p.spd || null,
            p.ax || null,
            p.ay || null,
            p.az || null
          );
        });

        await query(`
          INSERT INTO walk_points (
            walk_id, time_offset_seconds, latitude, longitude,
            altitude, speed_kmh, accel_x, accel_y, accel_z
          ) VALUES ${batchValues}
        `, batchParams);
      }
    }

    console.log(`Walk uploaded: ID=${walkId}, Device=${deviceId}, Points=${validPoints.length}`);

    res.status(201).json({
      success: true,
      walkId: walkId,
      message: 'Walk data uploaded successfully',
      stats: {
        pointCount: validPoints.length,
        durationSeconds: Math.round(durationSeconds),
        distanceMeters: Math.round(totalDistance),
        avgSpeedKmh: avgSpeed.toFixed(2),
        maxSpeedKmh: maxSpeed.toFixed(2)
      }
    });
  } catch (error) {
    next(error);
  }
});

/**
 * DELETE /api/walks/:id
 * Delete a walk and its points
 */
walkRoutes.delete('/:id', async (req, res, next) => {
  try {
    const { id } = req.params;

    const result = await query('DELETE FROM walks WHERE id = $1 RETURNING id', [id]);

    if (result.rows.length === 0) {
      return res.status(404).json({ error: 'Walk not found' });
    }

    res.json({ success: true, message: 'Walk deleted' });
  } catch (error) {
    next(error);
  }
});

// Helper functions

function formatWalk(row) {
  return {
    id: row.id,
    deviceId: row.device_id,
    filename: row.filename,
    startTime: row.start_time,
    endTime: row.end_time,
    durationSeconds: row.duration_seconds,
    durationFormatted: formatDuration(row.duration_seconds),
    totalDistanceMeters: row.total_distance_meters,
    totalDistanceKm: (row.total_distance_meters / 1000).toFixed(2),
    avgSpeedKmh: parseFloat(row.avg_speed_kmh?.toFixed(2) || 0),
    maxSpeedKmh: parseFloat(row.max_speed_kmh?.toFixed(2) || 0),
    startLocation: row.start_lat ? { lat: row.start_lat, lon: row.start_lon } : null,
    endLocation: row.end_lat ? { lat: row.end_lat, lon: row.end_lon } : null,
    pointCount: row.point_count,
    uploadedAt: row.uploaded_at
  };
}

function formatDuration(seconds) {
  if (!seconds) return '0:00';
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (h > 0) {
    return `${h}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  }
  return `${m}:${s.toString().padStart(2, '0')}`;
}

function haversineDistance(lat1, lon1, lat2, lon2) {
  const R = 6371000; // Earth's radius in meters
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLon = (lon2 - lon1) * Math.PI / 180;
  const a =
    Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
    Math.sin(dLon / 2) * Math.sin(dLon / 2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return R * c;
}

function simplifyPath(points, maxPoints) {
  if (points.length <= maxPoints) return points;

  const step = Math.ceil(points.length / maxPoints);
  const simplified = [];

  for (let i = 0; i < points.length; i += step) {
    simplified.push(points[i]);
  }

  // Always include last point
  if (simplified[simplified.length - 1] !== points[points.length - 1]) {
    simplified.push(points[points.length - 1]);
  }

  return simplified;
}
