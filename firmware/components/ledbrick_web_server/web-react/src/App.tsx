import { useState, useEffect, useCallback } from 'react';
import { api } from './api/client';
import { Header } from './components/Header';
import { ScheduleControls } from './components/ScheduleControls';
import { ScheduleTable } from './components/ScheduleTable';
import { ScheduleChart } from './components/ScheduleChart';
import { StatusDisplay } from './components/StatusDisplay';
import { MoonSettings } from './components/MoonSettings';
import { LocationSettings } from './components/LocationSettings';
import { JsonModal } from './components/JsonModal';
import { ScheduleDebug } from './components/ScheduleDebug';
import { SettingsModal } from './components/SettingsModal';
import type { Schedule, Status } from './types';

export function App() {
  const [schedule, setSchedule] = useState<Schedule | null>(null);
  const [status, setStatus] = useState<Status | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [isJsonModalOpen, setIsJsonModalOpen] = useState(false);
  const [isDebugModalOpen, setIsDebugModalOpen] = useState(false);
  const [isSettingsModalOpen, setIsSettingsModalOpen] = useState(false);

  // Load schedule data (only when needed)
  const loadSchedule = useCallback(async () => {
    try {
      const scheduleData = await api.getSchedule();
      setSchedule(scheduleData);
      return scheduleData;
    } catch (err: any) {
      setError(err.message || 'Failed to load schedule');
      return null;
    }
  }, []);

  // Load status (for polling)
  const loadStatus = useCallback(async () => {
    try {
      const statusData = await api.getStatus();
      setStatus(statusData);
      return statusData;
    } catch (err: any) {
      // Silent fail for status updates during polling
      return null;
    }
  }, []);

  // Initial load
  const loadData = useCallback(async () => {
    setLoading(true);
    try {
      const [scheduleData, statusData] = await Promise.all([
        loadSchedule(),
        loadStatus(),
      ]);
      setError(null);
    } catch (err: any) {
      setError(err.message || 'Failed to load data');
    } finally {
      setLoading(false);
    }
  }, [loadSchedule, loadStatus]);

  useEffect(() => {
    loadData();
  }, []);

  // Status polling - separate from main data loading
  useEffect(() => {
    const interval = setInterval(() => {
      loadStatus();
    }, 5000);
    return () => clearInterval(interval);
  }, [loadStatus]);

  const handleSchedulerToggle = async () => {
    if (!status) return;
    try {
      await api.setSchedulerEnabled(!status.enabled);
      await loadData();
    } catch (err: any) {
      setError(err.message || 'Failed to toggle scheduler');
    }
  };

  const handlePWMScaleChange = async (value: number) => {
    try {
      await api.setPWMScale(value);
      // Update local state immediately for responsiveness
      if (status) {
        setStatus({ ...status, pwm_scale: value / 100 });
      }
    } catch (err: any) {
      setError(err.message || 'Failed to set brightness');
    }
  };

  if (loading) {
    return <div className="loading">Loading...</div>;
  }

  return (
    <div className="app">
      <Header 
        onOpenJsonModal={() => setIsJsonModalOpen(true)}
        onOpenDebugModal={() => setIsDebugModalOpen(true)}
        onOpenSettingsModal={() => setIsSettingsModalOpen(true)}
      />
      
      <div className="container">

        {error && (
          <div className="error-banner">
            {error}
            <button onClick={() => setError(null)}>×</button>
          </div>
        )}

        {schedule && (
          <>
            <ScheduleChart schedule={schedule} currentTime={status?.time_minutes || 0} />
            <ScheduleTable schedule={schedule} onUpdate={loadData} />
            <MoonSettings 
              moonSimulation={status?.moon_simulation}
              schedule={schedule}
              onUpdate={loadData}
            />
          </>
        )}

        <StatusDisplay schedule={schedule} initialStatus={status} onUpdate={loadData} />
        
        <JsonModal
          isOpen={isJsonModalOpen}
          onClose={() => setIsJsonModalOpen(false)}
          schedule={schedule}
          onUpdate={loadData}
        />
        
        {isDebugModalOpen && (
          <ScheduleDebug onClose={() => setIsDebugModalOpen(false)} />
        )}
        
        <SettingsModal
          isOpen={isSettingsModalOpen}
          onClose={() => setIsSettingsModalOpen(false)}
          schedule={schedule}
          onUpdate={loadData}
        />
      </div>
    </div>
  );
}