/**
 * Device Routes
 *
 * API endpoints for managing tracker devices.
 */

import { Router } from 'express';
import { query } from '../db/init.js';

export const deviceRoutes = Router();

/**
 * GET /api/devices
 * List all registered devices
 */
deviceRoutes.get('/', async (req, res, next) => {
  try {
    const result = await query(`
      SELECT
        d.*,
        COUNT(w.id) as walk_count,
        MAX(w.start_time) as last_walk_time,
        COALESCE(SUM(w.total_distance_meters), 0) as total_distance
      FROM devices d
      LEFT JOIN walks w ON d.device_id = w.device_id
      GROUP BY d.id
      ORDER BY d.last_seen DESC
    `);

    res.json({
      devices: result.rows.map(row => ({
        id: row.id,
        deviceId: row.device_id,
        name: row.name || row.device_id,
        registeredAt: row.registered_at,
        lastSeen: row.last_seen,
        walkCount: parseInt(row.walk_count),
        lastWalkTime: row.last_walk_time,
        totalDistanceKm: (parseFloat(row.total_distance) / 1000).toFixed(2),
        metadata: row.metadata
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/devices/:id
 * Get device details
 */
deviceRoutes.get('/:id', async (req, res, next) => {
  try {
    const { id } = req.params;

    const result = await query(`
      SELECT * FROM devices WHERE device_id = $1 OR id::text = $1
    `, [id]);

    if (result.rows.length === 0) {
      return res.status(404).json({ error: 'Device not found' });
    }

    const device = result.rows[0];

    // Get device statistics
    const statsResult = await query(`
      SELECT
        COUNT(*) as walk_count,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(SUM(duration_seconds), 0) as total_duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        MAX(max_speed_kmh) as max_speed
      FROM walks
      WHERE device_id = $1
    `, [device.device_id]);

    const stats = statsResult.rows[0];

    res.json({
      id: device.id,
      deviceId: device.device_id,
      name: device.name || device.device_id,
      registeredAt: device.registered_at,
      lastSeen: device.last_seen,
      metadata: device.metadata,
      stats: {
        walkCount: parseInt(stats.walk_count),
        totalDistanceKm: (parseFloat(stats.total_distance) / 1000).toFixed(2),
        totalDurationHours: (parseFloat(stats.total_duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(stats.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(stats.max_speed || 0).toFixed(2)
      }
    });
  } catch (error) {
    next(error);
  }
});

/**
 * POST /api/devices/register
 * Register a new device
 */
deviceRoutes.post('/register', async (req, res, next) => {
  try {
    const { deviceId, name, metadata = {} } = req.body;

    if (!deviceId) {
      return res.status(400).json({ error: 'deviceId is required' });
    }

    const result = await query(`
      INSERT INTO devices (device_id, name, metadata, last_seen)
      VALUES ($1, $2, $3, CURRENT_TIMESTAMP)
      ON CONFLICT (device_id)
      DO UPDATE SET
        name = COALESCE($2, devices.name),
        metadata = devices.metadata || $3,
        last_seen = CURRENT_TIMESTAMP
      RETURNING *
    `, [deviceId, name || null, JSON.stringify(metadata)]);

    res.status(201).json({
      success: true,
      device: {
        id: result.rows[0].id,
        deviceId: result.rows[0].device_id,
        name: result.rows[0].name,
        registeredAt: result.rows[0].registered_at
      }
    });
  } catch (error) {
    next(error);
  }
});

/**
 * PUT /api/devices/:id
 * Update device info
 */
deviceRoutes.put('/:id', async (req, res, next) => {
  try {
    const { id } = req.params;
    const { name, metadata } = req.body;

    const updates = [];
    const params = [];
    let paramIndex = 1;

    if (name !== undefined) {
      updates.push(`name = $${paramIndex++}`);
      params.push(name);
    }

    if (metadata !== undefined) {
      updates.push(`metadata = metadata || $${paramIndex++}`);
      params.push(JSON.stringify(metadata));
    }

    if (updates.length === 0) {
      return res.status(400).json({ error: 'No fields to update' });
    }

    params.push(id);

    const result = await query(`
      UPDATE devices
      SET ${updates.join(', ')}
      WHERE device_id = $${paramIndex} OR id::text = $${paramIndex}
      RETURNING *
    `, params);

    if (result.rows.length === 0) {
      return res.status(404).json({ error: 'Device not found' });
    }

    res.json({
      success: true,
      device: {
        id: result.rows[0].id,
        deviceId: result.rows[0].device_id,
        name: result.rows[0].name,
        metadata: result.rows[0].metadata
      }
    });
  } catch (error) {
    next(error);
  }
});

/**
 * DELETE /api/devices/:id
 * Delete a device (walks will remain)
 */
deviceRoutes.delete('/:id', async (req, res, next) => {
  try {
    const { id } = req.params;

    const result = await query(`
      DELETE FROM devices
      WHERE device_id = $1 OR id::text = $1
      RETURNING id
    `, [id]);

    if (result.rows.length === 0) {
      return res.status(404).json({ error: 'Device not found' });
    }

    res.json({ success: true, message: 'Device deleted' });
  } catch (error) {
    next(error);
  }
});
