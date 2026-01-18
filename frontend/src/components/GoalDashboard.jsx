import { useState, useEffect } from 'react'
import {
  BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip,
  ResponsiveContainer, Cell, PieChart, Pie
} from 'recharts'

const API_URL = import.meta.env.VITE_API_URL || ''

function GoalDashboard() {
  const [todayGoals, setTodayGoals] = useState(null)
  const [history, setHistory] = useState(null)
  const [alerts, setAlerts] = useState([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    fetchGoalData()
    // Refresh every 5 minutes
    const interval = setInterval(fetchGoalData, 5 * 60 * 1000)
    return () => clearInterval(interval)
  }, [])

  const fetchGoalData = async () => {
    try {
      const [todayRes, historyRes, alertsRes] = await Promise.all([
        fetch(`${API_URL}/api/goals/today`),
        fetch(`${API_URL}/api/goals/history?days=30`),
        fetch(`${API_URL}/api/goals/alerts`)
      ])

      if (todayRes.ok) {
        const data = await todayRes.json()
        setTodayGoals(data)
      }

      if (historyRes.ok) {
        const data = await historyRes.json()
        setHistory(data)
      }

      if (alertsRes.ok) {
        const data = await alertsRes.json()
        setAlerts(data.alerts || [])
      }
    } catch (err) {
      console.error('Failed to fetch goal data:', err)
    } finally {
      setLoading(false)
    }
  }

  if (loading) {
    return (
      <div className="goal-dashboard loading">
        <div className="loading-spinner"></div>
        <p>Loading goals...</p>
      </div>
    )
  }

  return (
    <div className="goal-dashboard">
      {/* Alerts Section */}
      {alerts.length > 0 && (
        <div className="alerts-section">
          {alerts.map((alert, idx) => (
            <div key={idx} className={`alert alert-${alert.severity}`}>
              <span className="alert-icon">
                {alert.severity === 'success' ? '🏆' :
                 alert.severity === 'high' ? '⚠️' :
                 alert.severity === 'medium' ? '📢' : '💡'}
              </span>
              <div className="alert-content">
                <strong>{alert.title}</strong>
                <p>{alert.message}</p>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Today's Progress */}
      {todayGoals && (
        <div className="today-section">
          <div className="section-header">
            <h2>Today's Progress - {todayGoals.dogName}</h2>
            <span className={`daily-score ${todayGoals.goalMet ? 'met' : ''}`}>
              Score: {todayGoals.dailyScore}/100
            </span>
          </div>

          <div className="progress-cards">
            {/* Walks Progress */}
            <ProgressCard
              title="Walks"
              current={todayGoals.progress.walks.current}
              goal={todayGoals.progress.walks.goal}
              percent={todayGoals.progress.walks.percent}
              completed={todayGoals.progress.walks.completed}
              icon="🚶"
            />

            {/* Minutes Progress */}
            <ProgressCard
              title="Minutes"
              current={todayGoals.progress.minutes.current}
              goal={todayGoals.progress.minutes.goal}
              percent={todayGoals.progress.minutes.percent}
              completed={todayGoals.progress.minutes.completed}
              icon="⏱️"
              unit="min"
            />

            {/* Distance Progress */}
            <ProgressCard
              title="Distance"
              current={todayGoals.progress.distance.currentKm}
              goal={todayGoals.progress.distance.goalKm}
              percent={todayGoals.progress.distance.percent}
              completed={todayGoals.progress.distance.completed}
              icon="📍"
              unit="km"
            />
          </div>

          {/* Activity Breakdown */}
          <div className="activity-breakdown">
            <h3>Activity Breakdown</h3>
            <div className="activity-charts">
              <div className="activity-pie">
                <ResponsiveContainer width="100%" height={200}>
                  <PieChart>
                    <Pie
                      data={[
                        { name: 'Walking', value: todayGoals.activity.walkingPercent, color: '#4CAF50' },
                        { name: 'Running', value: todayGoals.activity.runningPercent, color: '#FF9800' },
                        { name: 'Resting', value: todayGoals.activity.sittingPercent, color: '#9E9E9E' }
                      ]}
                      cx="50%"
                      cy="50%"
                      innerRadius={50}
                      outerRadius={80}
                      dataKey="value"
                      label={({ name, value }) => `${name}: ${value}%`}
                    >
                      {[
                        { name: 'Walking', value: todayGoals.activity.walkingPercent, color: '#4CAF50' },
                        { name: 'Running', value: todayGoals.activity.runningPercent, color: '#FF9800' },
                        { name: 'Resting', value: todayGoals.activity.sittingPercent, color: '#9E9E9E' }
                      ].map((entry, index) => (
                        <Cell key={index} fill={entry.color} />
                      ))}
                    </Pie>
                    <Tooltip />
                  </PieChart>
                </ResponsiveContainer>
              </div>

              <div className="activity-stats">
                <div className="stat-item">
                  <span className="stat-label">Average Speed</span>
                  <span className="stat-value">{todayGoals.activity.avgSpeedKmh} km/h</span>
                </div>
                <div className="stat-item">
                  <span className="stat-label">Max Speed</span>
                  <span className="stat-value">{todayGoals.activity.maxSpeedKmh} km/h</span>
                </div>
                {todayGoals.activity.tooMuchResting && (
                  <div className="warning-message">
                    ⚠️ {todayGoals.activity.restingWarning}
                  </div>
                )}
              </div>
            </div>
          </div>

          <div className="daily-message">
            {todayGoals.message}
          </div>
        </div>
      )}

      {/* 30-Day History */}
      {history && (
        <div className="history-section">
          <div className="section-header">
            <h2>30-Day History</h2>
            <div className="streak-badge">
              🔥 {history.summary.currentStreak} day streak
            </div>
          </div>

          <div className="summary-stats">
            <div className="summary-stat">
              <span className="stat-value">{history.summary.averageScore}</span>
              <span className="stat-label">Avg Score</span>
            </div>
            <div className="summary-stat">
              <span className="stat-value">{history.summary.completionRate}%</span>
              <span className="stat-label">Goal Rate</span>
            </div>
            <div className="summary-stat">
              <span className="stat-value">{history.summary.totalWalks}</span>
              <span className="stat-label">Total Walks</span>
            </div>
            <div className="summary-stat">
              <span className="stat-value">{history.summary.totalDistanceKm}</span>
              <span className="stat-label">Total km</span>
            </div>
          </div>

          {/* History Chart */}
          <div className="history-chart">
            <ResponsiveContainer width="100%" height={250}>
              <BarChart data={history.history.slice(0, 14).reverse()}>
                <CartesianGrid strokeDasharray="3 3" stroke="#eee" />
                <XAxis
                  dataKey="date"
                  tick={{ fontSize: 10 }}
                  tickFormatter={(date) => {
                    const d = new Date(date)
                    return `${d.getMonth() + 1}/${d.getDate()}`
                  }}
                />
                <YAxis domain={[0, 100]} tick={{ fontSize: 12 }} />
                <Tooltip
                  formatter={(value, name) => [value, name === 'score' ? 'Score' : name]}
                  labelFormatter={(date) => new Date(date).toLocaleDateString()}
                />
                <Bar dataKey="score" name="Daily Score" radius={[4, 4, 0, 0]}>
                  {history.history.slice(0, 14).reverse().map((entry, index) => (
                    <Cell
                      key={index}
                      fill={entry.goalMet ? '#4CAF50' : entry.score > 50 ? '#FF9800' : '#f44336'}
                    />
                  ))}
                </Bar>
              </BarChart>
            </ResponsiveContainer>
          </div>

          {/* Calendar View (last 30 days) */}
          <div className="calendar-view">
            <h3>Goal Completion</h3>
            <div className="calendar-grid">
              {history.history.map((day, idx) => (
                <div
                  key={idx}
                  className={`calendar-day ${day.goalMet ? 'met' : day.walks > 0 ? 'partial' : 'missed'}`}
                  title={`${day.date}: Score ${day.score}, ${day.minutes} min, ${day.distanceKm} km`}
                >
                  <span className="day-number">
                    {new Date(day.date).getDate()}
                  </span>
                  {day.goalMet && <span className="check-mark">✓</span>}
                </div>
              ))}
            </div>
            <div className="calendar-legend">
              <span><span className="legend-dot met"></span> Goal Met</span>
              <span><span className="legend-dot partial"></span> Partial</span>
              <span><span className="legend-dot missed"></span> No Walk</span>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

// Progress Card Component
function ProgressCard({ title, current, goal, percent, completed, icon, unit = '' }) {
  return (
    <div className={`progress-card ${completed ? 'completed' : ''}`}>
      <div className="progress-header">
        <span className="progress-icon">{icon}</span>
        <span className="progress-title">{title}</span>
        {completed && <span className="completed-badge">✓</span>}
      </div>
      <div className="progress-values">
        <span className="current-value">{current}</span>
        <span className="goal-value">/ {goal} {unit}</span>
      </div>
      <div className="progress-bar-container">
        <div
          className="progress-bar-fill"
          style={{ width: `${Math.min(100, percent)}%` }}
        ></div>
      </div>
      <div className="progress-percent">{percent}%</div>
    </div>
  )
}

export default GoalDashboard
