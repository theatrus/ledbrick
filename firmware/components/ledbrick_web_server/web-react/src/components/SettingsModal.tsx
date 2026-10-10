import { useState, useEffect } from 'react';
import { api } from '../api/client';
import type {
  Schedule,
  TemperatureConfig,
  FanCurve,
  ChannelDimming,
  ChannelDimmingUpdate,
  DimMode,
  DimPriority,
  LedGroup,
  LedModel,
} from '../types';
import { DEFAULT_CHANNEL_COLORS } from '../constants/colors';
import {
  MIN_FLOOR_CURRENT,
  MAX_FLOOR_CURRENT,
  MAX_LED_KINDS,
  MIN_LED_COUNT,
  MAX_LED_COUNT,
  DIM_MODE_LABELS,
  DIM_PRIORITY_LABELS,
} from '../constants/dimming';
import { formatLedMix, formatAmps, characterizedCurrent, ledMaxCurrent, sameLeds } from '../utils/dimming';
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

interface DimmingForm {
  mode: DimMode;
  priority: DimPriority;
  floor_current: string;  // as typed
  leds: LedGroup[];
  leds_default: boolean;
}

function dimmingToForm(dimming: ChannelDimming): DimmingForm {
  return {
    mode: dimming.mode === 'curve' ? 'curve' : 'manual',
    priority: dimming.priority === 'pwm' ? 'pwm' : 'current',
    floor_current: String(dimming.floor_current),
    leds: (dimming.leds || []).map(group => ({ ...group })),
    leds_default: !!dimming.leds_default,
  };
}

