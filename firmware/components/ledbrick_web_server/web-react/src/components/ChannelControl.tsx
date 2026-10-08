import { useState } from 'react';
import { api } from '../api/client';
import type { DimMode } from '../types';

interface ChannelControlProps {
  channel: number;
  mode?: DimMode;          // curve: set a level instead of PWM and current
  currentLevel?: number;   // curve: 0-100
  currentPwm: number;
  currentAmperage: number;
  maxCurrent: number;
  channelName: string;
  channelColor: string;
  onClose: () => void;
  onUpdate: () => void;
}

export function ChannelControl({
  channel,
  mode = 'manual',
  currentLevel,
  currentPwm,
  currentAmperage,
  maxCurrent,
  channelName,
  channelColor,
  onClose
}: ChannelControlProps) {
  const curve = mode === 'curve';
  const [level, setLevel] = useState(currentLevel ?? 0);
  const [pwm, setPwm] = useState(currentPwm);
  const [current, setCurrent] = useState(currentAmperage);
  const [isSaving, setIsSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleSave = async () => {
    setIsSaving(true);
    setError(null);
    
    try {
      if (curve) {
        await api.setChannelManualLevel(channel, level);
      } else {
        await api.setChannelManualControl(channel, pwm, current);
      }
      // Don't call onUpdate - let the status polling handle it
      onClose();
    } catch (err: any) {
      setError(err.error || 'Failed to update channel');
    } finally {
      setIsSaving(false);
    }
  };

  return (
    <div style={{
      position: 'fixed',
      top: '50%',
      left: '50%',
      transform: 'translate(-50%, -50%)',
      background: '#2a2a2a',
      border: '1px solid #444',
      borderRadius: '8px',
      padding: '20px',
      minWidth: '350px',
      boxShadow: '0 4px 20px rgba(0,0,0,0.5)',
      zIndex: 1001
    }}>
      <h3 style={{ 
        margin: '0 0 20px 0', 
        color: channelColor,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between'
      }}>
        {channelName} Manual Control
        <button 
          onClick={onClose}
          style={{
            background: 'transparent',
            border: 'none',
            color: '#999',
            fontSize: '24px',
            cursor: 'pointer',
            padding: '0',
            width: '30px',
            height: '30px',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center'
          }}
        >
          ×
        </button>
      </h3>

      {error && (
        <div style={{
          background: '#ff4444',
          color: '#fff',
          padding: '10px',
          borderRadius: '4px',
          marginBottom: '20px',
          fontSize: '14px'
        }}>
          {error}
        </div>
      )}

      {curve ? (
        <div style={{ marginBottom: '20px' }}>
          <label style={{ display: 'block', marginBottom: '8px', fontSize: '14px' }}>
            Level: <strong>{level.toFixed(1)}%</strong>
            <span style={{ color: '#999', marginLeft: '10px' }}>
              (of the light at max current)
            </span>
          </label>
          <div style={{ display: 'flex', gap: '10px', alignItems: 'center' }}>
            <input
              type="range"
              min="0"
              max="100"
              step="0.1"
              value={level}
              onChange={(e) => setLevel(Number(e.target.value))}
              disabled={isSaving}
              style={{ flex: 1 }}
              className="control-slider"
            />
            <input
              type="number"
              min="0"
              max="100"
              step="0.1"
              value={level}
              onChange={(e) => setLevel(Math.max(0, Math.min(100, Number(e.target.value) || 0)))}
              disabled={isSaving}
              style={{ width: '70px', marginBottom: 0 }}
              className="control-input"
            />
          </div>
          <div style={{ marginTop: '8px', fontSize: '12px', color: '#999' }}>
            Now: PWM {currentPwm.toFixed(1)}%, {currentAmperage.toFixed(2)}A (LED curve)
          </div>
        </div>
      ) : (
        <>
          <div style={{ marginBottom: '20px' }}>
            <label style={{ display: 'block', marginBottom: '8px', fontSize: '14px' }}>
              Brightness: <strong>{pwm.toFixed(0)}%</strong>
            </label>
            <input
              type="range"
              min="0"
              max="100"
              step="1"
              value={pwm}
              onChange={(e) => setPwm(Number(e.target.value))}
              disabled={isSaving}
              style={{ width: '100%' }}
              className="control-slider"
            />
          </div>

          <div style={{ marginBottom: '20px' }}>
            <label style={{ display: 'block', marginBottom: '8px', fontSize: '14px' }}>
              Current: <strong>{current.toFixed(2)}A</strong>
              <span style={{ color: '#999', marginLeft: '10px' }}>
                (max: {maxCurrent.toFixed(1)}A)
              </span>
            </label>
            <input
              type="range"
              min="0"
              max={maxCurrent}
              step="0.05"
              value={current}
              onChange={(e) => setCurrent(Number(e.target.value))}
              disabled={isSaving}
              style={{ width: '100%' }}
              className="control-slider"
            />
          </div>
        </>
      )}

      <div style={{ 
        display: 'flex', 
        gap: '10px',
        justifyContent: 'flex-end',
        marginTop: '30px'
      }}>
        <button
          onClick={onClose}
          disabled={isSaving}
          style={{
            padding: '8px 16px',
            background: '#444',
            color: '#fff',
            border: 'none',
            borderRadius: '4px',
            cursor: 'pointer',
            fontSize: '14px'
          }}
        >
          Cancel
        </button>
        <button
          onClick={handleSave}
          disabled={isSaving}
          style={{
            padding: '8px 16px',
            background: '#4a9eff',
            color: '#fff',
            border: 'none',
            borderRadius: '4px',
            cursor: 'pointer',
            fontSize: '14px'
          }}
        >
          {isSaving ? 'Saving...' : 'Apply'}
        </button>
      </div>
    </div>
  );
}