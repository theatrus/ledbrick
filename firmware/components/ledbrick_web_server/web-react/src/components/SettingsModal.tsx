import { useState, useEffect } from 'react';
import { api } from '../api/client';
import type { Schedule, TemperatureConfig, FanCurve } from '../types';
import { Line } from 'react-chartjs-2';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend
} from 'chart.js';

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend
);

interface SettingsModalProps {
  isOpen: boolean;
  onClose: () => void;
  schedule: Schedule | null;
  onUpdate: () => void;
}

interface ChannelConfigForm {
  name: string;
  rgb_hex: string;
  max_current: number;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

// Tropical reef locations around the world
const REEF_PRESETS = [
  // Pacific Ocean
  { name: 'Great Barrier Reef, Australia', lat: -16.2859, lon: 145.7781 },
  { name: 'Coral Triangle, Indonesia', lat: -2.5416, lon: 120.7590 },
  { name: 'Palau Rock Islands', lat: 7.5150, lon: 134.5825 },
  { name: 'Fiji Coral Reefs', lat: -17.7134, lon: 178.0650 },
  { name: 'Tubbataha Reefs, Philippines', lat: 8.8575, lon: 119.9200 },
  { name: 'French Polynesia Atolls', lat: -17.6797, lon: -149.4068 },
  // Indian Ocean
  { name: 'Maldives Atolls', lat: 3.2028, lon: 73.2207 },
  { name: 'Andaman Sea Reefs, Thailand', lat: 9.1537, lon: 98.3366 },
  { name: 'Seychelles Coral Reefs', lat: -4.6796, lon: 55.4920 },
  { name: 'Chagos Archipelago', lat: -6.3400, lon: 71.8800 },
  // Atlantic Ocean
  { name: 'Caribbean Coral Reef, Puerto Rico', lat: 18.2208, lon: -66.5901 },
  { name: 'Belize Barrier Reef', lat: 17.1899, lon: -87.9407 },
  { name: 'Bahamas Banks', lat: 24.0954, lon: -76.0000 },
  { name: 'Florida Keys Reef', lat: 24.6631, lon: -81.2717 },
  { name: 'Turks and Caicos', lat: 21.6940, lon: -71.7979 },
  // Red Sea
  { name: 'Red Sea Coral Reefs, Egypt', lat: 27.2946, lon: 33.8317 },
  { name: 'Eilat Coral Beach, Israel', lat: 29.5035, lon: 34.9200 },
  { name: 'Farasan Islands, Saudi Arabia', lat: 16.7056, lon: 42.0361 },
];

export function SettingsModal({ isOpen, onClose, schedule, onUpdate }: SettingsModalProps) {
  const [activeTab, setActiveTab] = useState<'channels' | 'location' | 'temperature'>('channels');
  const [channelConfigs, setChannelConfigs] = useState<ChannelConfigForm[]>([]);
  const [hasChanges, setHasChanges] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [localSchedule, setLocalSchedule] = useState<Schedule | null>(schedule);
  
  // Temperature control state
  const [tempConfig, setTempConfig] = useState<TemperatureConfig | null>(null);
  const [fanCurve, setFanCurve] = useState<FanCurve | null>(null);
  const [loadingTemp, setLoadingTemp] = useState(false);
  
  // Location settings
  const [latitude, setLatitude] = useState<string>('');
  const [longitude, setLongitude] = useState<string>('');
  const [timezoneOffset, setTimezoneOffset] = useState<string>('');
  const [astronomicalProjection, setAstronomicalProjection] = useState(false);
  const [timeShiftHours, setTimeShiftHours] = useState<string>('0');
  const [timeShiftMinutes, setTimeShiftMinutes] = useState<string>('0');

  useEffect(() => {
    if (isOpen && !schedule && !loading) {
      // Load schedule if not available
      setLoading(true);
      api.getSchedule()
        .then(scheduleData => {
          setLocalSchedule(scheduleData);
          setLoading(false);
        })
        .catch(err => {
          setError(err.message || 'Failed to load schedule');
          setLoading(false);
        });
    }
  }, [isOpen, schedule, loading]);

  useEffect(() => {
    const currentSchedule = schedule || localSchedule;
    if (currentSchedule && isOpen) {
      // Initialize channel configs
      const configs: ChannelConfigForm[] = [];
      for (let i = 0; i < currentSchedule.num_channels; i++) {
        const config = currentSchedule.channel_configs?.[i];
        configs.push({
          name: config?.name || `Channel ${i + 1}`,
          rgb_hex: config?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length],
          max_current: config?.max_current || 2.0
        });
      }
      setChannelConfigs(configs);
      
      // Initialize location settings
      setLatitude(currentSchedule.latitude?.toString() || '37.7749');
      setLongitude(currentSchedule.longitude?.toString() || '-122.4194');
      setTimezoneOffset(currentSchedule.timezone_offset_hours?.toString() || '0');
      setAstronomicalProjection(currentSchedule.astronomical_projection || false);
      setTimeShiftHours(currentSchedule.time_shift_hours?.toString() || '0');
      setTimeShiftMinutes(currentSchedule.time_shift_minutes?.toString() || '0');
      
      setHasChanges(false);
      setError(null);
    }
  }, [schedule, localSchedule, isOpen]);

