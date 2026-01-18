/**
 * Accountability Routes
 *
 * API endpoints for dog walker accountability features.
 */

import { Router } from 'express';
import { query } from '../db/init.js';

export const accountabilityRoutes = Router();

// Walk requirements configuration
const WALK_REQUIREMENTS = {
  minDurationMinutes: 15,      // Minimum 15 minutes
  minDistanceMeters: 500,      // Minimum 500 meters
  minAvgSpeedKmh: 2.0,         // Minimum average speed (to ensure actual walking)
  maxAvgSpeedKmh: 8.0,         // Maximum speed (to detect driving)
  minDataPoints: 10,           // Minimum GPS points for valid walk
  suspiciousGapSeconds: 120,   // Gap > 2 min is suspicious (device turned off?)
};

/**
 * GET /api/accountability/score/:walkId
 * Calculate accountability score for a walk
 */
accountabilityRoutes.get('/score/:walkId', async (req, res, next) => {
  try {
    const { walkId } = req.params;

    // Get walk data
    const walkResult = await query('SELECT * FROM walks WHERE id = $1', [walkId]);
    if (walkResult.rows.length === 0) {
      return res.status(404).json({ error: 'Walk not found' });
    }

    const walk = walkResult.rows[0];

    // Get walk points for detailed analysis
    const pointsResult = await query(`
      SELECT time_offset_seconds, latitude, longitude, speed_kmh
      FROM walk_points
      WHERE walk_id = $1
      ORDER BY time_offset_seconds
    `, [walkId]);

    const points = pointsResult.rows;

    // Calculate score components
    const score = calculateWalkScore(walk, points);

    res.json({
      walkId: parseInt(walkId),
      score: score.total,
      grade: getGrade(score.total),
      breakdown: score.breakdown,
      flags: score.flags,
      requirements: WALK_REQUIREMENTS,
      passed: score.total >= 70
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/accountability/report
 * Get accountability report for a time period
 */
accountabilityRoutes.get('/report', async (req, res, next) => {
  try {
    const { device_id, from, to, days = 7 } = req.query;

    let dateFilter = '';
    const params = [];
    let paramIndex = 1;

    if (from && to) {
      dateFilter = `AND start_time >= $${paramIndex++} AND start_time <= $${paramIndex++}`;
      params.push(from, to);
    } else {
      dateFilter = `AND start_time >= CURRENT_DATE - INTERVAL '${parseInt(days)} days'`;
    }

    let deviceFilter = '';
    if (device_id) {
      deviceFilter = ` AND device_id = $${paramIndex++}`;
      params.push(device_id);
    }

    // Get all walks in the period
    const walksResult = await query(`
      SELECT
        id, device_id, start_time, duration_seconds,
        total_distance_meters, avg_speed_kmh, max_speed_kmh, point_count
      FROM walks
      WHERE 1=1 ${dateFilter} ${deviceFilter}
      ORDER BY start_time DESC
    `, params);

    // Calculate scores for each walk
    const walkScores = [];
    let totalScore = 0;
    let passedCount = 0;
    let flaggedCount = 0;

    for (const walk of walksResult.rows) {
      const pointsResult = await query(`
        SELECT time_offset_seconds, latitude, longitude, speed_kmh
        FROM walk_points WHERE walk_id = $1
        ORDER BY time_offset_seconds
      `, [walk.id]);

      const score = calculateWalkScore(walk, pointsResult.rows);
      totalScore += score.total;

      if (score.total >= 70) passedCount++;
      if (score.flags.length > 0) flaggedCount++;

      walkScores.push({
        walkId: walk.id,
        date: walk.start_time,
        deviceId: walk.device_id,
        duration: Math.round(walk.duration_seconds / 60),
        distance: Math.round(walk.total_distance_meters),
        score: score.total,
        grade: getGrade(score.total),
        flags: score.flags,
        passed: score.total >= 70
      });
    }

    const avgScore = walksResult.rows.length > 0
      ? Math.round(totalScore / walksResult.rows.length)
      : 0;

    res.json({
      period: {
        from: from || `${days} days ago`,
        to: to || 'now',
        days: parseInt(days)
      },
      summary: {
        totalWalks: walksResult.rows.length,
        passedWalks: passedCount,
        flaggedWalks: flaggedCount,
        passRate: walksResult.rows.length > 0
          ? Math.round((passedCount / walksResult.rows.length) * 100)
          : 0,
        averageScore: avgScore,
        overallGrade: getGrade(avgScore)
      },
      walks: walkScores,
      requirements: WALK_REQUIREMENTS
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/accountability/alerts
 * Get walks with accountability issues
 */
accountabilityRoutes.get('/alerts', async (req, res, next) => {
  try {
    const { device_id, days = 7 } = req.query;

    let deviceFilter = '';
    const params = [parseInt(days)];

    if (device_id) {
      deviceFilter = 'AND device_id = $2';
      params.push(device_id);
    }

    // Find walks that don't meet minimum requirements
    const alertsResult = await query(`
      SELECT
        id, device_id, start_time, duration_seconds,
        total_distance_meters, avg_speed_kmh, point_count
      FROM walks
      WHERE start_time >= CURRENT_DATE - INTERVAL '1 day' * $1
      ${deviceFilter}
      AND (
        duration_seconds < ${WALK_REQUIREMENTS.minDurationMinutes * 60}
        OR total_distance_meters < ${WALK_REQUIREMENTS.minDistanceMeters}
        OR avg_speed_kmh < ${WALK_REQUIREMENTS.minAvgSpeedKmh}
        OR avg_speed_kmh > ${WALK_REQUIREMENTS.maxAvgSpeedKmh}
        OR point_count < ${WALK_REQUIREMENTS.minDataPoints}
      )
      ORDER BY start_time DESC
    `, params);

    const alerts = alertsResult.rows.map(walk => {
      const issues = [];

      if (walk.duration_seconds < WALK_REQUIREMENTS.minDurationMinutes * 60) {
        issues.push({
          type: 'short_duration',
          message: `Walk too short (${Math.round(walk.duration_seconds / 60)} min < ${WALK_REQUIREMENTS.minDurationMinutes} min required)`
        });
      }

      if (walk.total_distance_meters < WALK_REQUIREMENTS.minDistanceMeters) {
        issues.push({
          type: 'short_distance',
          message: `Distance too short (${Math.round(walk.total_distance_meters)}m < ${WALK_REQUIREMENTS.minDistanceMeters}m required)`
        });
      }

      if (walk.avg_speed_kmh < WALK_REQUIREMENTS.minAvgSpeedKmh) {
        issues.push({
          type: 'too_slow',
          message: `Speed too slow (${walk.avg_speed_kmh?.toFixed(1)} km/h < ${WALK_REQUIREMENTS.minAvgSpeedKmh} km/h minimum)`
        });
      }

      if (walk.avg_speed_kmh > WALK_REQUIREMENTS.maxAvgSpeedKmh) {
        issues.push({
          type: 'too_fast',
          message: `Speed too fast (${walk.avg_speed_kmh?.toFixed(1)} km/h > ${WALK_REQUIREMENTS.maxAvgSpeedKmh} km/h maximum) - possible vehicle?`
        });
      }

      if (walk.point_count < WALK_REQUIREMENTS.minDataPoints) {
        issues.push({
          type: 'insufficient_data',
          message: `Too few GPS points (${walk.point_count} < ${WALK_REQUIREMENTS.minDataPoints} required)`
        });
      }

      return {
        walkId: walk.id,
        deviceId: walk.device_id,
        date: walk.start_time,
        issues
      };
    });

    res.json({
      alertCount: alerts.length,
      alerts
    });
  } catch (error) {
    next(error);
  }
});

// Helper functions

function calculateWalkScore(walk, points) {
  const breakdown = {};
  const flags = [];

  // Duration score (0-25 points)
  const durationMinutes = walk.duration_seconds / 60;
  if (durationMinutes >= WALK_REQUIREMENTS.minDurationMinutes) {
    breakdown.duration = Math.min(25, Math.round((durationMinutes / 30) * 25));
  } else {
    breakdown.duration = Math.round((durationMinutes / WALK_REQUIREMENTS.minDurationMinutes) * 15);
    flags.push(`Short duration: ${Math.round(durationMinutes)} min`);
  }

  // Distance score (0-25 points)
  const distanceMeters = walk.total_distance_meters || 0;
  if (distanceMeters >= WALK_REQUIREMENTS.minDistanceMeters) {
    breakdown.distance = Math.min(25, Math.round((distanceMeters / 2000) * 25));
  } else {
    breakdown.distance = Math.round((distanceMeters / WALK_REQUIREMENTS.minDistanceMeters) * 15);
    flags.push(`Short distance: ${Math.round(distanceMeters)}m`);
  }

  // Speed score (0-25 points)
  const avgSpeed = walk.avg_speed_kmh || 0;
  if (avgSpeed >= WALK_REQUIREMENTS.minAvgSpeedKmh && avgSpeed <= WALK_REQUIREMENTS.maxAvgSpeedKmh) {
    // Optimal speed range is 3-5 km/h
    if (avgSpeed >= 3 && avgSpeed <= 5) {
      breakdown.speed = 25;
    } else {
      breakdown.speed = 20;
    }
  } else if (avgSpeed < WALK_REQUIREMENTS.minAvgSpeedKmh) {
    breakdown.speed = 10;
    flags.push(`Too slow: ${avgSpeed.toFixed(1)} km/h`);
  } else {
    breakdown.speed = 5;
    flags.push(`Suspicious speed: ${avgSpeed.toFixed(1)} km/h (possible vehicle)`);
  }

  // Data quality score (0-25 points)
  const pointCount = points.length;
  if (pointCount >= WALK_REQUIREMENTS.minDataPoints) {
    breakdown.dataQuality = Math.min(25, Math.round((pointCount / 100) * 25));

    // Check for suspicious gaps in data
    let hasGaps = false;
    for (let i = 1; i < points.length; i++) {
      const gap = points[i].time_offset_seconds - points[i-1].time_offset_seconds;
      if (gap > WALK_REQUIREMENTS.suspiciousGapSeconds) {
        hasGaps = true;
        break;
      }
    }
    if (hasGaps) {
      breakdown.dataQuality = Math.max(10, breakdown.dataQuality - 10);
      flags.push('Suspicious data gaps detected');
    }
  } else {
    breakdown.dataQuality = Math.round((pointCount / WALK_REQUIREMENTS.minDataPoints) * 15);
    flags.push(`Insufficient GPS data: ${pointCount} points`);
  }

  // Calculate total
  const total = Object.values(breakdown).reduce((sum, val) => sum + val, 0);

  return {
    total: Math.min(100, total),
    breakdown,
    flags
  };
}

function getGrade(score) {
  if (score >= 90) return 'A';
  if (score >= 80) return 'B';
  if (score >= 70) return 'C';
  if (score >= 60) return 'D';
  return 'F';
}
