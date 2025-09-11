import { useState } from 'react';
import type { Schedule } from '../types';
import { api } from '../api/client';

interface JsonModalProps {
  isOpen: boolean;
  onClose: () => void;
  schedule: Schedule | null;
  onUpdate: () => void;
}

export function JsonModal({ isOpen, onClose, schedule, onUpdate }: JsonModalProps) {
  const [jsonData, setJsonData] = useState('');
  const [status, setStatus] = useState<{ message: string; type: 'success' | 'error' | '' }>({
    message: '',
    type: ''
  });

  if (!isOpen) return null;

  const handleExport = () => {
    if (schedule) {
      const formatted = JSON.stringify(schedule, null, 2);
      setJsonData(formatted);
      setStatus({ message: 'Schedule exported successfully', type: 'success' });
    } else {
      setStatus({ message: 'No schedule to export', type: 'error' });
    }
  };

  const handleImport = async () => {
    if (!jsonData.trim()) {
      setStatus({ message: 'Please enter JSON data', type: 'error' });
      return;
    }

    try {
      // Validate JSON
      const parsed = JSON.parse(jsonData);
      
      // Import the schedule
      await api.setSchedule(parsed);
      setStatus({ message: 'Schedule imported successfully', type: 'success' });
      
      // Refresh the UI
      setTimeout(() => {
        onUpdate();
        onClose();
      }, 1000);
    } catch (error) {
      if (error instanceof SyntaxError) {
        setStatus({ message: 'Invalid JSON format', type: 'error' });
      } else {
        setStatus({ 
          message: `Failed to import: ${(error as any).message || 'Unknown error'}`, 
          type: 'error' 
        });
      }
    }
  };

  const handleClear = async () => {
    if (!confirm('Are you sure you want to clear the entire schedule?')) return;
    
    try {
      await api.clearSchedule();
      setStatus({ message: 'Schedule cleared successfully', type: 'success' });
      setTimeout(() => {
        onUpdate();
        onClose();
      }, 1000);
    } catch (error) {
      setStatus({ 
        message: `Failed to clear schedule: ${(error as any).message || 'Unknown error'}`, 
        type: 'error' 
      });
    }
  };


  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-dialog modal-large" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>Import/Export Schedule</h2>
          <button className="modal-close" onClick={onClose}>×</button>
        </div>

        <div className="modal-body">
          {status.message && (
            <div className={`status-message ${status.type}`}>
              {status.message}
            </div>
          )}

          <div className="form-group">
            <label>Schedule JSON:</label>
            <textarea
              className="json-textarea"
              value={jsonData}
              onChange={(e) => setJsonData(e.target.value)}
              placeholder="Paste JSON here or click Export to view current schedule"
              rows={20}
            />
          </div>

          <div className="button-group">
            <button onClick={handleExport} className="button-primary">
              Export Current
            </button>
            <button onClick={handleImport} className="button-primary">
              Import JSON
            </button>
            <button onClick={handleClear} className="button-danger">
              Clear Schedule
            </button>
          </div>

        </div>

        <div className="modal-footer">
          <button onClick={onClose} className="button-secondary">
            Close
          </button>
        </div>
      </div>
    </div>
  );
}