  // Load temperature config when temperature tab is selected
  useEffect(() => {
    if (isOpen && activeTab === 'temperature' && !tempConfig && !loadingTemp) {
      loadTemperatureConfig();
    }
  }, [isOpen, activeTab, tempConfig, loadingTemp]);

  const loadTemperatureConfig = async () => {
    setLoadingTemp(true);
    setError(null);
    try {
      const [config, curve] = await Promise.all([
        api.getTemperatureConfig(),
        api.getFanCurve()
      ]);
      if (config) {
        setTempConfig(config);
        // Only set fan curve if it's not null
        if (curve !== null) {
          setFanCurve(curve);
        }
      } else {
        // Temperature control not available - this is not an error
        // Just don't show the temperature tab
        setTempConfig(null);
        setFanCurve(null);
      }
    } catch (err: any) {
      setError('Failed to load temperature configuration: ' + err.message);
    } finally {
      setLoadingTemp(false);
    }
  };

  if (!isOpen) return null;

  const handleChannelConfigChange = (index: number, field: keyof ChannelConfigForm, value: string | number) => {
    const newConfigs = [...channelConfigs];
    newConfigs[index] = {
      ...newConfigs[index],
      [field]: field === 'max_current' ? parseFloat(value as string) || 0 : value
    };
    setChannelConfigs(newConfigs);
    setHasChanges(true);
  };

  const handleLocationPreset = (preset: typeof REEF_PRESETS[0]) => {
    setLatitude(preset.lat.toString());
    setLongitude(preset.lon.toString());
    setHasChanges(true);
  };

  const updateTempConfig = (field: keyof TemperatureConfig, value: number) => {
    if (!tempConfig) return;
    setTempConfig({ ...tempConfig, [field]: value });
    setHasChanges(true);
  };

