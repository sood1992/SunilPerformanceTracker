/**
 * Statistics Routes
 *
 * API endpoints for walk statistics and analytics.
 */

import { Router } from 'express';
import { query } from '../db/init.js';

export const statsRoutes = Router();

/**
 * GET /api/stats/summary
 * Get overall statistics summary
 */
statsRoutes.get('/summary', async (req, res, next) => {
  try {
    const { device_id } = req.query;

    let whereClause = '';
    const params = [];

    if (device_id) {
      whereClause = 'WHERE device_id = $1';
      params.push(device_id);
    }

    const result = await query(`
      SELECT
        COUNT(*) as total_walks,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(SUM(duration_seconds), 0) as total_duration,
        COALESCE(AVG(total_distance_meters), 0) as avg_distance,
        COALESCE(AVG(duration_seconds), 0) as avg_duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed_ever,
        COALESCE(MAX(total_distance_meters), 0) as longest_walk,
        MIN(start_time) as first_walk,
        MAX(start_time) as last_walk
      FROM walks
      ${whereClause}
    `, params);

    const stats = result.rows[0];

    // Get this week's stats
    const weekResult = await query(`
      SELECT
        COUNT(*) as week_walks,
        COALESCE(SUM(total_distance_meters), 0) as week_distance,
        COALESCE(SUM(duration_seconds), 0) as week_duration
      FROM walks
      WHERE start_time >= date_trunc('week', CURRENT_DATE)
      ${device_id ? 'AND device_id = $1' : ''}
    `, params);

    const weekStats = weekResult.rows[0];

    // Get today's stats
    const todayResult = await query(`
      SELECT
        COUNT(*) as today_walks,
        COALESCE(SUM(total_distance_meters), 0) as today_distance,
        COALESCE(SUM(duration_seconds), 0) as today_duration
      FROM walks
      WHERE DATE(start_time) = CURRENT_DATE
      ${device_id ? 'AND device_id = $1' : ''}
    `, params);

    const todayStats = todayResult.rows[0];

    res.json({
      allTime: {
        totalWalks: parseInt(stats.total_walks),
        totalDistanceKm: (parseFloat(stats.total_distance) / 1000).toFixed(2),
        totalDurationHours: (parseFloat(stats.total_duration) / 3600).toFixed(2),
        avgDistanceKm: (parseFloat(stats.avg_distance) / 1000).toFixed(2),
        avgDurationMinutes: Math.round(parseFloat(stats.avg_duration) / 60),
        avgSpeedKmh: parseFloat(stats.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(stats.max_speed_ever).toFixed(2),
        longestWalkKm: (parseFloat(stats.longest_walk) / 1000).toFixed(2),
        firstWalk: stats.first_walk,
        lastWalk: stats.last_walk
      },
      thisWeek: {
        walks: parseInt(weekStats.week_walks),
        distanceKm: (parseFloat(weekStats.week_distance) / 1000).toFixed(2),
        durationHours: (parseFloat(weekStats.week_duration) / 3600).toFixed(2)
      },
      today: {
        walks: parseInt(todayStats.today_walks),
        distanceKm: (parseFloat(todayStats.today_distance) / 1000).toFixed(2),
        durationMinutes: Math.round(parseFloat(todayStats.today_duration) / 60)
      }
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/daily
 * Get daily statistics for the last N days
 */
statsRoutes.get('/daily', async (req, res, next) => {
  try {
    const { device_id, days = 30 } = req.query;

    let whereClause = '';
    const params = [parseInt(days)];

    if (device_id) {
      whereClause = 'AND device_id = $2';
      params.push(device_id);
    }

    const result = await query(`
      SELECT
        DATE(start_time) as date,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed
      FROM walks
      WHERE start_time >= CURRENT_DATE - INTERVAL '1 day' * $1
      ${whereClause}
      GROUP BY DATE(start_time)
      ORDER BY date DESC
    `, params);

    // Fill in missing days
    const statsMap = new Map();
    result.rows.forEach(row => {
      statsMap.set(row.date.toISOString().split('T')[0], {
        date: row.date.toISOString().split('T')[0],
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationMinutes: Math.round(parseFloat(row.duration) / 60),
        avgSpeedKmh: parseFloat(row.avg_speed).toFixed(2)
      });
    });

    const dailyStats = [];
    const today = new Date();
    for (let i = 0; i < parseInt(days); i++) {
      const date = new Date(today);
      date.setDate(date.getDate() - i);
      const dateStr = date.toISOString().split('T')[0];

      dailyStats.push(statsMap.get(dateStr) || {
        date: dateStr,
        walks: 0,
        distanceKm: '0.00',
        durationMinutes: 0,
        avgSpeedKmh: '0.00'
      });
    }

    res.json({ daily: dailyStats });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/weekly
 * Get weekly statistics for the last N weeks
 */
statsRoutes.get('/weekly', async (req, res, next) => {
  try {
    const { device_id, weeks = 12 } = req.query;

    let whereClause = '';
    const params = [parseInt(weeks)];

    if (device_id) {
      whereClause = 'AND device_id = $2';
      params.push(device_id);
    }

    const result = await query(`
      SELECT
        date_trunc('week', start_time)::date as week_start,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        MAX(max_speed_kmh) as max_speed
      FROM walks
      WHERE start_time >= date_trunc('week', CURRENT_DATE) - INTERVAL '1 week' * $1
      ${whereClause}
      GROUP BY date_trunc('week', start_time)
      ORDER BY week_start DESC
    `, params);

    res.json({
      weekly: result.rows.map(row => ({
        weekStart: row.week_start.toISOString().split('T')[0],
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationHours: (parseFloat(row.duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(row.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(row.max_speed || 0).toFixed(2)
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/heatmap
 * Get data for generating a heatmap of walk locations
 */
statsRoutes.get('/heatmap', async (req, res, next) => {
  try {
    const { device_id, limit = 10000 } = req.query;

    let whereClause = '';
    const params = [parseInt(limit)];

    if (device_id) {
      whereClause = 'AND w.device_id = $2';
      params.push(device_id);
    }

    const result = await query(`
      SELECT
        wp.latitude as lat,
        wp.longitude as lon
      FROM walk_points wp
      JOIN walks w ON wp.walk_id = w.id
      WHERE 1=1 ${whereClause}
      ORDER BY RANDOM()
      LIMIT $1
    `, params);

    res.json({
      points: result.rows.map(row => [row.lat, row.lon])
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/speed-distribution
 * Get speed distribution data
 */
statsRoutes.get('/speed-distribution', async (req, res, next) => {
  try {
    const { device_id } = req.query;

    let whereClause = '';
    const params = [];

    if (device_id) {
      whereClause = 'WHERE w.device_id = $1';
      params.push(device_id);
    }

    // Get speed in 1 km/h buckets
    const result = await query(`
      SELECT
        FLOOR(speed_kmh) as speed_bucket,
        COUNT(*) as count
      FROM walk_points wp
      JOIN walks w ON wp.walk_id = w.id
      ${whereClause}
      ${whereClause ? 'AND' : 'WHERE'} speed_kmh IS NOT NULL AND speed_kmh > 0
      GROUP BY FLOOR(speed_kmh)
      ORDER BY speed_bucket
    `, params);

    res.json({
      distribution: result.rows.map(row => ({
        speedKmh: parseInt(row.speed_bucket),
        count: parseInt(row.count)
      }))
    });
  } catch (error) {
    next(error);
  }
});
