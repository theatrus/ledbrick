import { useState, useEffect } from 'react';
import type { MoonSimulation, Schedule } from '../types';
import { api } from '../api/client';

interface MoonSettingsProps {
  moonSimulation: MoonSimulation | undefined;
  schedule: Schedule;
  onUpdate: () => void;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

export function MoonSettings({ moonSimulation, schedule, onUpdate }: MoonSettingsProps) {
  const numChannels = schedule.num_channels || 8;
  const [isInitialized, setIsInitialized] = useState(false);
  const [enabled, setEnabled] = useState(false);
  const [phaseScaling, setPhaseScaling] = useState(false);
  const [baseIntensity, setBaseIntensity] = useState<number[]>(new Array(numChannels).fill(0));
  const [baseCurrent, setBaseCurrent] = useState<number[]>(new Array(numChannels).fill(0));
  const [isSaving, setIsSaving] = useState(false);
  const [hasUnsavedChanges, setHasUnsavedChanges] = useState(false);

  // Initialize from props only once
  useEffect(() => {
    if (moonSimulation && !isInitialized) {
      setEnabled(moonSimulation.enabled);
      setPhaseScaling(moonSimulation.phase_scaling);
      
      // Ensure arrays have correct length
      const intensity = new Array(schedule.num_channels || 8).fill(0);
      const current = new Array(schedule.num_channels || 8).fill(0);
      
      // Copy values from moonSimulation if available
      if (moonSimulation.base_intensity) {
        moonSimulation.base_intensity.forEach((val, i) => {
          if (i < intensity.length) intensity[i] = val;
        });
      }
      if (moonSimulation.base_current) {
        moonSimulation.base_current.forEach((val, i) => {
          if (i < current.length) current[i] = val;
        });
      }
      
      setBaseIntensity(intensity);
      setBaseCurrent(current);
      setIsInitialized(true);
    }
  }, [moonSimulation, schedule.num_channels, isInitialized]);
  
  // Track changes
  useEffect(() => {
    if (!moonSimulation || !isInitialized) return;
    
    const changed = 
      enabled !== moonSimulation.enabled ||
      phaseScaling !== moonSimulation.phase_scaling ||
      baseIntensity.some((val, i) => 
        Math.abs(val - (moonSimulation.base_intensity?.[i] || 0)) > 0.01
      ) ||
      baseCurrent.some((val, i) => 
        Math.abs(val - (moonSimulation.base_current?.[i] || 0)) > 0.001
      );
    
    setHasUnsavedChanges(changed);
  }, [enabled, phaseScaling, baseIntensity, baseCurrent, moonSimulation, isInitialized]);

  const handlePwmChange = (channel: number, value: number) => {
    const newValues = [...baseIntensity];
    // Moon simulation PWM limited to 0-20%
    newValues[channel] = Math.max(0, Math.min(20, value));
    setBaseIntensity(newValues);
    setHasUnsavedChanges(true);
  };

  const handleCurrentChange = (channel: number, value: number) => {
    const maxCurrent = schedule.channel_configs?.[channel]?.max_current || 2.0;
    const newValues = [...baseCurrent];
    newValues[channel] = Math.max(0, Math.min(maxCurrent, value));
    setBaseCurrent(newValues);
    setHasUnsavedChanges(true);
  };

  const setAllChannels = (pwmValue: number) => {
    setBaseIntensity(new Array(numChannels).fill(pwmValue));
    // Set current proportionally based on max current
    const newCurrents = new Array(numChannels).fill(0).map((_, i) => {
      const maxCurrent = schedule.channel_configs?.[i]?.max_current || 2.0;
      return (pwmValue / 20) * maxCurrent; // Scale to 20% max for moon
    });
    setBaseCurrent(newCurrents);
    setHasUnsavedChanges(true);
  };

  const handleSave = async () => {
    setIsSaving(true);
    const moonData: MoonSimulation = {
      enabled,
      phase_scaling: phaseScaling,
      base_intensity: baseIntensity,
      base_current: baseCurrent
    };

    try {
      await api.setMoonSimulation(moonData);
      setHasUnsavedChanges(false);
      onUpdate();
    } catch (error) {
      console.error('Failed to save moon simulation:', error);
      alert('Failed to save moon simulation settings');
    } finally {
      setIsSaving(false);
    }
  };

  return (
    <div className="card">
      <h2>Moon Simulation Settings</h2>
      
      <div className="form-group">
        <label className="checkbox-label">
          <input
            type="checkbox"
            checked={enabled}
            onChange={(e) => {
              setEnabled(e.target.checked);
              setHasUnsavedChanges(true);
            }}
          />
          <span>Enable Moon Simulation</span>
        </label>
      </div>

      {enabled && (
        <>
          <div className="form-group">
            <label className="checkbox-label">
              <input
                type="checkbox"
                checked={phaseScaling}
                onChange={(e) => {
                  setPhaseScaling(e.target.checked);
                  setHasUnsavedChanges(true);
                }}
              />
              <span>Scale intensity with moon phase</span>
            </label>
          </div>

          <div className="preset-buttons">
            <button onClick={() => setAllChannels(0)} className="preset-button">
              All Off
            </button>
            <button onClick={() => setAllChannels(5)} className="preset-button">
              Dim (5%)
            </button>
            <button onClick={() => setAllChannels(10)} className="preset-button">
              Bright (10%)
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
                      value={(baseIntensity[i] || 0).toFixed(1)}
                      onChange={(e) => handlePwmChange(i, Number(e.target.value))}
                      className="control-input"
                      min="0"
                      max="20"
                      step="0.1"
                    />
                    <input
                      type="range"
                      value={baseIntensity[i] || 0}
                      onChange={(e) => handlePwmChange(i, Number(e.target.value))}
                      className="control-slider"
                      min="0"
                      max="20"
                      step="0.1"
                    />
                  </div>

                  <div className="control-group">
                    <label className="control-label">Current A</label>
                    <input
                      type="number"
                      value={(baseCurrent[i] || 0).toFixed(2)}
                      onChange={(e) => handleCurrentChange(i, Number(e.target.value))}
                      className="control-input"
                      min="0"
                      max={maxCurrent}
                      step="0.01"
                    />
                    <input
                      type="range"
                      value={baseCurrent[i] || 0}
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
        </>
      )}

      <div style={{ marginTop: '20px', textAlign: 'right' }}>
        <button 
          onClick={handleSave} 
          className={`button-primary ${hasUnsavedChanges ? 'has-changes' : ''}`}
          disabled={isSaving}
        >
          {isSaving ? 'Saving...' : `Save Moon Settings${hasUnsavedChanges ? ' *' : ''}`}
        </button>
      </div>
    </div>
  );
}