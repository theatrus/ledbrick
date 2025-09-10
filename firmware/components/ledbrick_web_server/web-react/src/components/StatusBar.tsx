import { useState } from 'react';
import type { Status, Schedule } from '../types';
import { StatusBarControls } from './StatusBarControls';
import { ChannelControl } from './ChannelControl';

interface StatusBarProps {
  status: Status | null;
  schedule?: Schedule | null;
  onUpdate: () => void;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

export function StatusBar({ status, schedule, onUpdate }: StatusBarProps) {
  const [selectedChannel, setSelectedChannel] = useState<number | null>(null);

  if (!status) return null;

  const handleChannelClick = (channelIndex: number) => {
    // Only allow manual control when scheduler is disabled
    if (!status.enabled) {
      setSelectedChannel(channelIndex);
    }
  };

  return (
    <div className="status-bar">
      {/* Time, astronomical info, and sensors */}
      <div className="status-bar-info">
        <div className="status-grid">
          <div className="status-item">
            <strong>Time:</strong> {status.time_formatted || '--:--'}
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
          {(status.voltage !== undefined || status.total_current !== undefined) && (
            <div className="status-item">
              {status.voltage !== undefined && (
                <span><strong>V:</strong> {status.voltage.toFixed(1)}V</span>
              )}
              {status.voltage !== undefined && status.total_current !== undefined && (
                <span style={{ margin: '0 4px' }}>|</span>
              )}
              {status.total_current !== undefined && (
                <span><strong>I:</strong> {status.total_current.toFixed(2)}A</span>
              )}
            </div>
          )}
          {status.fan_speed !== undefined && (
            <div className="status-item">
              <strong>Fan:</strong> {status.fan_on ? `${Math.round(status.fan_speed)}RPM` : 'Off'}
            </div>
          )}
          {status.temperatures && status.temperatures.length > 0 && (
            <div className="status-item">
              <strong>Temp:</strong> 
              {status.temperatures.map((temp, i) => (
                <span key={i}>
                  {i > 0 && ', '}
                  {temp.value.toFixed(1)}°C{temp.name ? ` (${temp.name})` : ''}
                </span>
              ))}
            </div>
          )}
        </div>
      </div>
      
      {/* LED Channels - wraps first on smaller screens */}
      {status.channels && status.channels.length > 0 && (
        <div className="status-bar-channels">
          <div className="channels-grid">
            {status.channels.map((channel, index) => {
              const color = schedule?.channel_configs?.[index]?.rgb_hex || 
                            DEFAULT_CHANNEL_COLORS[index % DEFAULT_CHANNEL_COLORS.length];
              return (
                <div 
                  key={channel.id} 
                  className={`channel-current ${!status.enabled ? 'clickable' : ''}`}
                  style={{ 
                    borderLeft: `3px solid ${color}`,
                    cursor: !status.enabled ? 'pointer' : 'default'
                  }}
                  onClick={() => handleChannelClick(index)}
                  title={!status.enabled ? 'Click to control manually' : ''}
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
        </div>
      )}
      {/* Controls */}
      <div className="status-bar-controls">
        <StatusBarControls
          enabled={status.enabled}
          pwmScale={status.pwm_scale || 100}
          onUpdate={onUpdate}
        />
      </div>
      
      {selectedChannel !== null && !status.enabled && (
        <>
          <div 
            style={{
              position: 'fixed',
              top: 0,
              left: 0,
              right: 0,
              bottom: 0,
              background: 'rgba(0,0,0,0.5)',
              zIndex: 1000
            }}
            onClick={() => setSelectedChannel(null)}
          />
          <ChannelControl
            channel={selectedChannel}
            currentPwm={status.channels[selectedChannel]?.pwm || 0}
            currentAmperage={status.channels[selectedChannel]?.current || 0}
            maxCurrent={schedule?.channel_configs?.[selectedChannel]?.max_current || 2.0}
            channelName={schedule?.channel_configs?.[selectedChannel]?.name || `Channel ${selectedChannel + 1}`}
            channelColor={schedule?.channel_configs?.[selectedChannel]?.rgb_hex || DEFAULT_CHANNEL_COLORS[selectedChannel % DEFAULT_CHANNEL_COLORS.length]}
            onClose={() => setSelectedChannel(null)}
            onUpdate={onUpdate}
          />
        </>
      )}
    </div>
  );
}