/**
 * Database Initialization
 *
 * Creates tables and indexes for the dog walker tracker.
 */

import pg from 'pg';

const { Pool } = pg;

let pool = null;

export function getPool() {
  if (!pool) {
    pool = new Pool({
      connectionString: process.env.DATABASE_URL,
      ssl: process.env.NODE_ENV === 'production' ? { rejectUnauthorized: false } : false
    });
  }
  return pool;
}

export async function query(text, params) {
  const client = await getPool().connect();
  try {
    const result = await client.query(text, params);
    return result;
  } finally {
    client.release();
  }
}

export async function initDatabase() {
  console.log('Initializing database...');

  // Create devices table
  await query(`
    CREATE TABLE IF NOT EXISTS devices (
      id SERIAL PRIMARY KEY,
      device_id VARCHAR(50) UNIQUE NOT NULL,
      name VARCHAR(100),
      registered_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      last_seen TIMESTAMP,
      metadata JSONB DEFAULT '{}'
    )
  `);

  // Create walks table
  await query(`
    CREATE TABLE IF NOT EXISTS walks (
      id SERIAL PRIMARY KEY,
      device_id VARCHAR(50) NOT NULL,
      filename VARCHAR(255),
      start_time TIMESTAMP NOT NULL,
      end_time TIMESTAMP,
      duration_seconds INTEGER,
      total_distance_meters DOUBLE PRECISION DEFAULT 0,
      avg_speed_kmh DOUBLE PRECISION DEFAULT 0,
      max_speed_kmh DOUBLE PRECISION DEFAULT 0,
      start_lat DOUBLE PRECISION,
      start_lon DOUBLE PRECISION,
      end_lat DOUBLE PRECISION,
      end_lon DOUBLE PRECISION,
      point_count INTEGER DEFAULT 0,
      uploaded_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      metadata JSONB DEFAULT '{}'
    )
  `);

  // Create walk_points table for GPS data
  await query(`
    CREATE TABLE IF NOT EXISTS walk_points (
      id SERIAL PRIMARY KEY,
      walk_id INTEGER REFERENCES walks(id) ON DELETE CASCADE,
      time_offset_seconds DOUBLE PRECISION,
      latitude DOUBLE PRECISION NOT NULL,
      longitude DOUBLE PRECISION NOT NULL,
      altitude DOUBLE PRECISION,
      speed_kmh DOUBLE PRECISION,
      accel_x DOUBLE PRECISION,
      accel_y DOUBLE PRECISION,
      accel_z DOUBLE PRECISION,
      recorded_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
  `);

  // Create indexes for better query performance
  await query(`CREATE INDEX IF NOT EXISTS idx_walks_device_id ON walks(device_id)`);
  await query(`CREATE INDEX IF NOT EXISTS idx_walks_start_time ON walks(start_time)`);
  await query(`CREATE INDEX IF NOT EXISTS idx_walk_points_walk_id ON walk_points(walk_id)`);
  await query(`CREATE INDEX IF NOT EXISTS idx_devices_device_id ON devices(device_id)`);

  console.log('Database tables created successfully');
}

export default { getPool, query, initDatabase };
