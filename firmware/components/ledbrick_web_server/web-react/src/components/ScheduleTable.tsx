import { useState } from 'react';
import type { Schedule, SchedulePoint } from '../types';
import { SchedulePointEditor } from './SchedulePointEditor';
import { api } from '../api/client';

interface ScheduleTableProps {
  schedule: Schedule;
  onUpdate: () => void;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

// Convert a hex color to a good background tone by adjusting lightness and saturation
function colorToBackgroundTone(hexColor: string, opacity: number = 0.15): string {
  // Convert hex to RGB
  const hex = hexColor.replace('#', '');
  const r = parseInt(hex.substring(0, 2), 16);
  const g = parseInt(hex.substring(2, 4), 16);
  const b = parseInt(hex.substring(4, 6), 16);
  
  // Convert to HSL
  const rNorm = r / 255;
  const gNorm = g / 255;
  const bNorm = b / 255;
  
  const max = Math.max(rNorm, gNorm, bNorm);
  const min = Math.min(rNorm, gNorm, bNorm);
  const l = (max + min) / 2;
  
  let h = 0;
  let s = 0;
  
  if (max !== min) {
    const d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    
    switch (max) {
      case rNorm: h = ((gNorm - bNorm) / d + (gNorm < bNorm ? 6 : 0)) / 6; break;
      case gNorm: h = ((bNorm - rNorm) / d + 2) / 6; break;
      case bNorm: h = ((rNorm - gNorm) / d + 4) / 6; break;
    }
  }
  
  // Adjust for better background tone
  // Increase lightness for dark colors, decrease for light colors
  const targetLightness = 0.85; // Target a light background
  const adjustedL = l + (targetLightness - l) * 0.8;
  
  // Reduce saturation for better readability
  const adjustedS = s * 0.3;
  
  // Convert back to RGB
  const hue2rgb = (p: number, q: number, t: number) => {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1/6) return p + (q - p) * 6 * t;
    if (t < 1/2) return q;
    if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
    return p;
  };
  
  const q = adjustedL < 0.5 ? adjustedL * (1 + adjustedS) : adjustedL + adjustedS - adjustedL * adjustedS;
  const p = 2 * adjustedL - q;
  const rAdj = Math.round(hue2rgb(p, q, h + 1/3) * 255);
  const gAdj = Math.round(hue2rgb(p, q, h) * 255);
  const bAdj = Math.round(hue2rgb(p, q, h - 1/3) * 255);
  
  return `rgba(${rAdj}, ${gAdj}, ${bAdj}, ${opacity})`;
}

