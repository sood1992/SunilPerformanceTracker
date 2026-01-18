/**
 * Dog Walker GPS Tracker - Backend API
 *
 * Express server for receiving walk data from ESP32 device
 * and serving data to the frontend dashboard.
 */

import express from 'express';
import cors from 'cors';
import { config } from 'dotenv';
import { walkRoutes } from './routes/walks.js';
import { deviceRoutes } from './routes/devices.js';
import { statsRoutes } from './routes/stats.js';
import { initDatabase } from './db/init.js';

config();

const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors({
  origin: process.env.FRONTEND_URL || '*',
  methods: ['GET', 'POST', 'PUT', 'DELETE'],
  allowedHeaders: ['Content-Type', 'X-Device-ID', 'Authorization']
}));

app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true }));

// Request logging
app.use((req, res, next) => {
  const timestamp = new Date().toISOString();
  console.log(`[${timestamp}] ${req.method} ${req.path}`);
  next();
});

// Health check endpoint
app.get('/health', (req, res) => {
  res.json({
    status: 'ok',
    timestamp: new Date().toISOString(),
    version: '1.0.0'
  });
});

// API Routes
app.use('/api/walks', walkRoutes);
app.use('/api/devices', deviceRoutes);
app.use('/api/stats', statsRoutes);

// Root endpoint
app.get('/', (req, res) => {
  res.json({
    name: 'Dog Walker GPS Tracker API',
    version: '1.0.0',
    endpoints: {
      health: 'GET /health',
      walks: {
        list: 'GET /api/walks',
        get: 'GET /api/walks/:id',
        upload: 'POST /api/walks/upload',
        points: 'GET /api/walks/:id/points'
      },
      devices: {
        list: 'GET /api/devices',
        register: 'POST /api/devices/register'
      },
      stats: {
        summary: 'GET /api/stats/summary',
        daily: 'GET /api/stats/daily',
        weekly: 'GET /api/stats/weekly'
      }
    }
  });
});

// 404 handler
app.use((req, res) => {
  res.status(404).json({
    error: 'Not Found',
    message: `Cannot ${req.method} ${req.path}`
  });
});

// Error handler
app.use((err, req, res, next) => {
  console.error('Error:', err);
  res.status(err.status || 500).json({
    error: err.message || 'Internal Server Error',
    ...(process.env.NODE_ENV === 'development' && { stack: err.stack })
  });
});

// Initialize database and start server
async function start() {
  try {
    await initDatabase();
    console.log('Database initialized');

    app.listen(PORT, '0.0.0.0', () => {
      console.log(`\nDog Walker API running on port ${PORT}`);
      console.log(`Health check: http://localhost:${PORT}/health`);
    });
  } catch (error) {
    console.error('Failed to start server:', error);
    process.exit(1);
  }
}

start();
