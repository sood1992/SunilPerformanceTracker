import { useState, useEffect } from 'react'
import {
  BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip,
  ResponsiveContainer, LineChart, Line, Legend
} from 'recharts'

// Auto-add https:// if protocol is missing
const rawApiUrl = import.meta.env.VITE_API_URL || ''
const API_URL = rawApiUrl && !rawApiUrl.startsWith('http')
  ? `https://${rawApiUrl}`
  : rawApiUrl

function HistoricalStats({ onSelectWalk }) {
  const [viewMode, setViewMode] = useState('daily') // daily, monthly, yearly
  const [selectedDate, setSelectedDate] = useState(new Date().toISOString().split('T')[0])
  const [selectedMonth, setSelectedMonth] = useState(new Date().getMonth() + 1)
  const [selectedYear, setSelectedYear] = useState(new Date().getFullYear())
  const [dateRange, setDateRange] = useState({
    start: new Date(Date.now() - 30 * 24 * 60 * 60 * 1000).toISOString().split('T')[0],
    end: new Date().toISOString().split('T')[0]
  })

  const [data, setData] = useState(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(null)

  // Fetch data when selection changes
  useEffect(() => {
    fetchData()
  }, [viewMode, selectedDate, selectedMonth, selectedYear, dateRange])

  const fetchData = async () => {
    setLoading(true)
    setError(null)

    try {
      let url
      switch (viewMode) {
        case 'daily':
          url = `${API_URL}/api/stats/date/${selectedDate}`
          break
        case 'monthly':
          url = `${API_URL}/api/stats/month/${selectedYear}/${selectedMonth}`
          break
        case 'yearly':
          url = `${API_URL}/api/stats/year/${selectedYear}`
          break
        case 'range':
          url = `${API_URL}/api/stats/range?start_date=${dateRange.start}&end_date=${dateRange.end}`
          break
        default:
          url = `${API_URL}/api/stats/date/${selectedDate}`
      }

      const res = await fetch(url)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const result = await res.json()
      setData(result)
    } catch (err) {
      console.error('Failed to fetch historical stats:', err)
      setError('Failed to load historical data')
    } finally {
      setLoading(false)
    }
  }

  const formatDuration = (minutes) => {
    if (minutes < 60) return `${minutes} min`
    const hours = Math.floor(minutes / 60)
    const mins = minutes % 60
    return `${hours}h ${mins}m`
  }

  const formatHours = (hours) => {
    const h = Math.floor(hours)
    const m = Math.round((hours - h) * 60)
    return `${h}h ${m}m`
  }

  // Generate year options (last 5 years)
  const yearOptions = []
  const currentYear = new Date().getFullYear()
  for (let y = currentYear; y >= currentYear - 5; y--) {
    yearOptions.push(y)
  }

  const monthOptions = [
    { value: 1, label: 'January' },
    { value: 2, label: 'February' },
    { value: 3, label: 'March' },
    { value: 4, label: 'April' },
    { value: 5, label: 'May' },
    { value: 6, label: 'June' },
    { value: 7, label: 'July' },
    { value: 8, label: 'August' },
    { value: 9, label: 'September' },
    { value: 10, label: 'October' },
    { value: 11, label: 'November' },
    { value: 12, label: 'December' }
  ]

  return (
    <div className="historical-stats">
      <div className="historical-header">
        <h2>Historical Statistics</h2>

        {/* View Mode Selector */}
        <div className="view-mode-selector">
          <button
            className={viewMode === 'daily' ? 'active' : ''}
            onClick={() => setViewMode('daily')}
          >
            Day
          </button>
          <button
            className={viewMode === 'monthly' ? 'active' : ''}
            onClick={() => setViewMode('monthly')}
          >
            Month
          </button>
          <button
            className={viewMode === 'yearly' ? 'active' : ''}
            onClick={() => setViewMode('yearly')}
          >
            Year
          </button>
          <button
            className={viewMode === 'range' ? 'active' : ''}
            onClick={() => setViewMode('range')}
          >
            Range
          </button>
        </div>
      </div>

      {/* Date/Period Selector */}
      <div className="period-selector">
        {viewMode === 'daily' && (
          <div className="date-picker">
            <label>Select Date:</label>
            <input
              type="date"
              value={selectedDate}
              onChange={(e) => setSelectedDate(e.target.value)}
              max={new Date().toISOString().split('T')[0]}
            />
          </div>
        )}

        {viewMode === 'monthly' && (
          <div className="month-picker">
            <label>Select Month:</label>
            <select
              value={selectedMonth}
              onChange={(e) => setSelectedMonth(parseInt(e.target.value))}
            >
              {monthOptions.map(m => (
                <option key={m.value} value={m.value}>{m.label}</option>
              ))}
            </select>
            <select
              value={selectedYear}
              onChange={(e) => setSelectedYear(parseInt(e.target.value))}
            >
              {yearOptions.map(y => (
                <option key={y} value={y}>{y}</option>
              ))}
            </select>
          </div>
        )}

        {viewMode === 'yearly' && (
          <div className="year-picker">
            <label>Select Year:</label>
            <select
              value={selectedYear}
              onChange={(e) => setSelectedYear(parseInt(e.target.value))}
            >
              {yearOptions.map(y => (
                <option key={y} value={y}>{y}</option>
              ))}
            </select>
          </div>
        )}

        {viewMode === 'range' && (
          <div className="range-picker">
            <label>From:</label>
            <input
              type="date"
              value={dateRange.start}
              onChange={(e) => setDateRange({ ...dateRange, start: e.target.value })}
              max={dateRange.end}
            />
            <label>To:</label>
            <input
              type="date"
              value={dateRange.end}
              onChange={(e) => setDateRange({ ...dateRange, end: e.target.value })}
              min={dateRange.start}
              max={new Date().toISOString().split('T')[0]}
            />
          </div>
        )}
      </div>

      {/* Loading State */}
      {loading && (
        <div className="loading-container">
          <div className="loading-spinner"></div>
          <p>Loading stats...</p>
        </div>
      )}

      {/* Error State */}
      {error && (
        <div className="error-message">{error}</div>
      )}

      {/* Data Display */}
      {!loading && !error && data && (
        <div className="historical-content">
          {/* Summary Cards */}
          <div className="summary-cards">
            <div className="summary-card">
              <span className="card-icon">🚶</span>
              <div className="card-content">
                <span className="card-value">{data.summary?.totalWalks || 0}</span>
                <span className="card-label">Total Walks</span>
              </div>
            </div>
            <div className="summary-card">
              <span className="card-icon">📍</span>
              <div className="card-content">
                <span className="card-value">{data.summary?.totalDistanceKm || '0.00'} km</span>
                <span className="card-label">Distance</span>
              </div>
            </div>
            <div className="summary-card">
              <span className="card-icon">⏱️</span>
              <div className="card-content">
                <span className="card-value">
                  {viewMode === 'daily'
                    ? formatDuration(data.summary?.totalDurationMinutes || 0)
                    : formatHours(parseFloat(data.summary?.totalDurationHours || 0))
                  }
                </span>
                <span className="card-label">Duration</span>
              </div>
            </div>
            <div className="summary-card">
              <span className="card-icon">🏃</span>
              <div className="card-content">
                <span className="card-value">{data.summary?.avgSpeedKmh || '0.00'} km/h</span>
                <span className="card-label">Avg Speed</span>
              </div>
            </div>
            <div className="summary-card">
              <span className="card-icon">⚡</span>
              <div className="card-content">
                <span className="card-value">{data.summary?.maxSpeedKmh || '0.00'} km/h</span>
                <span className="card-label">Max Speed</span>
              </div>
            </div>
            {data.summary?.activeDays !== undefined && (
              <div className="summary-card">
                <span className="card-icon">📅</span>
                <div className="card-content">
                  <span className="card-value">{data.summary.activeDays}</span>
                  <span className="card-label">Active Days</span>
                </div>
              </div>
            )}
          </div>

          {/* Chart for Monthly/Yearly/Range views */}
          {(viewMode === 'monthly' || viewMode === 'range') && data.daily && data.daily.length > 0 && (
            <div className="chart-section">
              <h3>Daily Breakdown</h3>
              <ResponsiveContainer width="100%" height={300}>
                <BarChart data={data.daily}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#eee" />
                  <XAxis
                    dataKey="date"
                    tick={{ fontSize: 10 }}
                    tickFormatter={(date) => {
                      const d = new Date(date)
                      return `${d.getMonth() + 1}/${d.getDate()}`
                    }}
                  />
                  <YAxis yAxisId="left" />
                  <YAxis yAxisId="right" orientation="right" />
                  <Tooltip
                    labelFormatter={(date) => new Date(date).toLocaleDateString()}
                    formatter={(value, name) => {
                      if (name === 'distanceKm') return [`${value} km`, 'Distance']
                      if (name === 'durationMinutes') return [`${value} min`, 'Duration']
                      if (name === 'walks') return [value, 'Walks']
                      return [value, name]
                    }}
                  />
                  <Legend />
                  <Bar yAxisId="left" dataKey="distanceKm" name="Distance (km)" fill="#4CAF50" radius={[4, 4, 0, 0]} />
                  <Bar yAxisId="right" dataKey="walks" name="Walks" fill="#2196F3" radius={[4, 4, 0, 0]} />
                </BarChart>
              </ResponsiveContainer>
            </div>
          )}

          {viewMode === 'yearly' && data.monthly && data.monthly.length > 0 && (
            <div className="chart-section">
              <h3>Monthly Breakdown</h3>
              <ResponsiveContainer width="100%" height={300}>
                <BarChart data={data.monthly}>
                  <CartesianGrid strokeDasharray="3 3" stroke="#eee" />
                  <XAxis dataKey="monthName" tick={{ fontSize: 11 }} />
                  <YAxis yAxisId="left" />
                  <YAxis yAxisId="right" orientation="right" />
                  <Tooltip
                    formatter={(value, name) => {
                      if (name === 'distanceKm') return [`${value} km`, 'Distance']
                      if (name === 'durationHours') return [`${value} hrs`, 'Duration']
                      if (name === 'walks') return [value, 'Walks']
                      return [value, name]
                    }}
                  />
                  <Legend />
                  <Bar yAxisId="left" dataKey="distanceKm" name="Distance (km)" fill="#4CAF50" radius={[4, 4, 0, 0]} />
                  <Bar yAxisId="right" dataKey="walks" name="Walks" fill="#2196F3" radius={[4, 4, 0, 0]} />
                </BarChart>
              </ResponsiveContainer>
            </div>
          )}

          {/* Individual Walks for Daily view */}
          {viewMode === 'daily' && data.walks && data.walks.length > 0 && (
            <div className="walks-list-section">
              <h3>Walks on {new Date(selectedDate).toLocaleDateString('en-US', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' })}</h3>
              <div className="walks-table">
                <table>
                  <thead>
                    <tr>
                      <th>Time</th>
                      <th>Duration</th>
                      <th>Distance</th>
                      <th>Avg Speed</th>
                      <th>Max Speed</th>
                      <th>Points</th>
                      <th>Action</th>
                    </tr>
                  </thead>
                  <tbody>
                    {data.walks.map(walk => (
                      <tr key={walk.id}>
                        <td>{new Date(walk.startTime).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })}</td>
                        <td>{formatDuration(walk.durationMinutes)}</td>
                        <td>{walk.distanceKm} km</td>
                        <td>{walk.avgSpeedKmh} km/h</td>
                        <td>{walk.maxSpeedKmh} km/h</td>
                        <td>{walk.pointCount}</td>
                        <td>
                          <button
                            className="view-walk-btn"
                            onClick={() => onSelectWalk && onSelectWalk(walk)}
                          >
                            View Route
                          </button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          )}

          {/* No walks message */}
          {viewMode === 'daily' && (!data.walks || data.walks.length === 0) && (
            <div className="no-data-message">
              <p>No walks recorded on this date.</p>
            </div>
          )}

          {/* Monthly calendar view for month mode */}
          {viewMode === 'monthly' && data.daily && (
            <div className="month-calendar">
              <h3>Activity Calendar</h3>
              <div className="calendar-grid monthly">
                {Array.from({ length: new Date(selectedYear, selectedMonth, 0).getDate() }, (_, i) => {
                  const day = i + 1
                  const dayData = data.daily.find(d => d.day === day)
                  const hasWalks = dayData && dayData.walks > 0
                  return (
                    <div
                      key={day}
                      className={`calendar-day ${hasWalks ? 'has-walks' : 'no-walks'}`}
                      onClick={() => {
                        const dateStr = `${selectedYear}-${String(selectedMonth).padStart(2, '0')}-${String(day).padStart(2, '0')}`
                        setSelectedDate(dateStr)
                        setViewMode('daily')
                      }}
                      title={hasWalks ? `${dayData.walks} walks, ${dayData.distanceKm} km` : 'No walks'}
                    >
                      <span className="day-number">{day}</span>
                      {hasWalks && (
                        <div className="day-stats">
                          <span>{dayData.walks}</span>
                          <span>{dayData.distanceKm}km</span>
                        </div>
                      )}
                    </div>
                  )
                })}
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  )
}

export default HistoricalStats