export function ScheduleTable({ schedule, onUpdate }: ScheduleTableProps) {
  const [isEditorOpen, setIsEditorOpen] = useState(false);
  const [editingPoint, setEditingPoint] = useState<SchedulePoint | undefined>();
  const [editingIndex, setEditingIndex] = useState<number | undefined>();
  const formatTime = (minutes: number) => {
    const hours = Math.floor(minutes / 60);
    const mins = minutes % 60;
    return `${hours.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}`;
  };

  const formatType = (point: SchedulePoint) => {
    if (!point.time_type || point.time_type === 'fixed') return 'Fixed';
    
    const typeMap: Record<string, string> = {
      'sunrise_relative': 'Sunrise',
      'solar_noon': 'Solar Noon',
      'sunset_relative': 'Sunset',
      'civil_dawn_relative': 'Civil Dawn',
      'civil_dusk_relative': 'Civil Dusk',
      'nautical_dawn_relative': 'Nautical Dawn',
      'nautical_dusk_relative': 'Nautical Dusk',
    };
    
    let text = typeMap[point.time_type] || point.time_type;
    if (point.offset_minutes && point.offset_minutes !== 0) {
      const sign = point.offset_minutes > 0 ? '+' : '';
      text += ` ${sign}${point.offset_minutes}m`;
    }
    return text;
  };

  const sortedPoints = [...schedule.schedule_points].sort(
    (a, b) => a.time_minutes - b.time_minutes
  );

  const handleEdit = (point: SchedulePoint, index: number) => {
    setEditingPoint(point);
    setEditingIndex(index);
    setIsEditorOpen(true);
  };

  const handleAdd = () => {
    setEditingPoint(undefined);
    setEditingIndex(undefined);
    setIsEditorOpen(true);
  };

  const handleSavePoint = async (point: SchedulePoint) => {
    const newSchedule = { ...schedule };
    
    if (editingIndex !== undefined) {
      // Update existing point
      const originalIndex = schedule.schedule_points.findIndex(
        (p, i) => sortedPoints[editingIndex] === p
      );
      newSchedule.schedule_points[originalIndex] = point;
    } else {
      // Add new point
      newSchedule.schedule_points.push(point);
    }

    try {
      await api.setSchedule(newSchedule);
      setIsEditorOpen(false);
      // Close modal first, then update to prevent re-render issues
      setTimeout(() => {
        onUpdate();
      }, 100);
    } catch (error) {
      console.error('Failed to save schedule point:', error);
      alert('Failed to save schedule point');
    }
  };

  const handleDelete = async (index: number) => {
    if (!confirm('Are you sure you want to delete this schedule point?')) {
      return;
    }

    const originalIndex = schedule.schedule_points.findIndex(
      (p, i) => sortedPoints[index] === p
    );

    const newSchedule = { ...schedule };
    newSchedule.schedule_points.splice(originalIndex, 1);

    try {
      await api.setSchedule(newSchedule);
      onUpdate();
    } catch (error) {
      console.error('Failed to delete schedule point:', error);
      alert('Failed to delete schedule point');
    }
  };

  return (
    <div className="card">
      <h2>Schedule Points</h2>
      <table className="schedule-table">
        <thead>
          <tr>
            <th>Time</th>
            <th>Type</th>
            {Array.from({ length: schedule.num_channels }, (_, i) => {
              const channelConfig = schedule.channel_configs?.[i];
              const channelName = channelConfig?.name || `Ch${i + 1}`;
              const channelColor = channelConfig?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length];
              return (
                <th 
                  key={i}
                  style={{
                    backgroundColor: colorToBackgroundTone(channelColor, 0.25),
                    borderBottom: `2px solid ${channelColor}40`
                  }}
                >
                  {channelName}
                </th>
              );
            })}
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {sortedPoints.map((point, index) => (
            <tr key={index}>
              <td>{formatTime(point.time_minutes)}</td>
              <td className={point.time_type && point.time_type !== 'fixed' ? 'astronomical-type' : ''}>
                {formatType(point)}
              </td>
              {(point.pwm_values || []).map((pwm, i) => {
                const channelConfig = schedule.channel_configs?.[i];
                const channelColor = channelConfig?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length];
                return (
                  <td 
                    key={i}
                    style={{
                      backgroundColor: colorToBackgroundTone(channelColor, 0.15),
                      whiteSpace: 'pre-line',
                      fontSize: '11px',
                      borderLeft: `2px solid ${channelColor}20`
                    }}
                  >
                    {pwm.toFixed(1)}%
                    {point.current_values && point.current_values[i] !== undefined && (
                      <>{'\n'}{point.current_values[i].toFixed(2)}A</>
                    )}
                  </td>
                );
              })}
              <td className="actions">
                <button onClick={() => handleEdit(point, index)}>Edit</button>
                <button onClick={() => handleDelete(index)}>Delete</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <div style={{ marginTop: '20px', textAlign: 'right' }}>
        <button onClick={handleAdd} className="button-primary">
          Add Schedule Point
        </button>
      </div>

      <SchedulePointEditor
        isOpen={isEditorOpen}
        onClose={() => setIsEditorOpen(false)}
        onSave={handleSavePoint}
        point={editingPoint}
        index={editingIndex}
        schedule={schedule}
      />
    </div>
  );
}