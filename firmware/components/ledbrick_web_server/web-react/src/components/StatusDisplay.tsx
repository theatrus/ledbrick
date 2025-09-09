import { useState, useEffect } from 'react';
import { StatusBar } from './StatusBar';
import { api } from '../api/client';
import type { Status, Schedule } from '../types';

interface StatusDisplayProps {
  schedule: Schedule | null;
  initialStatus: Status | null;
  onUpdate: () => void;
}

// Separate component for status polling to prevent main UI re-renders
export function StatusDisplay({ schedule, initialStatus, onUpdate }: StatusDisplayProps) {
  const [status, setStatus] = useState<Status | null>(initialStatus);

  useEffect(() => {
    // Poll status every 5 seconds
    const interval = setInterval(async () => {
      try {
        const statusData = await api.getStatus();
        setStatus(statusData);
      } catch (err) {
        // Silent fail for status updates
      }
    }, 5000);
    
    return () => clearInterval(interval);
  }, []);

  // Update when initial status changes (e.g., after toggling scheduler)
  useEffect(() => {
    setStatus(initialStatus);
  }, [initialStatus]);

  return <StatusBar status={status} schedule={schedule} onUpdate={onUpdate} />;
}