  const handleSave = async () => {
    setSaving(true);
    setError(null);
    
    try {
      if (activeTab === 'channels') {
        // Save channel configurations
        await api.updateChannelConfigs(channelConfigs);
      } else if (activeTab === 'location') {
        // Save location settings
        // The backend now accepts timezone_offset_hours in the location endpoint
        await api.updateLocation(
          parseFloat(latitude),
          parseFloat(longitude),
          parseFloat(timezoneOffset)
        );
        
        // Save time projection settings using the current endpoint
        await api.updateTimeProjection(
          astronomicalProjection,
          parseInt(timeShiftHours) || 0,
          parseInt(timeShiftMinutes) || 0
        );
      } else if (activeTab === 'temperature' && tempConfig) {
        // Save temperature configuration
        await api.updateTemperatureConfig(tempConfig);
        // Reload fan curve to reflect new settings
        const curve = await api.getFanCurve();
        if (curve !== null) {
          setFanCurve(curve);
        }
        // If curve is null, temperature control is not available - don't update
      }
      
      setHasChanges(false);
      await onUpdate();
      // Close modal after saving temperature settings
      if (activeTab === 'temperature') {
        onClose();
      }
      // Don't close modal after save for other tabs - user might want to continue editing
    } catch (err: any) {
      setError(err.message || 'Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-dialog modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Settings</h2>
          <button className="modal-close" onClick={onClose}>&times;</button>
        </div>
        
        <div className="modal-body">
          {loading ? (
            <div style={{ padding: '60px', textAlign: 'center' }}>
              <div className="loading">Loading settings...</div>
            </div>
          ) : (
          <>
          <div className="preset-buttons" style={{ marginBottom: '20px' }}>
            <button
              className={`preset-button ${activeTab === 'channels' ? 'active' : ''}`}
              style={{ 
                background: activeTab === 'channels' ? '#4a9eff' : '#444',
                color: activeTab === 'channels' ? '#fff' : '#e0e0e0'
              }}
              onClick={() => setActiveTab('channels')}
            >
              Channel Configuration
            </button>
            <button
              className={`preset-button ${activeTab === 'location' ? 'active' : ''}`}
              style={{ 
                background: activeTab === 'location' ? '#4a9eff' : '#444',
                color: activeTab === 'location' ? '#fff' : '#e0e0e0'
              }}
              onClick={() => setActiveTab('location')}
            >
              Location & Time
            </button>
            <button
              className={`preset-button ${activeTab === 'temperature' ? 'active' : ''}`}
              style={{ 
                background: activeTab === 'temperature' ? '#4a9eff' : '#444',
                color: activeTab === 'temperature' ? '#fff' : '#e0e0e0'
              }}
              onClick={() => setActiveTab('temperature')}
            >
              Temperature Control
            </button>
          </div>

          {error && (
            <div className="status-message error">{error}</div>
          )}

          {activeTab === 'channels' ? (
            <div className="dual-channel-grid">
              {channelConfigs.length === 0 ? (
                <div style={{ gridColumn: '1 / -1', textAlign: 'center', padding: '40px' }}>
                  <div className="loading">Loading channel configuration...</div>
                </div>
              ) : channelConfigs.map((config, index) => (
                <div key={index} className="channel-dual-input">
                  <div className="channel-header" style={{ color: config.rgb_hex }}>
                    Channel {index + 1}
                  </div>
                  
                  <div className="control-group">
                    <label className="control-label">Name</label>
                    <input
                      type="text"
                      className="control-input"
                      value={config.name}
                      onChange={(e) => handleChannelConfigChange(index, 'name', e.target.value)}
                    />
                  </div>
                  
                  <div className="control-group">
                    <label className="control-label">Color</label>
                    <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                      <input
                        type="color"
                        value={config.rgb_hex}
                        onChange={(e) => handleChannelConfigChange(index, 'rgb_hex', e.target.value)}
                        style={{ 
                          width: '50px', 
                          height: '30px',
                          border: '1px solid #444',
                          borderRadius: '4px',
                          cursor: 'pointer'
                        }}
                      />
                      <input
                        type="text"
                        className="control-input"
                        value={config.rgb_hex}
                        onChange={(e) => handleChannelConfigChange(index, 'rgb_hex', e.target.value)}
                        pattern="^#[0-9A-Fa-f]{6}$"
                        style={{ flex: 1 }}
                      />
                    </div>
                  </div>
                  
                  <div className="control-group">
                    <label className="control-label">Max Current (A)</label>
                    <input
                      type="number"
                      className="control-input"
                      value={config.max_current}
                      onChange={(e) => handleChannelConfigChange(index, 'max_current', e.target.value)}
                      min="0"
                      max="10"
                      step="0.1"
                    />
                  </div>
                </div>
              ))}
            </div>
          ) : activeTab === 'location' ? (
            <>
              <div className="form-row">
                <div className="form-group">
                  <label>Latitude</label>
                  <input
                    type="number"
                    className="form-control"
                    value={latitude}
                    onChange={(e) => { setLatitude(e.target.value); setHasChanges(true); }}
                    step="0.0001"
                    min="-90"
                    max="90"
                  />
                </div>
                <div className="form-group">
                  <label>Longitude</label>
                  <input
                    type="number"
                    className="form-control"
                    value={longitude}
                    onChange={(e) => { setLongitude(e.target.value); setHasChanges(true); }}
                    step="0.0001"
                    min="-180"
                    max="180"
                  />
                </div>
              </div>
              
              <div className="form-group">
                <label>Timezone Offset (hours from UTC)</label>
                <input
                  type="number"
                  className="form-control"
                  value={timezoneOffset}
                  onChange={(e) => { setTimezoneOffset(e.target.value); setHasChanges(true); }}
                  step="0.5"
                  min="-12"
                  max="12"
                />
              </div>
              
              <div className="location-presets">
                <h3>Reef Locations</h3>
                <div className="preset-buttons" style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', maxHeight: '300px', overflowY: 'auto' }}>
                  {REEF_PRESETS.map((location) => (
                    <button
                      key={location.name}
                      className="preset-button"
                      onClick={() => handleLocationPreset(location)}
                    >
                      {location.name}
                    </button>
                  ))}
                </div>
              </div>
              
              <div className="presets-section">
                <h3>Time Projection</h3>
                <div className="form-group">
                  <label className="checkbox-label">
                    <input
                      type="checkbox"
                      checked={astronomicalProjection}
                      onChange={(e) => { setAstronomicalProjection(e.target.checked); setHasChanges(true); }}
                    />
                    Enable Astronomical Time Projection
                  </label>
                  <div className="help-text">
                    When enabled, sunrise and sunset times are projected to appear at different times, 
                    useful for replicating lighting from different geographic locations.
                  </div>
                </div>
                
                {astronomicalProjection && (
                  <div className="form-row">
                    <div className="form-group">
                      <label>Time Shift Hours</label>
                      <input
                        type="number"
                        className="form-control"
                        value={timeShiftHours}
                        onChange={(e) => { setTimeShiftHours(e.target.value); setHasChanges(true); }}
                        min="-23"
                        max="23"
                      />
                    </div>
                    <div className="form-group">
                      <label>Time Shift Minutes</label>
                      <input
                        type="number"
                        className="form-control"
                        value={timeShiftMinutes}
                        onChange={(e) => { setTimeShiftMinutes(e.target.value); setHasChanges(true); }}
                        min="-59"
                        max="59"
                      />
                    </div>
                  </div>
                )}
              </div>
            </>
          ) : (
            // Temperature Control Tab
            loadingTemp ? (
              <div style={{ padding: '60px', textAlign: 'center' }}>
                <div className="loading">Loading temperature configuration...</div>
              </div>
            ) : tempConfig ? (
              <>
                <div className="form-row">
                  <div className="form-group">
                    <label>Target Temperature (°C)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.target_temp_c}
                      onChange={(e) => updateTempConfig('target_temp_c', parseFloat(e.target.value))}
                      step="0.5"
                      min="20"
                      max="70"
                    />
                    <div className="help-text">
                      Temperature setpoint for PID control
                    </div>
                  </div>

                  <div className="form-group">
                    <label>Emergency Temperature (°C)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.emergency_temp_c}
                      onChange={(e) => updateTempConfig('emergency_temp_c', parseFloat(e.target.value))}
                      step="1"
                      min="50"
                      max="100"
                    />
                    <div className="help-text">
                      Temperature that triggers emergency shutdown
                    </div>
                  </div>

                  <div className="form-group">
                    <label>Recovery Temperature (°C)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.recovery_temp_c}
                      onChange={(e) => updateTempConfig('recovery_temp_c', parseFloat(e.target.value))}
                      step="1"
                      min="40"
                      max="90"
                    />
                    <div className="help-text">
                      Temperature to recover from emergency
                    </div>
                  </div>
                </div>

                <h3>PID Controller Settings</h3>
                <div className="form-row">
                  <div className="form-group">
                    <label>Proportional (Kp)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.kp}
                      onChange={(e) => updateTempConfig('kp', parseFloat(e.target.value))}
                      step="0.1"
                      min="0"
                      max="10"
                    />
                  </div>

                  <div className="form-group">
                    <label>Integral (Ki)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.ki}
                      onChange={(e) => updateTempConfig('ki', parseFloat(e.target.value))}
                      step="0.01"
                      min="0"
                      max="1"
                    />
                  </div>

                  <div className="form-group">
                    <label>Derivative (Kd)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.kd}
                      onChange={(e) => updateTempConfig('kd', parseFloat(e.target.value))}
                      step="0.1"
                      min="0"
                      max="5"
                    />
                  </div>
                </div>

                <h3>Fan Control</h3>
                <div className="form-row">
                  <div className="form-group">
                    <label>Minimum Fan PWM (%)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.min_fan_pwm}
                      onChange={(e) => updateTempConfig('min_fan_pwm', parseFloat(e.target.value))}
                      step="5"
                      min="0"
                      max="100"
                    />
                  </div>

                  <div className="form-group">
                    <label>Maximum Fan PWM (%)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.max_fan_pwm}
                      onChange={(e) => updateTempConfig('max_fan_pwm', parseFloat(e.target.value))}
                      step="5"
                      min="0"
                      max="100"
                    />
                  </div>

