import { useState, useEffect } from 'react';
import { api } from '../api/client';
import type { Schedule } from '../types';

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

const COMMON_LOCATIONS = [
  { name: 'San Francisco, CA', lat: 37.7749, lon: -122.4194 },
  { name: 'Los Angeles, CA', lat: 34.0522, lon: -118.2437 },
  { name: 'New York, NY', lat: 40.7128, lon: -74.0060 },
  { name: 'Seattle, WA', lat: 47.6062, lon: -122.3321 },
  { name: 'Miami, FL', lat: 25.7617, lon: -80.1918 },
  { name: 'Denver, CO', lat: 39.7392, lon: -104.9903 },
  { name: 'Chicago, IL', lat: 41.8781, lon: -87.6298 },
  { name: 'Phoenix, AZ', lat: 33.4484, lon: -112.0740 },
];

export function SettingsModal({ isOpen, onClose, schedule, onUpdate }: SettingsModalProps) {
  const [activeTab, setActiveTab] = useState<'channels' | 'location'>('channels');
  const [channelConfigs, setChannelConfigs] = useState<ChannelConfigForm[]>([]);
  const [hasChanges, setHasChanges] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  
  // Location settings
  const [latitude, setLatitude] = useState<string>('');
  const [longitude, setLongitude] = useState<string>('');
  const [timezoneOffset, setTimezoneOffset] = useState<string>('');
  const [astronomicalProjection, setAstronomicalProjection] = useState(false);
  const [timeShiftHours, setTimeShiftHours] = useState<string>('0');
  const [timeShiftMinutes, setTimeShiftMinutes] = useState<string>('0');

  useEffect(() => {
    if (schedule && isOpen) {
      // Initialize channel configs
      const configs: ChannelConfigForm[] = [];
      for (let i = 0; i < schedule.num_channels; i++) {
        const config = schedule.channel_configs?.[i];
        configs.push({
          name: config?.name || `Channel ${i + 1}`,
          rgb_hex: config?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length],
          max_current: config?.max_current || 2.0
        });
      }
      setChannelConfigs(configs);
      
      // Initialize location settings
      setLatitude(schedule.latitude?.toString() || '37.7749');
      setLongitude(schedule.longitude?.toString() || '-122.4194');
      setTimezoneOffset(schedule.timezone_offset_hours?.toString() || '0');
      setAstronomicalProjection(schedule.astronomical_projection || false);
      setTimeShiftHours(schedule.time_shift_hours?.toString() || '0');
      setTimeShiftMinutes(schedule.time_shift_minutes?.toString() || '0');
      
      setHasChanges(false);
      setError(null);
    }
  }, [schedule, isOpen]);

  if (!isOpen || !schedule) return null;

  const handleChannelConfigChange = (index: number, field: keyof ChannelConfigForm, value: string | number) => {
    const newConfigs = [...channelConfigs];
    newConfigs[index] = {
      ...newConfigs[index],
      [field]: field === 'max_current' ? parseFloat(value as string) || 0 : value
    };
    setChannelConfigs(newConfigs);
    setHasChanges(true);
  };

  const handleLocationPreset = (preset: typeof COMMON_LOCATIONS[0]) => {
    setLatitude(preset.lat.toString());
    setLongitude(preset.lon.toString());
    setHasChanges(true);
  };

  const handleSave = async () => {
    setSaving(true);
    setError(null);
    
    try {
      if (activeTab === 'channels') {
        // Save channel configurations
        await api.updateChannelConfigs(channelConfigs);
      } else {
        // Save location settings
        await api.updateLocation(
          parseFloat(latitude),
          parseFloat(longitude),
          parseFloat(timezoneOffset)
        );
        
        await api.updateTimeProjection(
          astronomicalProjection,
          parseInt(timeShiftHours) || 0,
          parseInt(timeShiftMinutes) || 0
        );
      }
      
      setHasChanges(false);
      await onUpdate();
    } catch (err: any) {
      setError(err.message || 'Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  return (
    <>
      <div className="modal-overlay" onClick={onClose} />
      <div className="modal-dialog modal-large">
        <div className="modal-header">
          <h2>Settings</h2>
          <button className="modal-close" onClick={onClose}>&times;</button>
        </div>
        
        <div className="modal-body">
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
          </div>

          {error && (
            <div className="status-message error">{error}</div>
          )}

          {activeTab === 'channels' ? (
            <div className="dual-channel-grid">
              {channelConfigs.map((config, index) => (
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
          ) : (
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
                <h3>Common Locations</h3>
                <div className="preset-buttons" style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)' }}>
                  {COMMON_LOCATIONS.map((location) => (
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
    </>
  );
}