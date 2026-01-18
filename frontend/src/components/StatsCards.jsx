function StatsCards({ stats }) {
  if (!stats) {
    return (
      <div className="stats-cards loading">
        <div className="stats-card skeleton"></div>
        <div className="stats-card skeleton"></div>
        <div className="stats-card skeleton"></div>
        <div className="stats-card skeleton"></div>
      </div>
    )
  }

  const cards = [
    {
      title: "Today's Walks",
      value: stats.today?.walks || 0,
      subtitle: `${stats.today?.distanceKm || '0.00'} km`,
      icon: '🚶',
      color: '#4CAF50'
    },
    {
      title: 'This Week',
      value: stats.thisWeek?.walks || 0,
      subtitle: `${stats.thisWeek?.distanceKm || '0.00'} km total`,
      icon: '📅',
      color: '#2196F3'
    },
    {
      title: 'Total Distance',
      value: `${stats.allTime?.totalDistanceKm || '0.00'}`,
      subtitle: 'kilometers walked',
      icon: '📍',
      color: '#FF9800'
    },
    {
      title: 'Average Speed',
      value: `${stats.allTime?.avgSpeedKmh || '0.00'}`,
      subtitle: 'km/h',
      icon: '⚡',
      color: '#9C27B0'
    },
    {
      title: 'Total Walks',
      value: stats.allTime?.totalWalks || 0,
      subtitle: `Max: ${stats.allTime?.maxSpeedKmh || 0} km/h`,
      icon: '🐕',
      color: '#E91E63'
    },
    {
      title: 'Walking Time',
      value: `${stats.allTime?.totalDurationHours || '0.00'}`,
      subtitle: 'hours total',
      icon: '⏱️',
      color: '#00BCD4'
    }
  ]

  return (
    <div className="stats-cards">
      {cards.map((card, index) => (
        <div
          key={index}
          className="stats-card"
          style={{ '--card-color': card.color }}
        >
          <div className="card-icon">{card.icon}</div>
          <div className="card-content">
            <div className="card-value">{card.value}</div>
            <div className="card-title">{card.title}</div>
            <div className="card-subtitle">{card.subtitle}</div>
          </div>
        </div>
      ))}
    </div>
  )
}

export default StatsCards
