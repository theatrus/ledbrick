import React, { useState, useEffect } from 'react';
import { api } from '../api/client';
import { TemperatureConfig, FanCurve } from '../types';
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

interface TemperatureControlModalProps {
  isOpen: boolean;
  onClose: () => void;
}

export function TemperatureControlModal({ isOpen, onClose }: TemperatureControlModalProps) {
  const [config, setConfig] = useState<TemperatureConfig | null>(null);
  const [fanCurve, setFanCurve] = useState<FanCurve | null>(null);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [hasChanges, setHasChanges] = useState(false);

  useEffect(() => {
    if (isOpen) {
      loadConfig();
      loadFanCurve();
    }
  }, [isOpen]);

  const loadConfig = async () => {
    setLoading(true);
    setError(null);
    try {
      const data = await api.getTemperatureConfig();
      if (data) {
        setConfig(data);
      } else {
        setError('Temperature control not available');
      }
    } catch (err: any) {
      setError('Failed to load configuration: ' + err.message);
    } finally {
      setLoading(false);
    }
  };

  const loadFanCurve = async () => {
    try {
      const data = await api.getFanCurve();
      if (data !== null) {
        setFanCurve(data);
      }
      // If data is null, temperature control is not available
      // Don't try to reload - this prevents rapid refresh loop
    } catch (err) {
      console.error('Failed to load fan curve:', err);
      // Don't set error state here to avoid refresh loops
    }
  };

  const handleSave = async () => {
    if (!config) return;
    
    setSaving(true);
    setError(null);
    try {
      await api.updateTemperatureConfig(config);
      setHasChanges(false);
      // Reload fan curve to reflect new settings
      loadFanCurve();
    } catch (err: any) {
      setError('Failed to save configuration: ' + err.message);
    } finally {
      setSaving(false);
    }
  };

  const updateConfig = (field: keyof TemperatureConfig, value: number) => {
    if (!config) return;
    setConfig({ ...config, [field]: value });
    setHasChanges(true);
  };

  if (!isOpen) return null;

  const chartData = fanCurve ? {
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
  } : null;

  const chartOptions = {
    responsive: true,
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
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-dialog modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Temperature Control Configuration</h2>
          <button className="modal-close" onClick={onClose}>&times;</button>
        </div>

        <div className="modal-body">
          {error && (
            <div className="status-message error">{error}</div>
          )}

          {loading ? (
            <div>Loading configuration...</div>
          ) : config ? (
            <>
              <div className="form-row">
                <div className="form-group">
                  <label>Target Temperature (°C)</label>
                  <input
                    type="number"
                    className="form-control"
                    value={config.target_temp_c}
                    onChange={(e) => updateConfig('target_temp_c', parseFloat(e.target.value))}
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
                    value={config.emergency_temp_c}
                    onChange={(e) => updateConfig('emergency_temp_c', parseFloat(e.target.value))}
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
                    value={config.recovery_temp_c}
                    onChange={(e) => updateConfig('recovery_temp_c', parseFloat(e.target.value))}
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
                    value={config.kp}
                    onChange={(e) => updateConfig('kp', parseFloat(e.target.value))}
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
                    value={config.ki}
                    onChange={(e) => updateConfig('ki', parseFloat(e.target.value))}
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
                    value={config.kd}
                    onChange={(e) => updateConfig('kd', parseFloat(e.target.value))}
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
                    value={config.min_fan_pwm}
                    onChange={(e) => updateConfig('min_fan_pwm', parseFloat(e.target.value))}
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
                    value={config.max_fan_pwm}
                    onChange={(e) => updateConfig('max_fan_pwm', parseFloat(e.target.value))}
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
                    value={config.fan_update_interval_ms}
                    onChange={(e) => updateConfig('fan_update_interval_ms', parseInt(e.target.value))}
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
                    value={config.emergency_delay_ms}
                    onChange={(e) => updateConfig('emergency_delay_ms', parseInt(e.target.value))}
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
                    value={config.sensor_timeout_ms}
                    onChange={(e) => updateConfig('sensor_timeout_ms', parseInt(e.target.value))}
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
                    value={config.temp_filter_alpha}
                    onChange={(e) => updateConfig('temp_filter_alpha', parseFloat(e.target.value))}
                    step="0.05"
                    min="0"
                    max="1"
                  />
                  <div className="help-text">
                    Low-pass filter coefficient (0-1, higher = less filtering)
                  </div>
                </div>
              </div>

              {chartData && (
                <div className="chart-container" style={{ height: '300px', marginTop: '20px' }}>
                  <Line data={chartData} options={chartOptions} />
                </div>
              )}
            </>
          ) : (
            <div>Temperature control not available</div>
          )}
        </div>

        <div className="modal-footer">
          <button className="button-secondary" onClick={onClose}>
            Close
          </button>
          {config && (
            <button 
              className={`button-primary ${hasChanges ? 'has-changes' : ''}`}
              onClick={handleSave}
              disabled={saving}
            >
              {saving ? 'Saving...' : 'Save Configuration'}
            </button>
          )}
        </div>
      </div>
    </div>
  );
}