// Only the fields that differ from the saved settings, or null when none do
function dimmingUpdate(channel: number, form: DimmingForm | null, saved: ChannelDimming | null): ChannelDimmingUpdate | null {
  if (!form || !saved) return null;
  const update: ChannelDimmingUpdate = { channel };
  let changed = false;
  if (form.mode !== saved.mode) {
    update.mode = form.mode;
    changed = true;
  }
  if (form.priority !== saved.priority) {
    update.priority = form.priority;
    changed = true;
  }
  const floor = parseFloat(form.floor_current);
  if (!(Math.abs(floor - saved.floor_current) < 1e-4)) {
    update.floor_current = floor;
    changed = true;
  }
  if (form.leds_default) {
    if (!saved.leds_default) {
      update.leds_default = true;
      changed = true;
    }
  } else if (saved.leds_default || !sameLeds(form.leds, saved.leds)) {
    update.leds = form.leds.map(group => ({ model: group.model, count: group.count }));
    changed = true;
  }
  return changed ? update : null;
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

export function SettingsModal({ isOpen, onClose, schedule, onUpdate }: SettingsModalProps) {
  const [activeTab, setActiveTab] = useState<'channels' | 'location' | 'temperature'>('channels');
  const [channelConfigs, setChannelConfigs] = useState<ChannelConfigForm[]>([]);
  const [hasChanges, setHasChanges] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [localSchedule, setLocalSchedule] = useState<Schedule | null>(schedule);

  // Per-channel dimming; null for firmware without curve dimming
  const [configsChanged, setConfigsChanged] = useState(false);
  const [dimmingForms, setDimmingForms] = useState<(DimmingForm | null)[]>([]);
  const [savedDimming, setSavedDimming] = useState<(ChannelDimming | null)[]>([]);
  const [ledModels, setLedModels] = useState<LedModel[]>([]);
  const [editingLeds, setEditingLeds] = useState<number | null>(null);
  // Some dimming changes were saved but the schedule has not been reloaded yet
  const [needsReload, setNeedsReload] = useState(false);
  
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
          max_current: config?.max_current ?? 2.0
        });
      }
      setChannelConfigs(configs);

      const dimming = Array.from({ length: currentSchedule.num_channels }, (_, i) =>
        currentSchedule.channel_configs?.[i]?.dimming ?? null);
      setSavedDimming(dimming);
      setDimmingForms(dimming.map(d => (d ? dimmingToForm(d) : null)));
      setEditingLeds(null);
      setConfigsChanged(false);
      
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

  // LED model names for the dimming settings; without them the ids are shown
  useEffect(() => {
    const currentSchedule = schedule || localSchedule;
    if (!isOpen || ledModels.length > 0 || !currentSchedule?.channel_configs?.some(c => c?.dimming)) {
      return;
    }
    api.getLedModels()
      .then(models => setLedModels(models))
      .catch(() => { /* names fall back to ids */ });
  }, [isOpen, schedule, localSchedule]);

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
    setConfigsChanged(true);
    setHasChanges(true);
  };

  const updateDimming = (index: number, changes: Partial<DimmingForm>) => {
    setDimmingForms(forms => forms.map((form, i) => (i === index && form ? { ...form, ...changes } : form)));
    setHasChanges(true);
  };

  const updateLedRow = (index: number, row: number, changes: Partial<LedGroup>) => {
    const form = dimmingForms[index];
    if (!form) return;
    updateDimming(index, {
      leds: form.leds.map((group, i) => (i === row ? { ...group, ...changes } : group)),
      leds_default: false,
    });
  };

  const addLedRow = (index: number) => {
    const form = dimmingForms[index];
    if (!form || form.leds.length >= MAX_LED_KINDS || ledModels.length === 0) return;
    const unused = ledModels.find(m => !form.leds.some(group => group.model === m.id)) || ledModels[0];
    updateDimming(index, { leds: [...form.leds, { model: unused.id, count: 1 }], leds_default: false });
  };

  const removeLedRow = (index: number, row: number) => {
    const form = dimmingForms[index];
    if (!form) return;
    updateDimming(index, { leds: form.leds.filter((_, i) => i !== row), leds_default: false });
  };

  // The device lists the emitter's LEDs only while a channel uses them
  const resetLedsToDefault = (index: number) => {
    const saved = savedDimming[index];
    updateDimming(index, {
      leds: saved?.leds_default ? saved.leds.map(group => ({ ...group })) : [],
      leds_default: true,
    });
  };

  const channelName = (index: number) => channelConfigs[index]?.name || `Channel ${index + 1}`;

  const validateDimming = (update: ChannelDimmingUpdate): string | null => {
    const index = update.channel;
    const name = channelName(index);
    const form = dimmingForms[index];
    const saved = savedDimming[index];
    if (update.floor_current !== undefined) {
      const maxFloor = Math.min(MAX_FLOOR_CURRENT, channelConfigs[index]?.max_current ?? MAX_FLOOR_CURRENT);
      if (!(update.floor_current >= MIN_FLOOR_CURRENT && update.floor_current <= maxFloor + 1e-6)) {
        return `${name}: floor current must be ${MIN_FLOOR_CURRENT}-${maxFloor} A`;
      }
    }
    if (update.leds) {
      if (update.leds.length === 0) {
        return `${name}: add an LED or use the emitter default`;
      }
      if (update.leds.length > MAX_LED_KINDS) {
        return `${name}: at most ${MAX_LED_KINDS} kinds of LED`;
      }
      for (const group of update.leds) {
        if (ledModels.length > 0 && !ledModels.some(m => m.id === group.model)) {
          return `${name}: unknown LED model ${group.model}`;
        }
        if (!Number.isInteger(group.count) || group.count < MIN_LED_COUNT || group.count > MAX_LED_COUNT) {
          return `${name}: LED counts must be ${MIN_LED_COUNT}-${MAX_LED_COUNT}`;
        }
      }
    }
    if (form?.mode === 'curve' && form.leds_default && saved?.leds_default && saved.leds.length === 0) {
      return `${name}: LED curve needs the channel's LEDs`;
    }
    return null;
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

  const handleClose = () => {
    if (hasChanges) {
      const confirmClose = window.confirm(
        'You have unsaved changes. Are you sure you want to close without saving?'
      );
      if (!confirmClose) {
        return;
      }
    }
    onClose();
    if (needsReload) {
      setNeedsReload(false);
      onUpdate();
    }
  };

  const handleTabChange = (newTab: 'channels' | 'location' | 'temperature') => {
    if (hasChanges && newTab !== activeTab) {
      const confirmSwitch = window.confirm(
        'You have unsaved changes on this tab. Are you sure you want to switch tabs without saving?'
      );
      if (!confirmSwitch) {
        return;
      }
    }
    setActiveTab(newTab);
  };

  const handleSave = async () => {
    // Dimming changes are checked, and mode switches confirmed, before anything is sent
    const dimmingUpdates: ChannelDimmingUpdate[] = [];
    if (activeTab === 'channels') {
      for (let i = 0; i < dimmingForms.length; i++) {
        const update = dimmingUpdate(i, dimmingForms[i], savedDimming[i]);
        if (update) {
          const problem = validateDimming(update);
          if (problem) {
            setError(problem);
            return;
          }
          dimmingUpdates.push(update);
        }
      }
      const switches = dimmingUpdates.filter(update => update.mode !== undefined);
      if (switches.length > 0) {
        const lines = switches.map(update =>
          `${channelName(update.channel)}: ${DIM_MODE_LABELS[savedDimming[update.channel]?.mode || 'manual']}` +
          ` to ${DIM_MODE_LABELS[update.mode || 'manual']}`);
        if (!window.confirm(
          'Changing the dimming mode converts the schedule points and moonlight of:\n\n' +
          lines.join('\n') + '\n\nThey will give the same light. Continue?'
        )) {
          return;
        }
      }
    }

    setSaving(true);
    setError(null);
    
    try {
      if (activeTab === 'channels') {
        // Reload the schedule when the dialog closes, even after an error: a change can
        // apply on the device and fail only to save (500). Edits made from a stale copy
        // would post it back and undo the change.
        setNeedsReload(true);
        // Save channel configurations
        if (configsChanged || dimmingUpdates.length === 0) {
          await api.updateChannelConfigs(channelConfigs);
          setConfigsChanged(false);
        }
        // Then each changed channel's dimming. A mode change converts the
        // channel's schedule on the device, so the schedule is reloaded below.
        const markSaved = (update: ChannelDimmingUpdate) => {
          const form = dimmingForms[update.channel];
          if (form) {
            const nowSaved: ChannelDimming = {
              mode: form.mode,
              priority: form.priority,
              floor_current: parseFloat(form.floor_current),
              leds: form.leds.map(group => ({ ...group })),
              leds_default: form.leds_default,
            };
            setSavedDimming(saved => saved.map((d, i) => (i === update.channel ? nowSaved : d)));
          }
        };
        for (const update of dimmingUpdates) {
          try {
            await api.setChannelDimming(update);
          } catch (err: any) {
            if (err?.code === 500) {
              markSaved(update);  // applied on the device, not saved
            }
            throw { error: `${channelName(update.channel)}: ${err?.error || err?.message || 'failed to save dimming'}` };
          }
          markSaved(update);
        }
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
      setNeedsReload(false);
      await onUpdate();
      // Close modal after successful save
      onClose();
    } catch (err: any) {
      setError(err?.error || err?.message || 'Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  // Dimming settings in a channel's card
  const renderDimming = (index: number, maxCurrent: number) => {
    const form = dimmingForms[index];
    if (!form) return null;
    const saved = savedDimming[index];
    const lowest = characterizedCurrent(form.leds, ledModels);
    const ledLimit = ledMaxCurrent(form.leds, ledModels);

    return (
      <div className="dimming-section">
        <div className="dimming-title">Dimming</div>

        <div className="control-group">
          <label className="control-label">Mode</label>
          <select
            className="control-input"
            value={form.mode}
            onChange={(e) => updateDimming(index, { mode: e.target.value as DimMode })}
          >
            <option value="manual">{DIM_MODE_LABELS.manual}</option>
            <option value="curve">{DIM_MODE_LABELS.curve}</option>
          </select>
          {saved && form.mode !== saved.mode && (
            <div className="dimming-note">
              Saving converts this channel's schedule points and moonlight to give the same light.
            </div>
          )}
          {form.mode === 'curve' && (
            <div className="dimming-help">Points set a level: % of the light at max current.</div>
          )}
        </div>

        {form.mode === 'curve' && (
          <>
            <div className="control-group">
              <label className="control-label">Priority</label>
              <select
                className="control-input"
                value={form.priority}
                onChange={(e) => updateDimming(index, { priority: e.target.value as DimPriority })}
              >
                <option value="current">{DIM_PRIORITY_LABELS.current}</option>
                <option value="pwm">{DIM_PRIORITY_LABELS.pwm}</option>
              </select>
              <div className="dimming-help">
                {form.priority === 'current'
                  ? 'Lowers the current, then uses PWM below the floor current.'
                  : 'Holds the current and dims with PWM only.'}
              </div>
            </div>

            {form.priority === 'current' && (
              <div className="control-group">
                <label className="control-label">Floor current (A)</label>
                <input
                  type="number"
                  className="control-input"
                  value={form.floor_current}
                  onChange={(e) => updateDimming(index, { floor_current: e.target.value })}
                  min={MIN_FLOOR_CURRENT}
                  max={Math.min(MAX_FLOOR_CURRENT, maxCurrent)}
                  step="0.01"
                />
                {lowest !== null && (
                  <div className="dimming-help">
                    Never below {formatAmps(lowest)}, the lowest current the LED curves cover.
                  </div>
                )}
              </div>
            )}

            <div className="control-group">
              <label className="control-label">LEDs</label>
              <div className="led-mix">{formatLedMix(form, ledModels)}</div>
              {ledLimit !== null && ledLimit < maxCurrent && (
                <div className="dimming-help">Limited to {formatAmps(ledLimit)} by the LEDs' rating.</div>
              )}
              {editingLeds === index ? (
                <div className="led-editor">
                  {form.leds.map((group, row) => (
                    <div key={row} className="led-row">
                      <select
                        className="control-input"
                        value={group.model}
                        onChange={(e) => updateLedRow(index, row, { model: e.target.value })}
                      >
                        {!ledModels.some(m => m.id === group.model) && (
                          <option value={group.model}>{group.model}</option>
                        )}
                        {ledModels.map(model => (
                          <option key={model.id} value={model.id}>{model.name}</option>
                        ))}
                      </select>
                      <input
                        type="number"
                        className="control-input led-count"
                        aria-label="Count"
                        value={Number.isFinite(group.count) ? group.count : ''}
                        onChange={(e) => updateLedRow(index, row, { count: parseInt(e.target.value, 10) })}
                        min={MIN_LED_COUNT}
                        max={MAX_LED_COUNT}
                        step="1"
                      />
                      <button
                        type="button"
                        className="led-remove"
                        title="Remove"
                        onClick={() => removeLedRow(index, row)}
                        disabled={form.leds.length <= 1}
                      >
                        &times;
                      </button>
                    </div>
                  ))}
                  <div className="led-editor-actions">
                    <button
                      type="button"
                      className="small-button"
                      onClick={() => addLedRow(index)}
                      disabled={form.leds.length >= MAX_LED_KINDS || ledModels.length === 0}
                    >
                      Add LED
                    </button>
                    <button
                      type="button"
                      className="small-button"
                      onClick={() => resetLedsToDefault(index)}
                      disabled={form.leds_default}
                    >
                      Use emitter default
                    </button>
                    <button type="button" className="small-button" onClick={() => setEditingLeds(null)}>
                      Done
                    </button>
                  </div>
                </div>
              ) : (
                <button type="button" className="small-button" onClick={() => setEditingLeds(index)}>
                  Edit LEDs
                </button>
              )}
            </div>
          </>
        )}
      </div>
    );
  };

  return (
    <div className="modal-overlay" onClick={handleClose}>
      <div className="modal-dialog modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Settings</h2>
          <button className="modal-close" onClick={handleClose}>&times;</button>
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
              onClick={() => handleTabChange('channels')}
            >
              Channel Configuration
            </button>
            <button
              className={`preset-button ${activeTab === 'location' ? 'active' : ''}`}
              style={{ 
                background: activeTab === 'location' ? '#4a9eff' : '#444',
                color: activeTab === 'location' ? '#fff' : '#e0e0e0'
              }}
              onClick={() => handleTabChange('location')}
            >
              Location & Time
            </button>
            <button
              className={`preset-button ${activeTab === 'temperature' ? 'active' : ''}`}
              style={{ 
                background: activeTab === 'temperature' ? '#4a9eff' : '#444',
                color: activeTab === 'temperature' ? '#fff' : '#e0e0e0'
              }}
              onClick={() => handleTabChange('temperature')}
            >
              Temperature Control
            </button>
          </div>

          {error && (
            <div className="status-message error">{error}</div>
          )}

          {activeTab === 'channels' ? (
            <div className={`dual-channel-grid${dimmingForms.some(form => form) ? ' dimming-grid' : ''}`}>
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
                      max="2"
                      step="0.1"
                    />
                  </div>

                  {renderDimming(index, config.max_current)}
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
                        min="-12"
                        max="12"
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
                        labels: fanCurve.points.map(p => p.temperature?.toFixed(0) ?? '--'),
                        datasets: [
                          {
                            label: 'Fan PWM %',
                            data: fanCurve.points.map(p => p.fan_pwm ?? 0),
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
                                return `${context.parsed.y?.toFixed(1) ?? '--'}% at ${context.label}°C`;
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
          <button className="button-secondary" onClick={handleClose}>
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