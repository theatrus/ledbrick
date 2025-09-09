import { useState } from 'react';
import { LocationModal } from './LocationModal';

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
  const [showModal, setShowModal] = useState(false);

  // Format time shift for display
  const formatTimeShift = () => {
    if (!astronomicalProjection) return 'Disabled';
    const sign = timeShiftHours >= 0 && timeShiftMinutes >= 0 ? '+' : '';
    return `${sign}${timeShiftHours}h ${Math.abs(timeShiftMinutes)}m`;
  };

  return (
    <>
      <div className="card">
        <h2>Location & Time Settings</h2>
        
        <div style={{ marginBottom: '20px' }}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px', marginBottom: '15px' }}>
            <div>
              <strong style={{ color: '#999' }}>Current Location:</strong>
              <div style={{ marginTop: '5px' }}>
                {latitude.toFixed(4)}°N, {longitude.toFixed(4)}°E
              </div>
            </div>
            <div>
              <strong style={{ color: '#999' }}>Time Projection:</strong>
              <div style={{ marginTop: '5px' }}>
                {formatTimeShift()}
              </div>
            </div>
          </div>
        </div>

        <button 
          onClick={() => setShowModal(true)} 
          className="button-primary"
          style={{ width: '100%' }}
        >
          Configure Location & Time
        </button>
      </div>

      <LocationModal
        isOpen={showModal}
        onClose={() => setShowModal(false)}
        latitude={latitude}
        longitude={longitude}
        astronomicalProjection={astronomicalProjection}
        timeShiftHours={timeShiftHours}
        timeShiftMinutes={timeShiftMinutes}
        onUpdate={onUpdate}
      />
    </>
  );
}