                  <div className="form-group">
                    <label>Update Interval (ms)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.fan_update_interval_ms}
                      onChange={(e) => updateTempConfig('fan_update_interval_ms', parseInt(e.target.value))}
                      step="100"
                      min="100"
                      max="10000"
                    />
                  </div>
                </div>

                <h3>Advanced Settings</h3>
                <div className="form-row">
                  <div className="form-group">
                    <label>Emergency Delay (ms)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.emergency_delay_ms}
                      onChange={(e) => updateTempConfig('emergency_delay_ms', parseInt(e.target.value))}
                      step="1000"
                      min="0"
                      max="60000"
                    />
                    <div className="help-text">
                      Time temperature must exceed limit before emergency
                    </div>
                  </div>

                  <div className="form-group">
                    <label>Sensor Timeout (ms)</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.sensor_timeout_ms}
                      onChange={(e) => updateTempConfig('sensor_timeout_ms', parseInt(e.target.value))}
                      step="1000"
                      min="1000"
                      max="60000"
                    />
                    <div className="help-text">
                      Maximum age for sensor readings
                    </div>
                  </div>

                  <div className="form-group">
                    <label>Temperature Filter Alpha</label>
                    <input
                      type="number"
                      className="form-control"
                      value={tempConfig.temp_filter_alpha}
                      onChange={(e) => updateTempConfig('temp_filter_alpha', parseFloat(e.target.value))}
                      step="0.05"
                      min="0"
                      max="1"
                    />
                    <div className="help-text">
                      Low-pass filter coefficient (0-1, higher = less filtering)
                    </div>
                  </div>
                </div>

                {fanCurve && (
                  <div className="chart-container" style={{ height: '300px', marginTop: '20px' }}>
                    <Line 
                      data={{
                        labels: fanCurve.points.map(p => p.temperature.toFixed(0)),
                        datasets: [
                          {
                            label: 'Fan PWM %',
                            data: fanCurve.points.map(p => p.fan_pwm),
                            borderColor: 'rgb(74, 158, 255)',
                            backgroundColor: 'rgba(74, 158, 255, 0.1)',
                            tension: 0,
                          }
                        ]
                      }} 
                      options={{
                        responsive: true,
                        maintainAspectRatio: false,
                        plugins: {
                          legend: {
                            display: true,
                            labels: {
                              color: '#e0e0e0'
                            }
                          },
                          title: {
                            display: true,
                            text: 'Fan Response Curve',
                            color: '#e0e0e0'
                          },
                          tooltip: {
                            callbacks: {
                              label: (context: any) => {
                                return `${context.parsed.y.toFixed(1)}% at ${context.label}°C`;
                              }
                            }
                          }
                        },
                        scales: {
                          y: {
                            beginAtZero: true,
                            max: 100,
                            title: {
                              display: true,
                              text: 'Fan PWM %',
                              color: '#e0e0e0'
                            },
                            ticks: {
                              color: '#e0e0e0'
                            },
                            grid: {
                              color: '#444'
                            }
                          },
                          x: {
                            title: {
                              display: true,
                              text: 'Temperature °C',
                              color: '#e0e0e0'
                            },
                            ticks: {
                              color: '#e0e0e0'
                            },
                            grid: {
                              color: '#444'
                            }
                          }
                        }
                      }} 
                    />
                  </div>
                )}
              </>
            ) : (
              <div style={{ padding: '60px', textAlign: 'center' }}>
                <div>Temperature control not available</div>
              </div>
            )
          )}
          </>
          )}
        </div>
        
        <div className="modal-footer">
          <button className="button-secondary" onClick={onClose}>
            Cancel
          </button>
          <button 
            className={`button-primary ${hasChanges ? 'has-changes' : ''}`}
            onClick={handleSave}
            disabled={saving || !hasChanges}
          >
            {saving ? 'Saving...' : 'Save Changes'}
          </button>
        </div>
      </div>
    </div>
  );
}