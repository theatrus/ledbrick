import { useState, useEffect } from 'react';
import { api } from '../api/client';

interface LocationSettingsProps {
  latitude: number;
  longitude: number;
  astronomicalProjection: boolean;
  timeShiftHours: number;
  timeShiftMinutes: number;
  onUpdate: () => void;
}

export function LocationSettings({
  latitude,
  longitude,
  astronomicalProjection,
  timeShiftHours,
  timeShiftMinutes,
  onUpdate
}: LocationSettingsProps) {
  // Initialize state from props only once
  const [lat, setLat] = useState(latitude);
  const [lon, setLon] = useState(longitude);
  const [projection, setProjection] = useState(astronomicalProjection);
  const [shiftHours, setShiftHours] = useState(timeShiftHours);
  const [shiftMinutes, setShiftMinutes] = useState(timeShiftMinutes);
  const [isSaving, setIsSaving] = useState(false);
  const [hasUnsavedChanges, setHasUnsavedChanges] = useState(false);

  // Track if values differ from props
  useEffect(() => {
    const changed = 
      Math.abs(lat - latitude) > 0.0001 ||
      Math.abs(lon - longitude) > 0.0001 ||
      projection !== astronomicalProjection ||
      shiftHours !== timeShiftHours ||
      shiftMinutes !== timeShiftMinutes;
    setHasUnsavedChanges(changed);
  }, [lat, lon, projection, shiftHours, shiftMinutes, 
      latitude, longitude, astronomicalProjection, timeShiftHours, timeShiftMinutes]);

  const handleLocationSave = async () => {
    setIsSaving(true);
    try {
      await api.setLocation(lat, lon);
      onUpdate();
    } catch (error) {
      console.error('Failed to save location:', error);
      alert('Failed to save location settings');
    } finally {
      setIsSaving(false);
    }
  };

  const handleTimeShiftSave = async () => {
    setIsSaving(true);
    try {
      await api.setTimeShift(projection, shiftHours, shiftMinutes);
      onUpdate();
    } catch (error) {
      console.error('Failed to save time shift:', error);
      alert('Failed to save time shift settings');
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

  return (
    <>
      <div className="card">
        <h2>Location Settings</h2>
        
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

        <div style={{ marginTop: '15px', display: 'flex', gap: '10px' }}>
          <button onClick={detectLocation} className="button-secondary">
            Detect Location
          </button>
          <button 
            onClick={handleLocationSave} 
            className={`button-primary ${hasUnsavedChanges ? 'has-changes' : ''}`}
            disabled={isSaving}
          >
            Save Location {hasUnsavedChanges && '*'}
          </button>
        </div>

        <div className="location-presets">
          <p style={{ marginBottom: '10px', color: '#999' }}>Presets:</p>
          <div className="preset-buttons">
            <button 
              onClick={() => { setLat(37.4419); setLon(-122.1430); }} 
              className="preset-button"
            >
              Palo Alto, CA
            </button>
            <button 
              onClick={() => { setLat(33.4484); setLon(-112.0740); }} 
              className="preset-button"
            >
              Phoenix, AZ
            </button>
            <button 
              onClick={() => { setLat(1.3521); setLon(103.8198); }} 
              className="preset-button"
            >
              Singapore
            </button>
          </div>
        </div>
      </div>

      <div className="card">
        <h2>Time Projection Settings</h2>
        
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

        <div style={{ marginTop: '15px', textAlign: 'right' }}>
          <button 
            onClick={handleTimeShiftSave} 
            className={`button-primary ${hasUnsavedChanges ? 'has-changes' : ''}`}
            disabled={isSaving}
          >
            Save Time Settings {hasUnsavedChanges && '*'}
          </button>
        </div>
      </div>
    </>
  );
}