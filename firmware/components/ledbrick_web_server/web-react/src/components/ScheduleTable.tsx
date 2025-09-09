import { useState } from 'react';
import type { Schedule, SchedulePoint } from '../types';
import { SchedulePointEditor } from './SchedulePointEditor';
import { api } from '../api/client';

interface ScheduleTableProps {
  schedule: Schedule;
  onUpdate: () => void;
}

const channelColors = [
  '#FFFFFF', '#0000FF', '#00FFFF', '#00FF00',
  '#FF0000', '#FF00FF', '#FFFF00', '#FF8000'
];

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
            {Array.from({ length: 8 }, (_, i) => (
              <th key={i}>Ch{i + 1}</th>
            ))}
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
              {point.pwm_values.map((pwm, i) => (
                <td 
                  key={i}
                  style={{
                    backgroundColor: `${channelColors[i]}20`,
                    whiteSpace: 'pre-line',
                    fontSize: '11px',
                  }}
                >
                  {pwm.toFixed(1)}%
                  {point.current_values && point.current_values[i] !== undefined && (
                    <>{'\n'}{point.current_values[i].toFixed(2)}A</>
                  )}
                </td>
              ))}
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