function WalkList({ walks, selectedWalk, onSelect }) {
  if (walks.length === 0) {
    return (
      <div className="walk-list empty">
        <p>No walks recorded yet.</p>
        <p className="hint">Walks will appear here once the tracker uploads data.</p>
      </div>
    )
  }

  const formatDate = (dateStr) => {
    const date = new Date(dateStr)
    const today = new Date()
    const yesterday = new Date(today)
    yesterday.setDate(yesterday.getDate() - 1)

    if (date.toDateString() === today.toDateString()) {
      return `Today at ${date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`
    }
    if (date.toDateString() === yesterday.toDateString()) {
      return `Yesterday at ${date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`
    }
    return date.toLocaleDateString([], {
      month: 'short',
      day: 'numeric',
      hour: '2-digit',
      minute: '2-digit'
    })
  }

  return (
    <div className="walk-list">
      {walks.map((walk) => (
        <div
          key={walk.id}
          className={`walk-item ${selectedWalk?.id === walk.id ? 'selected' : ''}`}
          onClick={() => onSelect(walk)}
        >
          <div className="walk-item-header">
            <span className="walk-date">{formatDate(walk.startTime)}</span>
            <span className="walk-duration">{walk.durationFormatted}</span>
          </div>

          <div className="walk-item-stats">
            <div className="stat">
              <span className="stat-value">{walk.totalDistanceKm}</span>
              <span className="stat-label">km</span>
            </div>
            <div className="stat">
              <span className="stat-value">{walk.avgSpeedKmh}</span>
              <span className="stat-label">km/h avg</span>
            </div>
            <div className="stat">
              <span className="stat-value">{walk.maxSpeedKmh}</span>
              <span className="stat-label">km/h max</span>
            </div>
          </div>

          <div className="walk-item-meta">
            <span className="device-id">{walk.deviceId}</span>
            <span className="point-count">{walk.pointCount} pts</span>
          </div>
        </div>
      ))}
    </div>
  )
}

export default WalkList
