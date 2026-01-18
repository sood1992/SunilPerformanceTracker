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
 * GET /api/stats/date/:date
 * Get detailed stats for a specific date
 */
statsRoutes.get('/date/:date', async (req, res, next) => {
  try {
    const { date } = req.params;
    const { device_id } = req.query;

    let deviceFilter = '';
    const params = [date];

    if (device_id) {
      deviceFilter = 'AND device_id = $2';
      params.push(device_id);
    }

    // Get summary for the date
    const summaryResult = await query(`
      SELECT
        COUNT(*) as total_walks,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(SUM(duration_seconds), 0) as total_duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed,
        COALESCE(MIN(start_time), NULL) as first_walk,
        COALESCE(MAX(start_time), NULL) as last_walk
      FROM walks
      WHERE DATE(start_time) = $1
      ${deviceFilter}
    `, params);

    // Get individual walks for the date
    const walksResult = await query(`
      SELECT
        id,
        device_id,
        start_time,
        end_time,
        duration_seconds,
        total_distance_meters,
        avg_speed_kmh,
        max_speed_kmh,
        point_count
      FROM walks
      WHERE DATE(start_time) = $1
      ${deviceFilter}
      ORDER BY start_time ASC
    `, params);

    const summary = summaryResult.rows[0];

    res.json({
      date,
      summary: {
        totalWalks: parseInt(summary.total_walks),
        totalDistanceKm: (parseFloat(summary.total_distance) / 1000).toFixed(2),
        totalDurationMinutes: Math.round(parseFloat(summary.total_duration) / 60),
        avgSpeedKmh: parseFloat(summary.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(summary.max_speed).toFixed(2),
        firstWalk: summary.first_walk,
        lastWalk: summary.last_walk
      },
      walks: walksResult.rows.map(w => ({
        id: w.id,
        deviceId: w.device_id,
        startTime: w.start_time,
        endTime: w.end_time,
        durationMinutes: Math.round(parseFloat(w.duration_seconds) / 60),
        distanceKm: (parseFloat(w.total_distance_meters) / 1000).toFixed(2),
        avgSpeedKmh: parseFloat(w.avg_speed_kmh).toFixed(2),
        maxSpeedKmh: parseFloat(w.max_speed_kmh).toFixed(2),
        pointCount: w.point_count
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/monthly
 * Get monthly statistics for the last N months
 */
statsRoutes.get('/monthly', async (req, res, next) => {
  try {
    const { device_id, months = 12 } = req.query;

    let deviceFilter = '';
    const params = [parseInt(months)];

    if (device_id) {
      deviceFilter = 'AND device_id = $2';
      params.push(device_id);
    }

    const result = await query(`
      SELECT
        date_trunc('month', start_time)::date as month_start,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed,
        COUNT(DISTINCT DATE(start_time)) as active_days
      FROM walks
      WHERE start_time >= date_trunc('month', CURRENT_DATE) - INTERVAL '1 month' * $1
      ${deviceFilter}
      GROUP BY date_trunc('month', start_time)
      ORDER BY month_start DESC
    `, params);

    res.json({
      monthly: result.rows.map(row => ({
        monthStart: row.month_start.toISOString().split('T')[0],
        month: new Date(row.month_start).toLocaleString('en-US', { month: 'long', year: 'numeric' }),
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationHours: (parseFloat(row.duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(row.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(row.max_speed).toFixed(2),
        activeDays: parseInt(row.active_days),
        avgWalksPerDay: (parseInt(row.walks) / parseInt(row.active_days)).toFixed(1)
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/yearly
 * Get yearly statistics
 */
statsRoutes.get('/yearly', async (req, res, next) => {
  try {
    const { device_id } = req.query;

    let deviceFilter = '';
    const params = [];

    if (device_id) {
      deviceFilter = 'WHERE device_id = $1';
      params.push(device_id);
    }

    const result = await query(`
      SELECT
        EXTRACT(YEAR FROM start_time)::integer as year,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed,
        COUNT(DISTINCT DATE(start_time)) as active_days,
        COUNT(DISTINCT date_trunc('month', start_time)) as active_months
      FROM walks
      ${deviceFilter}
      GROUP BY EXTRACT(YEAR FROM start_time)
      ORDER BY year DESC
    `, params);

    res.json({
      yearly: result.rows.map(row => ({
        year: row.year,
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationHours: (parseFloat(row.duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(row.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(row.max_speed).toFixed(2),
        activeDays: parseInt(row.active_days),
        activeMonths: parseInt(row.active_months),
        avgDistancePerWalkKm: (parseFloat(row.distance) / 1000 / parseInt(row.walks)).toFixed(2)
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/range
 * Get stats for a specific date range
 */
statsRoutes.get('/range', async (req, res, next) => {
  try {
    const { start_date, end_date, device_id } = req.query;

    if (!start_date || !end_date) {
      return res.status(400).json({ error: 'start_date and end_date are required' });
    }

    let deviceFilter = '';
    const params = [start_date, end_date];

    if (device_id) {
      deviceFilter = 'AND device_id = $3';
      params.push(device_id);
    }

    // Get summary for the range
    const summaryResult = await query(`
      SELECT
        COUNT(*) as total_walks,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(SUM(duration_seconds), 0) as total_duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed,
        COUNT(DISTINCT DATE(start_time)) as active_days
      FROM walks
      WHERE DATE(start_time) >= $1 AND DATE(start_time) <= $2
      ${deviceFilter}
    `, params);

    // Get daily breakdown for the range
    const dailyResult = await query(`
      SELECT
        DATE(start_time) as date,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed
      FROM walks
      WHERE DATE(start_time) >= $1 AND DATE(start_time) <= $2
      ${deviceFilter}
      GROUP BY DATE(start_time)
      ORDER BY date ASC
    `, params);

    const summary = summaryResult.rows[0];

    res.json({
      startDate: start_date,
      endDate: end_date,
      summary: {
        totalWalks: parseInt(summary.total_walks),
        totalDistanceKm: (parseFloat(summary.total_distance) / 1000).toFixed(2),
        totalDurationHours: (parseFloat(summary.total_duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(summary.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(summary.max_speed).toFixed(2),
        activeDays: parseInt(summary.active_days)
      },
      daily: dailyResult.rows.map(row => ({
        date: row.date.toISOString().split('T')[0],
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationMinutes: Math.round(parseFloat(row.duration) / 60),
        avgSpeedKmh: parseFloat(row.avg_speed).toFixed(2)
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/month/:year/:month
 * Get detailed stats for a specific month
 */
statsRoutes.get('/month/:year/:month', async (req, res, next) => {
  try {
    const { year, month } = req.params;
    const { device_id } = req.query;

    let deviceFilter = '';
    const params = [parseInt(year), parseInt(month)];

    if (device_id) {
      deviceFilter = 'AND device_id = $3';
      params.push(device_id);
    }

    // Get summary for the month
    const summaryResult = await query(`
      SELECT
        COUNT(*) as total_walks,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(SUM(duration_seconds), 0) as total_duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed,
        COUNT(DISTINCT DATE(start_time)) as active_days
      FROM walks
      WHERE EXTRACT(YEAR FROM start_time) = $1 AND EXTRACT(MONTH FROM start_time) = $2
      ${deviceFilter}
    `, params);

    // Get daily breakdown for the month
    const dailyResult = await query(`
      SELECT
        DATE(start_time) as date,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration
      FROM walks
      WHERE EXTRACT(YEAR FROM start_time) = $1 AND EXTRACT(MONTH FROM start_time) = $2
      ${deviceFilter}
      GROUP BY DATE(start_time)
      ORDER BY date ASC
    `, params);

    const summary = summaryResult.rows[0];
    const monthName = new Date(year, month - 1).toLocaleString('en-US', { month: 'long', year: 'numeric' });

    res.json({
      year: parseInt(year),
      month: parseInt(month),
      monthName,
      summary: {
        totalWalks: parseInt(summary.total_walks),
        totalDistanceKm: (parseFloat(summary.total_distance) / 1000).toFixed(2),
        totalDurationHours: (parseFloat(summary.total_duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(summary.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(summary.max_speed).toFixed(2),
        activeDays: parseInt(summary.active_days)
      },
      daily: dailyResult.rows.map(row => ({
        date: row.date.toISOString().split('T')[0],
        day: new Date(row.date).getDate(),
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationMinutes: Math.round(parseFloat(row.duration) / 60)
      }))
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/stats/year/:year
 * Get detailed stats for a specific year
 */
statsRoutes.get('/year/:year', async (req, res, next) => {
  try {
    const { year } = req.params;
    const { device_id } = req.query;

    let deviceFilter = '';
    const params = [parseInt(year)];

    if (device_id) {
      deviceFilter = 'AND device_id = $2';
      params.push(device_id);
    }

    // Get summary for the year
    const summaryResult = await query(`
      SELECT
        COUNT(*) as total_walks,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(SUM(duration_seconds), 0) as total_duration,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed,
        COUNT(DISTINCT DATE(start_time)) as active_days
      FROM walks
      WHERE EXTRACT(YEAR FROM start_time) = $1
      ${deviceFilter}
    `, params);

    // Get monthly breakdown for the year
    const monthlyResult = await query(`
      SELECT
        EXTRACT(MONTH FROM start_time)::integer as month,
        COUNT(*) as walks,
        COALESCE(SUM(total_distance_meters), 0) as distance,
        COALESCE(SUM(duration_seconds), 0) as duration,
        COUNT(DISTINCT DATE(start_time)) as active_days
      FROM walks
      WHERE EXTRACT(YEAR FROM start_time) = $1
      ${deviceFilter}
      GROUP BY EXTRACT(MONTH FROM start_time)
      ORDER BY month ASC
    `, params);

    const summary = summaryResult.rows[0];
    const monthNames = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

    res.json({
      year: parseInt(year),
      summary: {
        totalWalks: parseInt(summary.total_walks),
        totalDistanceKm: (parseFloat(summary.total_distance) / 1000).toFixed(2),
        totalDurationHours: (parseFloat(summary.total_duration) / 3600).toFixed(2),
        avgSpeedKmh: parseFloat(summary.avg_speed).toFixed(2),
        maxSpeedKmh: parseFloat(summary.max_speed).toFixed(2),
        activeDays: parseInt(summary.active_days)
      },
      monthly: monthlyResult.rows.map(row => ({
        month: row.month,
        monthName: monthNames[row.month - 1],
        walks: parseInt(row.walks),
        distanceKm: (parseFloat(row.distance) / 1000).toFixed(2),
        durationHours: (parseFloat(row.duration) / 3600).toFixed(2),
        activeDays: parseInt(row.active_days)
      }))
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
