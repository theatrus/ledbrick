import { useState, useEffect } from 'react';
import { api } from '../api/client';

interface LocationModalProps {
  isOpen: boolean;
  onClose: () => void;
  latitude: number;
  longitude: number;
  astronomicalProjection: boolean;
  timeShiftHours: number;
  timeShiftMinutes: number;
  onUpdate: () => void;
}

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

export function LocationModal({
  isOpen,
  onClose,
  latitude,
  longitude,
  astronomicalProjection,
  timeShiftHours,
  timeShiftMinutes,
  onUpdate
}: LocationModalProps) {
  const [lat, setLat] = useState(latitude);
  const [lon, setLon] = useState(longitude);
  const [projection, setProjection] = useState(astronomicalProjection);
  const [shiftHours, setShiftHours] = useState(timeShiftHours);
  const [shiftMinutes, setShiftMinutes] = useState(timeShiftMinutes);
  const [isSaving, setIsSaving] = useState(false);
  const [activeTab, setActiveTab] = useState<'location' | 'time'>('location');

  // Reset state when modal opens
  useEffect(() => {
    if (isOpen) {
      setLat(latitude);
      setLon(longitude);
      setProjection(astronomicalProjection);
      setShiftHours(timeShiftHours);
      setShiftMinutes(timeShiftMinutes);
    }
  }, [isOpen, latitude, longitude, astronomicalProjection, timeShiftHours, timeShiftMinutes]);

  if (!isOpen) return null;

  const handleLocationSave = async () => {
    setIsSaving(true);
    try {
      await api.setLocation(lat, lon);
      await api.setTimeShift(projection, shiftHours, shiftMinutes);
      onUpdate();
      onClose();
    } catch (error) {
      console.error('Failed to save settings:', error);
      alert('Failed to save settings');
    } finally {
      setIsSaving(false);
    }
  };

  const detectLocation = () => {
    if ('geolocation' in navigator) {
      navigator.geolocation.getCurrentPosition(
        (position) => {
          setLat(position.coords.latitude);
          setLon(position.coords.longitude);
        },
        (error) => {
          console.error('Failed to get location:', error);
          alert('Failed to get location. Please check your browser permissions.');
        }
      );
    } else {
      alert('Geolocation is not supported by your browser.');
    }
  };

  const selectPreset = (preset: typeof REEF_PRESETS[0]) => {
    setLat(preset.lat);
    setLon(preset.lon);
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-dialog modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Location & Time Settings</h2>
          <button className="modal-close" onClick={onClose}>×</button>
        </div>

        <div className="modal-body">
          <div className="button-group" style={{ marginBottom: '20px' }}>
            <button 
              onClick={() => setActiveTab('location')} 
              className={activeTab === 'location' ? 'button-primary' : 'button-secondary'}
            >
              Location
            </button>
            <button 
              onClick={() => setActiveTab('time')} 
              className={activeTab === 'time' ? 'button-primary' : 'button-secondary'}
            >
              Time Projection
            </button>
          </div>

          {activeTab === 'location' && (
            <>
              <div className="form-row">
                <div className="form-group">
                  <label>Latitude:</label>
                  <input
                    type="number"
                    value={lat.toFixed(4)}
                    onChange={(e) => setLat(Number(e.target.value))}
                    className="form-control"
                    min="-90"
                    max="90"
                    step="0.0001"
                  />
                </div>
                
                <div className="form-group">
                  <label>Longitude:</label>
                  <input
                    type="number"
                    value={lon.toFixed(4)}
                    onChange={(e) => setLon(Number(e.target.value))}
                    className="form-control"
                    min="-180"
                    max="180"
                    step="0.0001"
                  />
                </div>
              </div>

              <div style={{ marginTop: '15px', marginBottom: '20px' }}>
                <button onClick={detectLocation} className="button-secondary">
                  Detect My Location
                </button>
              </div>

              <div className="form-group">
                <label>Reef Location Presets:</label>
                <div style={{ 
                  maxHeight: '300px', 
                  overflowY: 'auto',
                  marginTop: '10px',
                  padding: '10px',
                  background: '#1a1a1a',
                  borderRadius: '4px',
                  border: '1px solid #444'
                }}>
                  <div style={{ 
                    display: 'grid', 
                    gridTemplateColumns: 'repeat(auto-fit, minmax(250px, 1fr))',
                    gap: '8px'
                  }}>
                    {REEF_PRESETS.map((preset, index) => (
                      <button
                        key={index}
                        onClick={() => selectPreset(preset)}
                        className="preset-button"
                        style={{ 
                          fontSize: '13px',
                          padding: '8px 12px',
                          textAlign: 'left',
                          whiteSpace: 'nowrap',
                          overflow: 'hidden',
                          textOverflow: 'ellipsis',
                          background: (lat === preset.lat && lon === preset.lon) ? '#4a9eff' : '#444',
                          color: (lat === preset.lat && lon === preset.lon) ? '#fff' : '#e0e0e0'
                        }}
                        title={`${preset.name} (${preset.lat.toFixed(4)}, ${preset.lon.toFixed(4)})`}
                      >
                        {preset.name}
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            </>
          )}

          {activeTab === 'time' && (
            <>
              <div className="form-group">
                <label className="checkbox-label">
                  <input
                    type="checkbox"
                    checked={projection}
                    onChange={(e) => setProjection(e.target.checked)}
                  />
                  <span>Enable Astronomical Time Projection</span>
                </label>
                <p className="help-text">
                  When enabled, sunrise/sunset times from the location above will be 
                  projected to occur at the shifted time in your local timezone.
                </p>
              </div>

              {projection && (
                <div className="form-row">
                  <div className="form-group">
                    <label>Shift Hours:</label>
                    <input
                      type="number"
                      value={shiftHours}
                      onChange={(e) => setShiftHours(Number(e.target.value))}
                      className="form-control"
                      min="-12"
                      max="12"
                      step="1"
                    />
                  </div>
                  
                  <div className="form-group">
                    <label>Shift Minutes:</label>
                    <input
                      type="number"
                      value={shiftMinutes}
                      onChange={(e) => setShiftMinutes(Number(e.target.value))}
                      className="form-control"
                      min="-59"
                      max="59"
                      step="1"
                    />
                  </div>
                </div>
              )}
            </>
          )}
        </div>

        <div className="modal-footer">
          <button onClick={onClose} className="button-secondary">
            Cancel
          </button>
          <button 
            onClick={handleLocationSave} 
            className="button-primary"
            disabled={isSaving}
          >
            {isSaving ? 'Saving...' : 'Save Settings'}
          </button>
        </div>
      </div>
    </div>
  );
}