import { useState, useEffect } from 'react';
import type { SchedulePoint, Schedule, DynamicTimeType } from '../types';

interface SchedulePointEditorProps {
  isOpen: boolean;
  onClose: () => void;
  onSave: (point: SchedulePoint) => void;
  point?: SchedulePoint;
  index?: number;
  schedule: Schedule;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

const TIME_TYPES: Array<{ value: DynamicTimeType; label: string }> = [
  { value: 'fixed', label: 'Fixed Time' },
  { value: 'sunrise_relative', label: 'Sunrise' },
  { value: 'solar_noon', label: 'Solar Noon' },
  { value: 'sunset_relative', label: 'Sunset' },
  { value: 'civil_dawn_relative', label: 'Civil Dawn' },
  { value: 'civil_dusk_relative', label: 'Civil Dusk' },
  { value: 'nautical_dawn_relative', label: 'Nautical Dawn' },
  { value: 'nautical_dusk_relative', label: 'Nautical Dusk' },
];

export function SchedulePointEditor({
  isOpen,
  onClose,
  onSave,
  point,
  index,
  schedule
}: SchedulePointEditorProps) {
  const isEditing = point !== undefined;
  
  // Form state
  const [timeType, setTimeType] = useState<DynamicTimeType>('fixed');
  const [timeMinutes, setTimeMinutes] = useState(720); // 12:00 default
  const [offsetMinutes, setOffsetMinutes] = useState(0);
  const [pwmValues, setPwmValues] = useState<number[]>(new Array(8).fill(0));
  const [currentValues, setCurrentValues] = useState<number[]>(new Array(8).fill(0));

  // Initialize form when point changes
  useEffect(() => {
    if (point) {
      setTimeType((point.time_type as DynamicTimeType) || 'fixed');
      setTimeMinutes(point.time_minutes);
      setOffsetMinutes(point.offset_minutes || 0);
      setPwmValues([...point.pwm_values]);
      setCurrentValues([...point.current_values]);
    } else {
      // Reset to defaults for new point
      setTimeType('fixed');
      setTimeMinutes(720);
      setOffsetMinutes(0);
      setPwmValues(new Array(8).fill(0));
      setCurrentValues(new Array(8).fill(0));
    }
  }, [point]);

  const formatTime = (minutes: number) => {
    const hours = Math.floor(minutes / 60);
    const mins = minutes % 60;
    return `${hours.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}`;
  };

  const parseTime = (timeStr: string) => {
    const [hours, mins] = timeStr.split(':').map(Number);
    return hours * 60 + mins;
  };

  const handlePwmChange = (channel: number, value: number) => {
    const newValues = [...pwmValues];
    newValues[channel] = Math.max(0, Math.min(100, value));
    setPwmValues(newValues);
  };

  const handleCurrentChange = (channel: number, value: number) => {
    const maxCurrent = schedule.channel_configs?.[channel]?.max_current || 2.0;
    const newValues = [...currentValues];
    newValues[channel] = Math.max(0, Math.min(maxCurrent, value));
    setCurrentValues(newValues);
  };

  const handleSave = () => {
    // Default times for astronomical events (will be calculated by backend)
    const defaultTimes: Record<string, number> = {
      'sunrise_relative': 360,      // 6:00 AM
      'solar_noon': 720,            // 12:00 PM
      'sunset_relative': 1080,      // 6:00 PM
      'civil_dawn_relative': 330,   // 5:30 AM
      'civil_dusk_relative': 1110,  // 6:30 PM
      'nautical_dawn_relative': 300,// 5:00 AM
      'nautical_dusk_relative': 1140,// 7:00 PM
    };
    
    const newPoint: SchedulePoint = {
      time_minutes: timeType === 'fixed' ? timeMinutes : (defaultTimes[timeType] || 720),
      time_type: timeType,
      offset_minutes: timeType !== 'fixed' ? offsetMinutes : 0,
      pwm_values: pwmValues,
      current_values: currentValues,
    };
    onSave(newPoint);
  };

  const setAllChannels = (pwmValue: number) => {
    setPwmValues(new Array(8).fill(pwmValue));
    // Set current proportionally based on max current
    const newCurrents = currentValues.map((_, i) => {
      const maxCurrent = schedule.channel_configs?.[i]?.max_current || 2.0;
      return (pwmValue / 100) * maxCurrent;
    });
    setCurrentValues(newCurrents);
  };

  if (!isOpen) return null;

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-dialog" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>{isEditing ? 'Edit Schedule Point' : 'Add Schedule Point'}</h2>
          <button className="modal-close" onClick={onClose}>×</button>
        </div>
        
        <div className="modal-body">
          <div className="form-group">
            <label>Event Type:</label>
            <select 
              value={timeType} 
              onChange={(e) => setTimeType(e.target.value as DynamicTimeType)}
              className="form-control"
            >
              {TIME_TYPES.map(type => (
                <option key={type.value} value={type.value}>
                  {type.label}
                </option>
              ))}
            </select>
          </div>

          {timeType === 'fixed' ? (
            <div className="form-group">
              <label>Time:</label>
              <input
                type="time"
                value={formatTime(timeMinutes)}
                onChange={(e) => setTimeMinutes(parseTime(e.target.value))}
                className="form-control"
              />
            </div>
          ) : (
            <div className="form-group">
              <label>Offset (minutes):</label>
              <input
                type="number"
                value={offsetMinutes}
                onChange={(e) => setOffsetMinutes(Number(e.target.value))}
                className="form-control"
                min="-180"
                max="180"
              />
            </div>
          )}

          <div className="preset-buttons">
            <button onClick={() => setAllChannels(0)} className="preset-button">
              All Off
            </button>
            <button onClick={() => setAllChannels(50)} className="preset-button">
              50%
            </button>
            <button onClick={() => setAllChannels(100)} className="preset-button">
              100%
            </button>
          </div>

          <div className="dual-channel-grid">
            {Array.from({ length: schedule.num_channels }, (_, i) => {
              const channelConfig = schedule.channel_configs?.[i];
              const color = channelConfig?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length];
              const name = channelConfig?.name || `Channel ${i + 1}`;
              const maxCurrent = channelConfig?.max_current || 2.0;

              return (
                <div key={i} className="channel-dual-input">
                  <div className="channel-header" style={{ color }}>
                    {name}
                  </div>
                  
                  <div className="control-group">
                    <label className="control-label">PWM %</label>
                    <input
                      type="number"
                      value={pwmValues[i].toFixed(1)}
                      onChange={(e) => handlePwmChange(i, Number(e.target.value))}
                      className="control-input"
                      min="0"
                      max="100"
                      step="0.1"
                    />
                    <input
                      type="range"
                      value={pwmValues[i]}
                      onChange={(e) => handlePwmChange(i, Number(e.target.value))}
                      className="control-slider"
                      min="0"
                      max="100"
                      step="0.1"
                    />
                  </div>

                  <div className="control-group">
                    <label className="control-label">Current A</label>
                    <input
                      type="number"
                      value={currentValues[i].toFixed(2)}
                      onChange={(e) => handleCurrentChange(i, Number(e.target.value))}
                      className="control-input"
                      min="0"
                      max={maxCurrent}
                      step="0.01"
                    />
                    <input
                      type="range"
                      value={currentValues[i]}
                      onChange={(e) => handleCurrentChange(i, Number(e.target.value))}
                      className="control-slider"
                      min="0"
                      max={maxCurrent}
                      step="0.01"
                    />
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        <div className="modal-footer">
          <button onClick={onClose} className="button-secondary">
            Cancel
          </button>
          <button onClick={handleSave} className="button-primary">
            Save
          </button>
        </div>
      </div>
    </div>
  );
}