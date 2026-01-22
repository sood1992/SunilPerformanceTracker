import { useState, useEffect, useRef } from 'react'

/**
 * LiveTracker Component
 * Shows real-time location during active walks via LTE streaming
 */

// API base URL
const rawApiUrl = import.meta.env.VITE_API_URL || ''
const API_URL = rawApiUrl && !rawApiUrl.startsWith('http')
  ? `https://${rawApiUrl}`
  : rawApiUrl

function LiveTracker() {
  const [liveWalks, setLiveWalks] = useState([])
  const [selectedLiveWalk, setSelectedLiveWalk] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)
  const [lastUpdate, setLastUpdate] = useState(null)
  const mapRef = useRef(null)
  const markerRef = useRef(null)

  // Poll for live walks every 5 seconds
  useEffect(() => {
    fetchLiveWalks()
    const interval = setInterval(fetchLiveWalks, 5000)
    return () => clearInterval(interval)
  }, [])

  // Update map when selected walk changes
  useEffect(() => {
    if (selectedLiveWalk && selectedLiveWalk.recentPoints?.length > 0) {
      updateMap(selectedLiveWalk)
    }
  }, [selectedLiveWalk])

  const fetchLiveWalks = async () => {
    try {
      const res = await fetch(`${API_URL}/api/walks/live`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = await res.json()

      setLiveWalks(data.liveWalks || [])
      setLastUpdate(new Date())
      setLoading(false)

      // Auto-select first live walk if none selected
      if (data.liveWalks?.length > 0 && !selectedLiveWalk) {
        setSelectedLiveWalk(data.liveWalks[0])
      }

      // Update selected walk with fresh data
      if (selectedLiveWalk && data.liveWalks) {
        const updated = data.liveWalks.find(w => w.id === selectedLiveWalk.id)
        if (updated) {
          setSelectedLiveWalk(updated)
        } else {
          // Walk ended
          setSelectedLiveWalk(null)
        }
      }

      setError(null)
    } catch (err) {
      console.error('Failed to fetch live walks:', err)
      setError('Failed to fetch live data')
      setLoading(false)
    }
  }

  const updateMap = (walk) => {
    if (!walk.recentPoints || walk.recentPoints.length === 0) return

    // Get most recent point
    const latestPoint = walk.recentPoints[0]

    // Initialize map if not exists
    if (!mapRef.current && window.L) {
      const container = document.getElementById('live-map')
      if (!container) return

      mapRef.current = L.map('live-map').setView(
        [latestPoint.lat, latestPoint.lon],
        16
      )

      L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '&copy; OpenStreetMap contributors'
      }).addTo(mapRef.current)

      // Add pulsing marker for current location
      markerRef.current = L.circleMarker([latestPoint.lat, latestPoint.lon], {
        radius: 12,
        fillColor: '#ef4444',
        color: '#fff',
        weight: 3,
        opacity: 1,
        fillOpacity: 0.8
      }).addTo(mapRef.current)

      // Add trail line
      const trailCoords = walk.recentPoints.map(p => [p.lat, p.lon]).reverse()
      L.polyline(trailCoords, {
        color: '#3b82f6',
        weight: 4,
        opacity: 0.8
      }).addTo(mapRef.current)
    } else if (mapRef.current && markerRef.current) {
      // Update existing marker position
      markerRef.current.setLatLng([latestPoint.lat, latestPoint.lon])
      mapRef.current.panTo([latestPoint.lat, latestPoint.lon])
    }
  }

  const formatDuration = (seconds) => {
    if (!seconds) return '0:00'
    const h = Math.floor(seconds / 3600)
    const m = Math.floor((seconds % 3600) / 60)
    const s = seconds % 60
    if (h > 0) {
      return `${h}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`
    }
    return `${m}:${s.toString().padStart(2, '0')}`
  }

  const formatDistance = (meters) => {
    if (!meters) return '0 m'
    if (meters >= 1000) {
      return `${(meters / 1000).toFixed(2)} km`
    }
    return `${Math.round(meters)} m`
  }

  // No live walks
  if (!loading && liveWalks.length === 0) {
    return (
      <div className="live-tracker live-tracker--idle">
        <div className="live-tracker__header">
          <h2>
            <span className="pulse-dot pulse-dot--gray"></span>
            Live Tracking
          </h2>
          <span className="live-tracker__status">No active walks</span>
        </div>
        <div className="live-tracker__empty">
          <p>Popcorn is resting</p>
          <p className="text-muted">Live tracking will appear here when a walk starts</p>
          {lastUpdate && (
            <p className="text-small">Last checked: {lastUpdate.toLocaleTimeString()}</p>
          )}
        </div>
      </div>
    )
  }

  return (
    <div className="live-tracker live-tracker--active">
      <div className="live-tracker__header">
        <h2>
          <span className="pulse-dot pulse-dot--red"></span>
          LIVE - Walk in Progress
        </h2>
        {lastUpdate && (
          <span className="live-tracker__update">
            Updated: {lastUpdate.toLocaleTimeString()}
          </span>
        )}
      </div>

      {error && (
        <div className="live-tracker__error">{error}</div>
      )}

      {loading ? (
        <div className="live-tracker__loading">
          <div className="loading-spinner"></div>
          <p>Connecting to live feed...</p>
        </div>
      ) : selectedLiveWalk && (
        <>
          {/* Live Stats */}
          <div className="live-tracker__stats">
            <div className="live-stat">
              <span className="live-stat__value">
                {formatDuration(selectedLiveWalk.durationSeconds)}
              </span>
              <span className="live-stat__label">Duration</span>
            </div>
            <div className="live-stat">
              <span className="live-stat__value">
                {formatDistance(selectedLiveWalk.totalDistanceMeters)}
              </span>
              <span className="live-stat__label">Distance</span>
            </div>
            <div className="live-stat">
              <span className="live-stat__value">
                {selectedLiveWalk.recentPoints?.[0]?.spd?.toFixed(1) || '0.0'} km/h
              </span>
              <span className="live-stat__label">Current Speed</span>
            </div>
            <div className="live-stat">
              <span className="live-stat__value">{selectedLiveWalk.pointCount || 0}</span>
              <span className="live-stat__label">GPS Points</span>
            </div>
          </div>

          {/* Live Map */}
          <div className="live-tracker__map">
            <div id="live-map" style={{ height: '300px', width: '100%' }}></div>
          </div>

          {/* Current Location */}
          {selectedLiveWalk.recentPoints?.[0] && (
            <div className="live-tracker__location">
              <span className="location-label">Current Position:</span>
              <span className="location-coords">
                {selectedLiveWalk.recentPoints[0].lat.toFixed(6)},
                {selectedLiveWalk.recentPoints[0].lon.toFixed(6)}
              </span>
            </div>
          )}
        </>
      )}
    </div>
  )
}

export default LiveTracker
