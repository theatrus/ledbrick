import { useState, useEffect } from 'react';

interface DebugData {
  astronomical_times: {
    sunrise_minutes: number;
    sunset_minutes: number;
    sunrise_formatted: string;
    sunset_formatted: string;
  };
  projected_astronomical_times?: {
    sunrise_minutes: number;
    sunset_minutes: number;
    time_shift_hours: number;
    time_shift_minutes: number;
  };
  resolved_schedule: any;
  debug_points: Array<{
    index: number;
    time_type: string;
    offset_minutes: number;
    calculated_time_minutes: number;
    is_dynamic: boolean;
  }>;
  current_time_minutes: number;
  scheduler_enabled: boolean;
  astronomical_projection_enabled: boolean;
}

interface ScheduleDebugProps {
  onClose: () => void;
}

export function ScheduleDebug({ onClose }: ScheduleDebugProps) {
  const [debugData, setDebugData] = useState<DebugData | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchDebugData = async () => {
    setLoading(true);
    setError(null);
    try {
      const response = await fetch('/api/schedule/debug');
      if (!response.ok) {
        throw new Error(`HTTP error! status: ${response.status}`);
      }
      const data = await response.json();
      setDebugData(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Unknown error');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchDebugData();
  }, []);

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-dialog modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Schedule Debug Information</h2>
          <button className="modal-close" onClick={onClose}>&times;</button>
        </div>
        <div className="modal-body">
          {loading && <div>Loading debug data...</div>}
          {error && <div className="error-banner">Error: {error}</div>}
          {debugData && (
            <>
              <h3>Current State</h3>
              <div className="debug-info">
                <p><strong>Current Time:</strong> {Math.floor(debugData.current_time_minutes / 60)}:{String(debugData.current_time_minutes % 60).padStart(2, '0')} (minute {debugData.current_time_minutes})</p>
                <p><strong>Scheduler Enabled:</strong> {debugData.scheduler_enabled ? 'Yes' : 'No'}</p>
                <p><strong>Astronomical Projection:</strong> {debugData.astronomical_projection_enabled ? 'Yes' : 'No'}</p>
              </div>

              <h3>Astronomical Times</h3>
              <div className="debug-info">
                <p><strong>Sunrise:</strong> {debugData.astronomical_times.sunrise_formatted} (minute {debugData.astronomical_times.sunrise_minutes})</p>
                <p><strong>Sunset:</strong> {debugData.astronomical_times.sunset_formatted} (minute {debugData.astronomical_times.sunset_minutes})</p>
              </div>

              {debugData.projected_astronomical_times && (
                <>
                  <h3>Projected Astronomical Times</h3>
                  <div className="debug-info">
                    <p><strong>Time Shift:</strong> {debugData.projected_astronomical_times.time_shift_hours}h {debugData.projected_astronomical_times.time_shift_minutes}m</p>
                    <p><strong>Projected Sunrise:</strong> minute {debugData.projected_astronomical_times.sunrise_minutes}</p>
                    <p><strong>Projected Sunset:</strong> minute {debugData.projected_astronomical_times.sunset_minutes}</p>
                  </div>
                </>
              )}

              <h3>Schedule Points</h3>
              <table className="schedule-table">
                <thead>
                  <tr>
                    <th>Index</th>
                    <th>Type</th>
                    <th>Offset</th>
                    <th>Calculated Time</th>
                    <th>Dynamic</th>
                  </tr>
                </thead>
                <tbody>
                  {debugData.debug_points.map((point) => (
                    <tr key={point.index}>
                      <td>{point.index}</td>
                      <td>{point.time_type}</td>
                      <td>{point.offset_minutes}</td>
                      <td>{Math.floor(point.calculated_time_minutes / 60)}:{String(point.calculated_time_minutes % 60).padStart(2, '0')}</td>
                      <td>{point.is_dynamic ? 'Yes' : 'No'}</td>
                    </tr>
                  ))}
                </tbody>
              </table>

              <h3>Raw Schedule Data</h3>
              <pre className="json-textarea">
                {JSON.stringify(debugData.resolved_schedule, null, 2)}
              </pre>
            </>
          )}
        </div>
        <div className="modal-footer">
          <button className="button-secondary" onClick={fetchDebugData}>
            Refresh
          </button>
          <button className="button-primary" onClick={onClose}>
            Close
          </button>
        </div>
      </div>
    </div>
  );
}