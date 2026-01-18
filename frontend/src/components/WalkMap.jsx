import { useEffect, useRef } from 'react'
import { MapContainer, TileLayer, Polyline, Marker, Popup, useMap } from 'react-leaflet'
import L from 'leaflet'

// Fix for default marker icons in React-Leaflet
delete L.Icon.Default.prototype._getIconUrl
L.Icon.Default.mergeOptions({
  iconRetinaUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-icon-2x.png',
  iconUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-icon.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-shadow.png',
})

// Custom start/end markers
const startIcon = new L.Icon({
  iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-green.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-shadow.png',
  iconSize: [25, 41],
  iconAnchor: [12, 41],
  popupAnchor: [1, -34],
  shadowSize: [41, 41]
})

const endIcon = new L.Icon({
  iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-red.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-shadow.png',
  iconSize: [25, 41],
  iconAnchor: [12, 41],
  popupAnchor: [1, -34],
  shadowSize: [41, 41]
})

// Component to fit map bounds to route
function FitBounds({ points }) {
  const map = useMap()

  useEffect(() => {
    if (points && points.length > 0) {
      const latLngs = points.map(p => [p.lat, p.lon])
      const bounds = L.latLngBounds(latLngs)
      map.fitBounds(bounds, { padding: [50, 50] })
    }
  }, [points, map])

  return null
}

function WalkMap({ points, selectedWalk }) {
  const defaultCenter = [40.7128, -74.0060] // Default to NYC
  const defaultZoom = 13

  // Convert points to path coordinates
  const pathCoordinates = points.map(p => [p.lat, p.lon])

  // Get start and end points
  const startPoint = points.length > 0 ? points[0] : null
  const endPoint = points.length > 1 ? points[points.length - 1] : null

  // Color gradient based on speed
  const getSpeedColor = (speed) => {
    if (!speed || speed < 2) return '#3388ff' // Slow - blue
    if (speed < 4) return '#33ff88'  // Walking - green
    if (speed < 6) return '#ffff33'  // Fast walk - yellow
    return '#ff3333'  // Running - red
  }

  return (
    <div className="map-container">
      <MapContainer
        center={defaultCenter}
        zoom={defaultZoom}
        scrollWheelZoom={true}
        style={{ height: '100%', width: '100%' }}
      >
        <TileLayer
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
        />

        {points.length > 0 && (
          <>
            {/* Route polyline */}
            <Polyline
              positions={pathCoordinates}
              color="#3388ff"
              weight={4}
              opacity={0.8}
            />

            {/* Start marker */}
            {startPoint && (
              <Marker position={[startPoint.lat, startPoint.lon]} icon={startIcon}>
                <Popup>
                  <strong>Start</strong><br />
                  Speed: {startPoint.speed?.toFixed(1) || 0} km/h
                </Popup>
              </Marker>
            )}

            {/* End marker */}
            {endPoint && (
              <Marker position={[endPoint.lat, endPoint.lon]} icon={endIcon}>
                <Popup>
                  <strong>End</strong><br />
                  Speed: {endPoint.speed?.toFixed(1) || 0} km/h
                </Popup>
              </Marker>
            )}

            {/* Fit bounds to route */}
            <FitBounds points={points} />
          </>
        )}

        {/* Show placeholder if no route */}
        {points.length === 0 && (
          <div className="map-placeholder">
            Select a walk to view the route
          </div>
        )}
      </MapContainer>

      {/* Speed legend */}
      {points.length > 0 && (
        <div className="map-legend">
          <div className="legend-item">
            <span className="legend-color" style={{ background: '#3388ff' }}></span>
            <span>Slow (&lt;2 km/h)</span>
          </div>
          <div className="legend-item">
            <span className="legend-color" style={{ background: '#33ff88' }}></span>
            <span>Walking (2-4 km/h)</span>
          </div>
          <div className="legend-item">
            <span className="legend-color" style={{ background: '#ffff33' }}></span>
            <span>Fast (4-6 km/h)</span>
          </div>
          <div className="legend-item">
            <span className="legend-color" style={{ background: '#ff3333' }}></span>
            <span>Running (&gt;6 km/h)</span>
          </div>
        </div>
      )}
    </div>
  )
}

export default WalkMap
