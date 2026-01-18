import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  LineChart,
  Line,
  Legend
} from 'recharts'

function SpeedChart({ dailyStats }) {
  if (!dailyStats || dailyStats.length === 0) {
    return (
      <div className="chart-container empty">
        <p>No activity data available yet.</p>
      </div>
    )
  }

  // Reverse to show oldest first (left to right)
  const chartData = [...dailyStats].reverse().map(stat => ({
    ...stat,
    date: formatDateShort(stat.date),
    distance: parseFloat(stat.distanceKm),
    duration: stat.durationMinutes,
    avgSpeed: parseFloat(stat.avgSpeedKmh)
  }))

  function formatDateShort(dateStr) {
    const date = new Date(dateStr)
    return date.toLocaleDateString([], { month: 'short', day: 'numeric' })
  }

  const CustomTooltip = ({ active, payload, label }) => {
    if (active && payload && payload.length) {
      return (
        <div className="chart-tooltip">
          <p className="tooltip-label">{label}</p>
          {payload.map((entry, index) => (
            <p key={index} style={{ color: entry.color }}>
              {entry.name}: {entry.value} {entry.name === 'Distance' ? 'km' : entry.name === 'Duration' ? 'min' : 'km/h'}
            </p>
          ))}
        </div>
      )
    }
    return null
  }

  return (
    <div className="charts-container">
      {/* Distance Bar Chart */}
      <div className="chart-wrapper">
        <h3>Daily Distance</h3>
        <ResponsiveContainer width="100%" height={250}>
          <BarChart data={chartData}>
            <CartesianGrid strokeDasharray="3 3" stroke="#eee" />
            <XAxis
              dataKey="date"
              tick={{ fontSize: 12 }}
              stroke="#666"
            />
            <YAxis
              tick={{ fontSize: 12 }}
              stroke="#666"
              label={{ value: 'km', angle: -90, position: 'insideLeft', fontSize: 12 }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Bar
              dataKey="distance"
              name="Distance"
              fill="#4CAF50"
              radius={[4, 4, 0, 0]}
            />
          </BarChart>
        </ResponsiveContainer>
      </div>

      {/* Duration & Speed Line Chart */}
      <div className="chart-wrapper">
        <h3>Duration & Speed Trends</h3>
        <ResponsiveContainer width="100%" height={250}>
          <LineChart data={chartData}>
            <CartesianGrid strokeDasharray="3 3" stroke="#eee" />
            <XAxis
              dataKey="date"
              tick={{ fontSize: 12 }}
              stroke="#666"
            />
            <YAxis
              yAxisId="left"
              tick={{ fontSize: 12 }}
              stroke="#666"
              label={{ value: 'min', angle: -90, position: 'insideLeft', fontSize: 12 }}
            />
            <YAxis
              yAxisId="right"
              orientation="right"
              tick={{ fontSize: 12 }}
              stroke="#666"
              label={{ value: 'km/h', angle: 90, position: 'insideRight', fontSize: 12 }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Legend />
            <Line
              yAxisId="left"
              type="monotone"
              dataKey="duration"
              name="Duration"
              stroke="#2196F3"
              strokeWidth={2}
              dot={{ fill: '#2196F3', strokeWidth: 2, r: 4 }}
              activeDot={{ r: 6 }}
            />
            <Line
              yAxisId="right"
              type="monotone"
              dataKey="avgSpeed"
              name="Avg Speed"
              stroke="#FF9800"
              strokeWidth={2}
              dot={{ fill: '#FF9800', strokeWidth: 2, r: 4 }}
              activeDot={{ r: 6 }}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>

      {/* Walks Count */}
      <div className="chart-wrapper">
        <h3>Walks Per Day</h3>
        <ResponsiveContainer width="100%" height={200}>
          <BarChart data={chartData}>
            <CartesianGrid strokeDasharray="3 3" stroke="#eee" />
            <XAxis
              dataKey="date"
              tick={{ fontSize: 12 }}
              stroke="#666"
            />
            <YAxis
              tick={{ fontSize: 12 }}
              stroke="#666"
              allowDecimals={false}
            />
            <Tooltip content={<CustomTooltip />} />
            <Bar
              dataKey="walks"
              name="Walks"
              fill="#9C27B0"
              radius={[4, 4, 0, 0]}
            />
          </BarChart>
        </ResponsiveContainer>
      </div>
    </div>
  )
}

export default SpeedChart
