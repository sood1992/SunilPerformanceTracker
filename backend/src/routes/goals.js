/**
 * Goals & Dog Profile Routes
 *
 * Daily exercise goals based on dog's needs.
 * Configured for Popcorn (Mini Poodle, 1.5 years, 7.5kg)
 */

import { Router } from 'express';
import { query } from '../db/init.js';

export const goalsRoutes = Router();

// Popcorn's Profile - Mini Poodle Exercise Requirements
// Based on veterinary guidelines for adult mini poodles
const DOG_PROFILE = {
  name: 'Popcorn',
  breed: 'Mini Poodle',
  ageYears: 1.5,
  weightKg: 7.5,

  // Daily Exercise Requirements
  dailyGoals: {
    totalMinutes: 60,           // 60 minutes total exercise per day
    walkingMinutes: 40,         // 30-40 minutes of actual walking
    distanceKm: 2.5,            // 1.6-3.2 km (target middle)
    minWalks: 3,                // At least 3 walks per day (Popcorn typically does 3-4)
    maxRestPercent: 30,         // Max 30% of walk time resting/sitting
  },

  // Optimal Walking Parameters
  optimalWalking: {
    speedMinKmh: 3.0,           // Minimum healthy walking pace
    speedMaxKmh: 5.5,           // Maximum comfortable pace
    speedOptimalKmh: 4.0,       // Ideal walking speed
  },

  // Activity Detection Thresholds
  activityThresholds: {
    sittingAccelMagnitude: 0.5,  // Below this = sitting/resting
    walkingAccelMagnitude: 1.5,  // Above this = active walking
    runningAccelMagnitude: 3.0,  // Above this = running/playing
  }
};

/**
 * GET /api/goals/profile
 * Get dog profile and exercise requirements
 */
goalsRoutes.get('/profile', (req, res) => {
  res.json({
    dog: DOG_PROFILE,
    description: `${DOG_PROFILE.name} is a ${DOG_PROFILE.ageYears} year old ${DOG_PROFILE.breed} weighing ${DOG_PROFILE.weightKg}kg. ` +
      `As a young adult mini poodle, ${DOG_PROFILE.name} needs ${DOG_PROFILE.dailyGoals.totalMinutes} minutes of exercise daily, ` +
      `including ${DOG_PROFILE.dailyGoals.walkingMinutes} minutes of walking covering about ${DOG_PROFILE.dailyGoals.distanceKm}km.`
  });
});

/**
 * GET /api/goals/today
 * Get today's progress towards daily goals
 */
