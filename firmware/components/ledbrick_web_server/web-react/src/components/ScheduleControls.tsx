import { useState, useEffect } from 'react';

interface ScheduleControlsProps {
  enabled: boolean;
  pwmScale: number;
  onToggleScheduler: () => void;
  onPWMScaleChange: (value: number) => void;
  onOpenJsonModal: () => void;
}

export function ScheduleControls({
  enabled,
  pwmScale,
  onToggleScheduler,
  onPWMScaleChange,
  onOpenJsonModal,
}: ScheduleControlsProps) {
  // Local state for PWM scale to prevent external updates while user is interacting
  const [localPwmScale, setLocalPwmScale] = useState(pwmScale);
  const [isAdjusting, setIsAdjusting] = useState(false);
  
  // Only update local state from props when not actively adjusting
  useEffect(() => {
    if (!isAdjusting) {
      setLocalPwmScale(pwmScale);
    }
  }, [pwmScale, isAdjusting]);
  
  const handlePwmChange = (value: number) => {
    setLocalPwmScale(value);
    onPWMScaleChange(value);
  };
  
  const handleMouseDown = () => {
    setIsAdjusting(true);
  };
  
  const handleMouseUp = () => {
    // Delay clearing the flag to allow the final value to be sent
    setTimeout(() => setIsAdjusting(false), 100);
  };
  return (
    <div className="card">
      <h2>Schedule Controls</h2>
      <div className="controls">
        <button className="control-button" onClick={onToggleScheduler}>
          <span className="icon">{enabled ? '⏸️' : '▶️'}</span>
          <span>{enabled ? 'Disable' : 'Enable'} Scheduler</span>
        </button>
        
        <div className="slider-control">
          <label htmlFor="pwmScale">Master Brightness:</label>
          <input
            type="range"
            id="pwmScale"
            min="0"
            max="100"
            value={localPwmScale}
            onChange={(e) => handlePwmChange(Number(e.target.value))}
            onMouseDown={handleMouseDown}
            onMouseUp={handleMouseUp}
            onTouchStart={handleMouseDown}
            onTouchEnd={handleMouseUp}
          />
          <span>{localPwmScale.toFixed(0)}%</span>
        </div>
        
        <button className="control-button" onClick={onOpenJsonModal}>
          <span className="icon">📋</span>
          <span>Import/Export</span>
        </button>
      </div>
    </div>
  );
}