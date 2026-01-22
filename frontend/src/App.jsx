import { useState, useEffect, useRef } from 'react'
import WalkMap from './components/WalkMap'
import WalkList from './components/WalkList'
import StatsCards from './components/StatsCards'
import SpeedChart from './components/SpeedChart'
import GoalDashboard from './components/GoalDashboard'
import HistoricalStats from './components/HistoricalStats'
import LiveTracker from './components/LiveTracker'

// API base URL - update for production
// Auto-add https:// if protocol is missing
const rawApiUrl = import.meta.env.VITE_API_URL || ''
const API_URL = rawApiUrl && !rawApiUrl.startsWith('http')
  ? `https://${rawApiUrl}`
  : rawApiUrl

function App() {
  const [walks, setWalks] = useState([])
  const [selectedWalk, setSelectedWalk] = useState(null)
  const [walkPoints, setWalkPoints] = useState([])
  const [stats, setStats] = useState(null)
  const [dailyStats, setDailyStats] = useState([])
  const [loading, setLoading] = useState(true)
  const [loadingPoints, setLoadingPoints] = useState(false)
  const [error, setError] = useState(null)

  // Abort controller for cancelling fetch requests
  const abortControllerRef = useRef(null)

  // Fetch walks and stats on mount
  useEffect(() => {
    fetchWalks()
    fetchStats()
    fetchDailyStats()
  }, [])

  // Fetch walk points when a walk is selected (with race condition fix)
  useEffect(() => {
    // Cancel any pending request
    if (abortControllerRef.current) {
      abortControllerRef.current.abort()
    }

    if (selectedWalk) {
      fetchWalkPoints(selectedWalk.id)
    } else {
      setWalkPoints([])
    }

    // Cleanup on unmount or when selectedWalk changes
    return () => {
      if (abortControllerRef.current) {
        abortControllerRef.current.abort()
      }
    }
  }, [selectedWalk])

  const fetchWalks = async () => {
    try {
      const res = await fetch(`${API_URL}/api/walks?limit=100`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      if (!data || !Array.isArray(data.walks)) {
        throw new Error('Invalid response format')
      }
      setWalks(data.walks)
      setLoading(false)
    } catch (err) {
      console.error('Failed to fetch walks:', err)
      setError('Failed to load walks')
      setLoading(false)
    }
  }

  const fetchStats = async () => {
    try {
      const res = await fetch(`${API_URL}/api/stats/summary`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      if (!data || !data.allTime) {
        throw new Error('Invalid stats format')
      }
      setStats(data)
    } catch (err) {
      console.error('Failed to fetch stats:', err)
      // Don't set main error - stats are secondary
    }
  }

  const fetchDailyStats = async () => {
    try {
      const res = await fetch(`${API_URL}/api/stats/daily?days=14`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      if (!data || !Array.isArray(data.daily)) {
        throw new Error('Invalid daily stats format')
      }
      setDailyStats(data.daily)
    } catch (err) {
      console.error('Failed to fetch daily stats:', err)
    }
  }

  const fetchWalkPoints = async (walkId) => {
    // Create new abort controller for this request
    abortControllerRef.current = new AbortController()

    setLoadingPoints(true)
    try {
      const res = await fetch(`${API_URL}/api/walks/${walkId}/points`, {
        signal: abortControllerRef.current.signal
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()
      if (!data || !Array.isArray(data.points)) {
        throw new Error('Invalid points format')
      }
      setWalkPoints(data.points)
    } catch (err) {
      // Ignore abort errors (expected when user selects different walk)
      if (err.name !== 'AbortError') {
        console.error('Failed to fetch walk points:', err)
        setWalkPoints([])
      }
    } finally {
      setLoadingPoints(false)
    }
  }

  const handleWalkSelect = (walk) => {
    setSelectedWalk(walk)
  }

  const handleClearSelection = () => {
    setSelectedWalk(null)
    setWalkPoints([])
  }

  if (loading) {
    return (
      <div className="loading-container">
        <div className="loading-spinner"></div>
        <p>Loading dashboard...</p>
      </div>
    )
  }

  return (
    <div className="app">
      <header className="header">
        <div className="header-content">
          <h1>Dog Walker Tracker</h1>
          <p className="subtitle">Track walks, routes, and performance</p>
        </div>
      </header>

      <main className="main-content">
        {error && (
          <div className="error-banner">
            {error}
            <button onClick={() => setError(null)}>Dismiss</button>
          </div>
        )}

        {/* Live Tracking Section - Shows when walk is active */}
        <section className="live-section">
          <LiveTracker />
        </section>

        {/* Stats Overview */}
        <section className="stats-section">
          <StatsCards stats={stats} />
        </section>

        {/* Goals Dashboard - Popcorn's Daily Goals */}
        <section className="goals-section">
          <GoalDashboard />
        </section>

        {/* Main Dashboard Grid */}
        <div className="dashboard-grid">
          {/* Map Section */}
          <section className="map-section">
            <div className="section-header">
              <h2>Walk Route</h2>
              {selectedWalk && (
                <button className="clear-btn" onClick={handleClearSelection}>
                  Clear Selection
                </button>
              )}
            </div>
            <WalkMap
              points={walkPoints}
              selectedWalk={selectedWalk}
              loading={loadingPoints}
            />
            {selectedWalk && (
              <div className="walk-details">
                <h3>Walk Details</h3>
                <div className="details-grid">
                  <div className="detail-item">
                    <span className="label">Date</span>
                    <span className="value">
                      {new Date(selectedWalk.startTime).toLocaleDateString()}
                    </span>
                  </div>
                  <div className="detail-item">
                    <span className="label">Duration</span>
                    <span className="value">{selectedWalk.durationFormatted}</span>
                  </div>
                  <div className="detail-item">
                    <span className="label">Distance</span>
                    <span className="value">{selectedWalk.totalDistanceKm} km</span>
                  </div>
                  <div className="detail-item">
                    <span className="label">Avg Speed</span>
                    <span className="value">{selectedWalk.avgSpeedKmh} km/h</span>
                  </div>
                  <div className="detail-item">
                    <span className="label">Max Speed</span>
                    <span className="value">{selectedWalk.maxSpeedKmh} km/h</span>
                  </div>
                  <div className="detail-item">
                    <span className="label">GPS Points</span>
                    <span className="value">{selectedWalk.pointCount}</span>
                  </div>
                </div>
              </div>
            )}
          </section>

          {/* Walk List Section */}
          <section className="walks-section">
            <div className="section-header">
              <h2>Recent Walks</h2>
              <span className="walk-count">{walks.length} walks</span>
            </div>
            <WalkList
              walks={walks}
              selectedWalk={selectedWalk}
              onSelect={handleWalkSelect}
            />
          </section>
        </div>

        {/* Charts Section */}
        <section className="charts-section">
          <div className="section-header">
            <h2>Activity Overview</h2>
          </div>
          <SpeedChart dailyStats={dailyStats} />
        </section>

        {/* Historical Stats Section */}
        <section className="historical-section">
          <HistoricalStats onSelectWalk={handleWalkSelect} />
        </section>
      </main>

      <footer className="footer">
        <p>Dog Walker GPS Tracker - LilyGo T-A7670G R2 + ADXL345</p>
      </footer>
    </div>
  )
}

export default App