goalsRoutes.get('/today', async (req, res, next) => {
  try {
    const { device_id } = req.query;

    let deviceFilter = '';
    const params = [];

    if (device_id) {
      deviceFilter = 'AND device_id = $1';
      params.push(device_id);
    }

    // Get today's walks
    const todayResult = await query(`
      SELECT
        COUNT(*) as walk_count,
        COALESCE(SUM(duration_seconds), 0) as total_seconds,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed,
        COALESCE(MAX(max_speed_kmh), 0) as max_speed
      FROM walks
      WHERE DATE(start_time) = CURRENT_DATE
      ${deviceFilter}
    `, params);

    const today = todayResult.rows[0];
    const totalMinutes = Math.round(parseFloat(today.total_seconds) / 60);
    const totalKm = parseFloat(today.total_distance) / 1000;

    // Calculate goal progress percentages
    const minutesProgress = Math.min(100, Math.round((totalMinutes / DOG_PROFILE.dailyGoals.totalMinutes) * 100));
    const distanceProgress = Math.min(100, Math.round((totalKm / DOG_PROFILE.dailyGoals.distanceKm) * 100));
    const walksProgress = Math.min(100, Math.round((parseInt(today.walk_count) / DOG_PROFILE.dailyGoals.minWalks) * 100));

    // Overall daily score (weighted average)
    const dailyScore = Math.round(
      (minutesProgress * 0.4) +
      (distanceProgress * 0.4) +
      (walksProgress * 0.2)
    );

    // Get activity breakdown from walk points
    const activityResult = await query(`
      SELECT
        wp.speed_kmh,
        wp.accel_x,
        wp.accel_y,
        wp.accel_z
      FROM walk_points wp
      JOIN walks w ON wp.walk_id = w.id
      WHERE DATE(w.start_time) = CURRENT_DATE
      ${deviceFilter ? 'AND w.device_id = $1' : ''}
    `, params);

    // Calculate activity breakdown
    let sittingPoints = 0;
    let walkingPoints = 0;
    let runningPoints = 0;

    activityResult.rows.forEach(point => {
      const accelMag = Math.sqrt(
        Math.pow(point.accel_x || 0, 2) +
        Math.pow(point.accel_y || 0, 2) +
        Math.pow(point.accel_z || 0, 2)
      );
      const deviation = Math.abs(accelMag - 9.8); // Remove gravity

      if (deviation < DOG_PROFILE.activityThresholds.sittingAccelMagnitude) {
        sittingPoints++;
      } else if (deviation >= DOG_PROFILE.activityThresholds.runningAccelMagnitude) {
        runningPoints++;
      } else {
        walkingPoints++;
      }
    });

    const totalPoints = activityResult.rows.length || 1;
    const sittingPercent = Math.round((sittingPoints / totalPoints) * 100);
    const walkingPercent = Math.round((walkingPoints / totalPoints) * 100);
    const runningPercent = Math.round((runningPoints / totalPoints) * 100);

    // Check if too much sitting
    const tooMuchResting = sittingPercent > DOG_PROFILE.dailyGoals.maxRestPercent;

    res.json({
      date: new Date().toISOString().split('T')[0],
      dogName: DOG_PROFILE.name,

      progress: {
        walks: {
          current: parseInt(today.walk_count),
          goal: DOG_PROFILE.dailyGoals.minWalks,
          percent: walksProgress,
          completed: parseInt(today.walk_count) >= DOG_PROFILE.dailyGoals.minWalks
        },
        minutes: {
          current: totalMinutes,
          goal: DOG_PROFILE.dailyGoals.totalMinutes,
          percent: minutesProgress,
          completed: totalMinutes >= DOG_PROFILE.dailyGoals.totalMinutes
        },
        distance: {
          currentKm: parseFloat(totalKm.toFixed(2)),
          goalKm: DOG_PROFILE.dailyGoals.distanceKm,
          percent: distanceProgress,
          completed: totalKm >= DOG_PROFILE.dailyGoals.distanceKm
        }
      },

      activity: {
        sittingPercent,
        walkingPercent,
        runningPercent,
        avgSpeedKmh: parseFloat(today.avg_speed).toFixed(1),
        maxSpeedKmh: parseFloat(today.max_speed).toFixed(1),
        tooMuchResting,
        restingWarning: tooMuchResting
          ? `${DOG_PROFILE.name} spent ${sittingPercent}% of walk time resting. Try to keep moving!`
          : null
      },

      dailyScore,
      goalMet: dailyScore >= 80,

      message: getDailyMessage(dailyScore, minutesProgress, totalMinutes),

      goals: DOG_PROFILE.dailyGoals
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/goals/history
 * Get goal completion history for last N days
 */
goalsRoutes.get('/history', async (req, res, next) => {
  try {
    const { device_id, days = 30 } = req.query;

    let deviceFilter = '';
    const params = [parseInt(days)];

    if (device_id) {
      deviceFilter = 'AND device_id = $2';
      params.push(device_id);
    }

    // Get daily aggregates
    const historyResult = await query(`
      SELECT
        DATE(start_time) as date,
        COUNT(*) as walk_count,
        COALESCE(SUM(duration_seconds), 0) as total_seconds,
        COALESCE(SUM(total_distance_meters), 0) as total_distance,
        COALESCE(AVG(avg_speed_kmh), 0) as avg_speed
      FROM walks
      WHERE start_time >= CURRENT_DATE - INTERVAL '1 day' * $1
      ${deviceFilter}
      GROUP BY DATE(start_time)
      ORDER BY date DESC
    `, params);

    // Calculate scores for each day
    const history = [];
    const today = new Date();

    for (let i = 0; i < parseInt(days); i++) {
      const date = new Date(today);
      date.setDate(date.getDate() - i);
      const dateStr = date.toISOString().split('T')[0];

      const dayData = historyResult.rows.find(r =>
        r.date.toISOString().split('T')[0] === dateStr
      );

      if (dayData) {
        const minutes = Math.round(parseFloat(dayData.total_seconds) / 60);
        const km = parseFloat(dayData.total_distance) / 1000;
        const walks = parseInt(dayData.walk_count);

        const minutesProgress = Math.min(100, Math.round((minutes / DOG_PROFILE.dailyGoals.totalMinutes) * 100));
        const distanceProgress = Math.min(100, Math.round((km / DOG_PROFILE.dailyGoals.distanceKm) * 100));
        const walksProgress = Math.min(100, Math.round((walks / DOG_PROFILE.dailyGoals.minWalks) * 100));

        const score = Math.round(
          (minutesProgress * 0.4) +
          (distanceProgress * 0.4) +
          (walksProgress * 0.2)
        );

        history.push({
          date: dateStr,
          walks,
          minutes,
          distanceKm: parseFloat(km.toFixed(2)),
          avgSpeedKmh: parseFloat(dayData.avg_speed).toFixed(1),
          score,
          goalMet: score >= 80
        });
      } else {
        history.push({
          date: dateStr,
          walks: 0,
          minutes: 0,
          distanceKm: 0,
          avgSpeedKmh: '0.0',
          score: 0,
          goalMet: false
        });
      }
    }

    // Calculate streak
    let currentStreak = 0;
    for (const day of history) {
      if (day.goalMet) {
        currentStreak++;
      } else {
        break;
      }
    }

    // Calculate 30-day average score
    const daysWithData = history.filter(d => d.walks > 0);
    const avgScore = daysWithData.length > 0
      ? Math.round(daysWithData.reduce((sum, d) => sum + d.score, 0) / daysWithData.length)
      : 0;

    // Calculate completion rate
    const goalsMetCount = history.filter(d => d.goalMet).length;
    const completionRate = Math.round((goalsMetCount / parseInt(days)) * 100);

    res.json({
      dogName: DOG_PROFILE.name,
      period: {
        days: parseInt(days),
        from: history[history.length - 1]?.date,
        to: history[0]?.date
      },
      summary: {
        currentStreak,
        longestStreak: calculateLongestStreak(history),
        averageScore: avgScore,
        completionRate,
        totalWalks: history.reduce((sum, d) => sum + d.walks, 0),
        totalMinutes: history.reduce((sum, d) => sum + d.minutes, 0),
        totalDistanceKm: parseFloat(history.reduce((sum, d) => sum + d.distanceKm, 0).toFixed(2))
      },
      history,
      goals: DOG_PROFILE.dailyGoals
    });
  } catch (error) {
    next(error);
  }
});

/**
 * GET /api/goals/alerts
 * Get alerts for owner/walker about exercise compliance
 */
goalsRoutes.get('/alerts', async (req, res, next) => {
  try {
    const { device_id } = req.query;

    let deviceFilter = '';
    const params = [];

    if (device_id) {
      deviceFilter = 'AND device_id = $1';
      params.push(device_id);
    }

    const alerts = [];

    // Check today's progress
    const todayResult = await query(`
      SELECT
        COUNT(*) as walk_count,
        COALESCE(SUM(duration_seconds), 0) as total_seconds,
        COALESCE(SUM(total_distance_meters), 0) as total_distance
      FROM walks
      WHERE DATE(start_time) = CURRENT_DATE
      ${deviceFilter}
    `, params);

    const today = todayResult.rows[0];
    const totalMinutes = Math.round(parseFloat(today.total_seconds) / 60);
    const totalKm = parseFloat(today.total_distance) / 1000;
    const walkCount = parseInt(today.walk_count);

    const currentHour = new Date().getHours();

    // Alert: No walk yet and it's afternoon
    if (walkCount === 0 && currentHour >= 12) {
      alerts.push({
        type: 'no_walk_yet',
        severity: currentHour >= 17 ? 'high' : 'medium',
        title: `${DOG_PROFILE.name} hasn't been walked today!`,
        message: `It's ${currentHour}:00 and ${DOG_PROFILE.name} needs ${DOG_PROFILE.dailyGoals.totalMinutes} minutes of exercise today.`,
        icon: 'warning'
      });
    }

    // Alert: Only one walk and it's evening
    if (walkCount === 1 && currentHour >= 17) {
      const remainingMinutes = DOG_PROFILE.dailyGoals.totalMinutes - totalMinutes;
      if (remainingMinutes > 10) {
        alerts.push({
          type: 'needs_second_walk',
          severity: 'medium',
          title: `${DOG_PROFILE.name} needs another walk`,
          message: `Only ${totalMinutes} minutes of exercise so far. ${DOG_PROFILE.name} needs ${remainingMinutes} more minutes today.`,
          icon: 'info'
        });
      }
    }

    // Alert: Goal almost complete - encouragement!
    const progressPercent = Math.round((totalMinutes / DOG_PROFILE.dailyGoals.totalMinutes) * 100);
    if (progressPercent >= 80 && progressPercent < 100) {
      alerts.push({
        type: 'almost_there',
        severity: 'low',
        title: 'Almost there!',
        message: `${DOG_PROFILE.name} is at ${progressPercent}% of today's goal. Just ${DOG_PROFILE.dailyGoals.totalMinutes - totalMinutes} more minutes!`,
        icon: 'star'
      });
    }

    // Alert: Goal completed!
    if (progressPercent >= 100) {
      alerts.push({
        type: 'goal_complete',
        severity: 'success',
        title: 'Daily goal achieved!',
        message: `Great job! ${DOG_PROFILE.name} got ${totalMinutes} minutes of exercise (${totalKm.toFixed(1)}km) today!`,
        icon: 'trophy'
      });
    }

    // Check for recent walks with too much sitting
    const recentWalkResult = await query(`
      SELECT w.id, w.duration_seconds
      FROM walks w
      WHERE DATE(w.start_time) = CURRENT_DATE
      ${deviceFilter ? 'AND w.device_id = $1' : ''}
      ORDER BY w.start_time DESC
      LIMIT 1
    `, params);

    if (recentWalkResult.rows.length > 0) {
      const walkId = recentWalkResult.rows[0].id;

      // Get activity breakdown for most recent walk
      const activityResult = await query(`
        SELECT accel_x, accel_y, accel_z
        FROM walk_points
        WHERE walk_id = $1
      `, [walkId]);

      let sittingPoints = 0;
      activityResult.rows.forEach(point => {
        const accelMag = Math.sqrt(
          Math.pow(point.accel_x || 0, 2) +
          Math.pow(point.accel_y || 0, 2) +
          Math.pow(point.accel_z || 0, 2)
        );
        const deviation = Math.abs(accelMag - 9.8);
        if (deviation < DOG_PROFILE.activityThresholds.sittingAccelMagnitude) {
          sittingPoints++;
        }
      });

      const sittingPercent = activityResult.rows.length > 0
        ? Math.round((sittingPoints / activityResult.rows.length) * 100)
        : 0;

      if (sittingPercent > DOG_PROFILE.dailyGoals.maxRestPercent) {
        alerts.push({
          type: 'too_much_sitting',
          severity: 'medium',
          title: 'Too much resting during walk',
          message: `${sittingPercent}% of the last walk was spent sitting/resting. Try to keep ${DOG_PROFILE.name} moving!`,
          icon: 'alert'
        });
      }
    }

    res.json({
      dogName: DOG_PROFILE.name,
      alertCount: alerts.length,
      alerts,
      currentProgress: {
        walks: walkCount,
        minutes: totalMinutes,
        distanceKm: parseFloat(totalKm.toFixed(2)),
        percentComplete: progressPercent
      }
    });
  } catch (error) {
    next(error);
  }
});

// Helper functions

function getDailyMessage(score, minutesProgress, totalMinutes) {
  if (score >= 100) {
    return `Perfect day! ${DOG_PROFILE.name} got all the exercise needed! 🏆`;
  } else if (score >= 80) {
    return `Great job! ${DOG_PROFILE.name}'s daily goal is met! Keep it up!`;
  } else if (score >= 60) {
    return `Good progress! ${DOG_PROFILE.name} needs ${DOG_PROFILE.dailyGoals.totalMinutes - totalMinutes} more minutes today.`;
  } else if (score >= 30) {
    return `${DOG_PROFILE.name} needs more exercise. Try to fit in another walk!`;
  } else if (score > 0) {
    return `Just getting started! ${DOG_PROFILE.name} needs ${DOG_PROFILE.dailyGoals.totalMinutes - totalMinutes} more minutes.`;
  } else {
    return `${DOG_PROFILE.name} hasn't been walked yet today. Time for a walk!`;
  }
}

function calculateLongestStreak(history) {
  let longest = 0;
  let current = 0;

  for (const day of history) {
    if (day.goalMet) {
      current++;
      if (current > longest) {
        longest = current;
      }
    } else {
      current = 0;
    }
  }

  return longest;
}
