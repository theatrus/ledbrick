import type { Status, Schedule } from '../types';

interface StatusBarProps {
  status: Status | null;
  schedule?: Schedule | null;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

export function StatusBar({ status, schedule }: StatusBarProps) {
  if (!status) return null;

  return (
    <div className="status-bar">
      <div className="status-item">
        <strong>Status:</strong> {status.enabled ? 'Enabled' : 'Disabled'}
      </div>
      <div className="status-item">
        <strong>Time:</strong> {status.time_formatted || '--:--'}
      </div>
      <div className="status-item">
        <strong>Points:</strong> {status.schedule_points}
      </div>
      {status.sunrise_time && (
        <div className="status-item">
          <strong>Sunrise:</strong> {status.sunrise_time}
        </div>
      )}
      {status.sunset_time && (
        <div className="status-item">
          <strong>Sunset:</strong> {status.sunset_time}
        </div>
      )}
      {status.moon_phase !== undefined && (
        <div className="status-item">
          <strong>Moon:</strong> {(status.moon_phase * 100).toFixed(1)}%
        </div>
      )}
      
      {status.channels && status.channels.length > 0 && (
        <div className="current-values">
          {status.channels.map((channel, index) => {
            const color = schedule?.channel_configs?.[index]?.rgb_hex || 
                          DEFAULT_CHANNEL_COLORS[index % DEFAULT_CHANNEL_COLORS.length];
            return (
              <div 
                key={channel.id} 
                className="channel-current"
                style={{ borderLeft: `3px solid ${color}` }}
              >
                <span className="channel-label" style={{ color }}>
                  Ch{channel.id}:
                </span>
                <span className="channel-value">
                  {Math.round(channel.pwm || 0)}% ({(channel.current || 0).toFixed(2)}A)
                </span>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}