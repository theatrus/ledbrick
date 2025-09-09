import { useState, useRef, useEffect } from 'react';
import { api } from '../api/client';

interface StatusBarControlsProps {
  enabled: boolean;
  pwmScale: number;
  onUpdate: () => void;
}

export function StatusBarControls({ enabled, pwmScale, onUpdate }: StatusBarControlsProps) {
  const [showPopout, setShowPopout] = useState(false);
  const [localEnabled, setLocalEnabled] = useState(enabled);
  const [localPwmScale, setLocalPwmScale] = useState(pwmScale);
  const [isSaving, setIsSaving] = useState(false);
  const popoutRef = useRef<HTMLDivElement>(null);

  // Update local state when props change
  useEffect(() => {
    setLocalEnabled(enabled);
    setLocalPwmScale(pwmScale);
  }, [enabled, pwmScale]);

  // Close popout when clicking outside
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (popoutRef.current && !popoutRef.current.contains(event.target as Node)) {
        setShowPopout(false);
      }
    };

    if (showPopout) {
      document.addEventListener('mousedown', handleClickOutside);
    }

    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
    };
  }, [showPopout]);

  const handleToggleScheduler = async () => {
    setIsSaving(true);
    try {
      await api.setSchedulerEnabled(!localEnabled);
      setLocalEnabled(!localEnabled);
      onUpdate();
    } catch (error) {
      console.error('Failed to toggle scheduler:', error);
      setLocalEnabled(enabled); // Reset on error
    } finally {
      setIsSaving(false);
    }
  };

  const handlePwmScaleChange = async (value: number) => {
    setLocalPwmScale(value);
    setIsSaving(true);
    try {
      await api.setPwmScale(value);
      onUpdate();
    } catch (error) {
      console.error('Failed to set PWM scale:', error);
      setLocalPwmScale(pwmScale); // Reset on error
    } finally {
      setIsSaving(false);
    }
  };

  return (
    <div style={{ position: 'relative' }}>
      <button
        onClick={() => setShowPopout(!showPopout)}
        style={{
          background: localEnabled ? '#4a9eff' : '#666',
          color: '#fff',
          border: 'none',
          padding: '6px 12px',
          borderRadius: '4px',
          cursor: 'pointer',
          fontSize: '13px',
          display: 'flex',
          alignItems: 'center',
          gap: '8px'
        }}
      >
        <span style={{ 
          width: '8px', 
          height: '8px', 
          borderRadius: '50%',
          background: localEnabled ? '#4ade80' : '#ef4444',
          display: 'inline-block'
        }} />
        Scheduler {localEnabled ? 'ON' : 'OFF'} • {Math.round(localPwmScale)}%
      </button>

      {showPopout && (
        <div
          ref={popoutRef}
          style={{
            position: 'absolute',
            bottom: '100%',
            right: 0,
            marginBottom: '10px',
            background: '#2a2a2a',
            border: '1px solid #444',
            borderRadius: '8px',
            padding: '20px',
            minWidth: '300px',
            boxShadow: '0 4px 20px rgba(0,0,0,0.5)',
            zIndex: 1000
          }}
        >
          <h3 style={{ margin: '0 0 15px 0', fontSize: '16px', color: '#4a9eff' }}>
            Scheduler Controls
          </h3>

          <div style={{ marginBottom: '20px' }}>
            <label style={{ display: 'flex', alignItems: 'center', gap: '10px', cursor: 'pointer' }}>
              <input
                type="checkbox"
                checked={localEnabled}
                onChange={handleToggleScheduler}
                disabled={isSaving}
                style={{ width: '18px', height: '18px', cursor: 'pointer' }}
              />
              <span>Scheduler Enabled</span>
            </label>
            <p style={{ margin: '5px 0 0 28px', fontSize: '12px', color: '#999' }}>
              {localEnabled ? 'Schedule is active and controlling lights' : 'Manual control only'}
            </p>
          </div>

          <div>
            <label style={{ display: 'block', marginBottom: '8px', fontSize: '14px' }}>
              Master Brightness: <strong>{Math.round(localPwmScale)}%</strong>
            </label>
            <input
              type="range"
              min="0"
              max="100"
              value={localPwmScale}
              onChange={(e) => handlePwmScaleChange(Number(e.target.value))}
              disabled={isSaving}
              style={{ width: '100%' }}
              className="control-slider"
            />
            <p style={{ margin: '5px 0 0 0', fontSize: '12px', color: '#999' }}>
              Scales all channel outputs by this percentage
            </p>
          </div>

          {isSaving && (
            <div style={{ 
              marginTop: '15px', 
              textAlign: 'center', 
              color: '#4a9eff',
              fontSize: '13px'
            }}>
              Saving...
            </div>
          )}
        </div>
      )}
    </div>
  );
}