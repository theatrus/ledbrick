import { useRef, useEffect, useState } from 'react';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  ChartOptions,
  TooltipItem,
} from 'chart.js';
import { Line } from 'react-chartjs-2';
import annotationPlugin from 'chartjs-plugin-annotation';
import type { Schedule, SchedulePoint } from '../types';

// Register Chart.js components
ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  annotationPlugin
);

interface ScheduleChartProps {
  schedule: Schedule;
  currentTime: number;
  moonriseTime?: string;
  moonsetTime?: string;
}

const DEFAULT_CHANNEL_COLORS = [
  '#FFFFFF', // White
  '#0000FF', // Blue  
  '#00FFFF', // Cyan
  '#00FF00', // Green
  '#FF0000', // Red
  '#FF00FF', // Magenta
  '#FFFF00', // Yellow
  '#FF8000'  // Orange
];

export function ScheduleChart({ schedule, currentTime, moonriseTime, moonsetTime }: ScheduleChartProps) {
  const chartRef = useRef<ChartJS<'line'>>(null);
  const [visibleChannels, setVisibleChannels] = useState<boolean[]>(
    new Array(schedule.num_channels).fill(true)
  );
  const [showCurrent, setShowCurrent] = useState(false);

  // Generate time labels for x-axis (every 15 minutes)
  const timeLabels = Array.from({ length: 96 }, (_, i) => {
    const minutes = i * 15;
    const hours = Math.floor(minutes / 60);
    const mins = minutes % 60;
    return `${hours.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}`;
  });

  // Interpolate schedule data for smooth chart
  const interpolateFullDay = (points: SchedulePoint[], useCurrentValues: boolean): number[][] => {
    if (!points || points.length === 0) {
      return Array(schedule.num_channels).fill([]).map(() => 
        Array(96).fill(0)
      );
    }

    // Sort points by time
    const sortedPoints = [...points].sort((a, b) => a.time_minutes - b.time_minutes);
    const interpolatedData: number[][] = Array(schedule.num_channels)
      .fill([])
      .map(() => []);

    // Generate data for every 15 minutes
    for (let i = 0; i < 96; i++) {
      const targetTime = i * 15;
      const values = interpolateAtTime(sortedPoints, targetTime);
      
      for (let ch = 0; ch < schedule.num_channels; ch++) {
        if (useCurrentValues) {
          // Current values are in amps, convert to mA
          const value = values.current_values && values.current_values[ch] !== undefined ? values.current_values[ch] * 1000 : 0;
          interpolatedData[ch].push(value);
        } else {
          const value = values.pwm_values && values.pwm_values[ch] !== undefined ? values.pwm_values[ch] : 0;
          interpolatedData[ch].push(value);
        }
      }
    }

    return interpolatedData;
  };

  // Interpolate values at a specific time
  const interpolateAtTime = (points: SchedulePoint[], targetTime: number): { pwm_values: number[], current_values: number[] } => {
    if (points.length === 0) {
      return { 
        pwm_values: new Array(schedule.num_channels).fill(0),
        current_values: new Array(schedule.num_channels).fill(0)
      };
    }

    if (points.length === 1) {
      return { 
        pwm_values: [...points[0].pwm_values],
        current_values: points[0].current_values ? [...points[0].current_values] : new Array(schedule.num_channels).fill(0)
      };
    }

    // Find surrounding points
    let prevPoint: SchedulePoint | null = null;
    let nextPoint: SchedulePoint | null = null;

    for (let i = 0; i < points.length; i++) {
      if (points[i].time_minutes <= targetTime) {
        prevPoint = points[i];
      }
      if (points[i].time_minutes > targetTime && !nextPoint) {
        nextPoint = points[i];
        break;
      }
    }

    // Handle wrap-around (last point of day to first point of next day)
    if (!nextPoint && prevPoint) {
      nextPoint = points[0];
    }
    if (!prevPoint && nextPoint) {
      prevPoint = points[points.length - 1];
    }

    if (!prevPoint || !nextPoint) {
      return { 
        pwm_values: new Array(schedule.num_channels).fill(0),
        current_values: new Array(schedule.num_channels).fill(0)
      };
    }

    // Calculate interpolation
    let timeDiff: number;
    let timeRatio: number;

    if (prevPoint.time_minutes <= targetTime && nextPoint.time_minutes > targetTime) {
      // Normal case
      timeDiff = nextPoint.time_minutes - prevPoint.time_minutes;
      timeRatio = (targetTime - prevPoint.time_minutes) / timeDiff;
    } else {
      // Wrap-around case
      if (targetTime < nextPoint.time_minutes) {
        // We're in the early morning part
        timeDiff = (1440 - prevPoint.time_minutes) + nextPoint.time_minutes;
        timeRatio = ((1440 - prevPoint.time_minutes) + targetTime) / timeDiff;
      } else {
        // We're in the late evening part
        timeDiff = (1440 - prevPoint.time_minutes) + nextPoint.time_minutes;
        timeRatio = (targetTime - prevPoint.time_minutes) / timeDiff;
      }
    }

    // Interpolate PWM and current values
    const pwm_values = new Array(schedule.num_channels);
    const current_values = new Array(schedule.num_channels);
    for (let i = 0; i < schedule.num_channels; i++) {
      const prevPwmValue = (prevPoint.pwm_values && prevPoint.pwm_values[i] !== undefined) ? prevPoint.pwm_values[i] : 0;
      const nextPwmValue = (nextPoint.pwm_values && nextPoint.pwm_values[i] !== undefined) ? nextPoint.pwm_values[i] : 0;
      pwm_values[i] = prevPwmValue + (nextPwmValue - prevPwmValue) * timeRatio;
      
      const prevCurrentValue = (prevPoint.current_values && prevPoint.current_values[i] !== undefined) ? prevPoint.current_values[i] : 0;
      const nextCurrentValue = (nextPoint.current_values && nextPoint.current_values[i] !== undefined) ? nextPoint.current_values[i] : 0;
      current_values[i] = prevCurrentValue + (nextCurrentValue - prevCurrentValue) * timeRatio;
    }

    return { pwm_values, current_values };
  };

  // Process schedule data - filter out astronomical points with time_minutes=0
  const validPoints = schedule.schedule_points.filter(point => {
    // Keep all fixed points and astronomical points with valid times
    return point.time_type === 'fixed' || point.time_minutes > 0;
  });
  const interpolatedData = interpolateFullDay(validPoints, showCurrent);
  
  // Create datasets for each channel
  const datasets = Array.from({ length: schedule.num_channels }, (_, i) => {
    const channelConfig = schedule.channel_configs?.[i];
    const color = channelConfig?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length];
    const name = channelConfig?.name || `Channel ${i + 1}`;

    return {
      label: name,
      data: interpolatedData[i],
      borderColor: color,
      backgroundColor: color + '20', // 20% opacity
      borderWidth: 2,
      tension: 0.4,
      pointRadius: 0,
      pointHoverRadius: 5,
      hidden: !visibleChannels[i]
    };
  });

  // Calculate current time position for annotation
  const currentTimeIndex = Math.floor(currentTime / 15);
  const currentTimeRemainder = (currentTime % 15) / 15;
  const currentTimePosition = currentTimeIndex + currentTimeRemainder;

  // Convert time string (HH:MM) to minutes and chart position
  const timeStringToPosition = (timeStr?: string): number | null => {
    if (!timeStr) return null;
    const [hours, minutes] = timeStr.split(':').map(Number);
    if (isNaN(hours) || isNaN(minutes)) return null;
    const totalMinutes = hours * 60 + minutes;
    return totalMinutes / 15; // Convert to chart index position
  };

  const moonrisePosition = timeStringToPosition(moonriseTime);
  const moonsetPosition = timeStringToPosition(moonsetTime);

  // Calculate Y-axis max for current mode
  // Need to check actual current values in schedule, not just max_current config
  let yAxisMax = 115; // Default for PWM mode
  
  if (showCurrent) {
    // Find the maximum current value across all schedule points
    let maxScheduledCurrent = 0;
    for (const point of schedule.schedule_points) {
      if (point.current_values) {
        const pointMax = Math.max(...point.current_values);
        maxScheduledCurrent = Math.max(maxScheduledCurrent, pointMax);
      }
    }
    // Convert from amps to mA and add 15% padding
    yAxisMax = Math.max(maxScheduledCurrent * 1000 * 1.15, 1000); // At least 1000mA
  }

  // Chart options
  const options: ChartOptions<'line'> = {
    responsive: true,
    maintainAspectRatio: false,
    layout: {
      padding: {
        top: 10,
        right: 10,
        bottom: 10,
        left: 10
      }
    },
    interaction: {
      mode: 'index',
      intersect: false,
    },
    plugins: {
      legend: {
        display: false // We'll use custom legend
      },
      tooltip: {
        mode: 'index',
        intersect: false,
        callbacks: {
          title: (tooltipItems: TooltipItem<'line'>[]) => `Time: ${tooltipItems[0].label}`,
          label: (context: TooltipItem<'line'>) => 
            `${context.dataset.label}: ${context.parsed.y.toFixed(1)}${showCurrent ? 'mA' : '%'}`
        }
      },
      annotation: {
        annotations: {
          currentTimeLine: {
            type: 'line',
            scaleID: 'x',
            value: currentTimePosition,
            borderColor: 'rgba(255, 0, 0, 0.8)',
            borderWidth: 3,
            borderDash: [5, 5],
            label: {
              display: true,
              content: 'Now',
              position: 'start',
              backgroundColor: 'rgba(255, 0, 0, 0.8)',
              color: 'white',
              font: { size: 12 },
              yAdjust: -5
            }
          },
          ...(moonrisePosition !== null && {
            moonriseLine: {
              type: 'line',
              scaleID: 'x',
              value: moonrisePosition,
              borderColor: 'rgba(255, 255, 0, 0.6)',
              borderWidth: 2,
              borderDash: [8, 4],
              label: {
                display: true,
                content: '🌙↑',
                position: 'start',
                backgroundColor: 'rgba(255, 255, 0, 0.8)',
                color: 'black',
                font: { size: 14 },
                yAdjust: -5,
                xAdjust: -10
              }
            }
          }),
          ...(moonsetPosition !== null && {
            moonsetLine: {
              type: 'line',
              scaleID: 'x',
              value: moonsetPosition,
              borderColor: 'rgba(100, 100, 255, 0.6)',
              borderWidth: 2,
              borderDash: [8, 4],
              label: {
                display: true,
                content: '🌙↓',
                position: 'start',
                backgroundColor: 'rgba(100, 100, 255, 0.8)',
                color: 'white',
                font: { size: 14 },
                yAdjust: -5,
                xAdjust: -10
              }
            }
          })
        }
      }
    },
    scales: {
      x: {
        title: {
          display: true,
          text: 'Time (24hr)'
        },
        grid: {
          display: true,
          color: 'rgba(255, 255, 255, 0.1)'
        }
      },
      y: {
        title: {
          display: true,
          text: showCurrent ? 'Current (mA)' : 'PWM (%)'
        },
        min: 0,
        max: yAxisMax,
        ticks: {
          stepSize: showCurrent ? 100 : 10,
          callback: function(value) {
            if (!showCurrent) {
              // Only show tick labels up to 100%
              return value <= 100 ? value : '';
            }
            return value;
          }
        },
        grid: {
          display: true,
          color: 'rgba(255, 255, 255, 0.1)'
        }
      }
    }
  };

  const toggleChannel = (index: number) => {
    const newVisible = [...visibleChannels];
    newVisible[index] = !newVisible[index];
    setVisibleChannels(newVisible);
  };

  // Force chart update when switching modes
  useEffect(() => {
    if (chartRef.current) {
      chartRef.current.update();
    }
  }, [showCurrent]);

  return (
    <div className="card">
      <h2>Schedule Visualization</h2>
      <div className="chart-controls">
        <button 
          className={!showCurrent ? 'active' : ''}
          onClick={() => setShowCurrent(false)}
        >
          PWM %
        </button>
        <button 
          className={showCurrent ? 'active' : ''}
          onClick={() => setShowCurrent(true)}
        >
          Current (mA)
        </button>
      </div>
      <div className="chart-container">
        <Line
          ref={chartRef}
          data={{ labels: timeLabels, datasets }}
          options={options}
          key={showCurrent ? 'current' : 'pwm'}
        />
      </div>
      <div className="legend">
        {Array.from({ length: schedule.num_channels }, (_, i) => {
          const channelConfig = schedule.channel_configs?.[i];
          const color = channelConfig?.rgb_hex || DEFAULT_CHANNEL_COLORS[i % DEFAULT_CHANNEL_COLORS.length];
          const name = channelConfig?.name || `Channel ${i + 1}`;

          return (
            <div key={i} className="legend-item">
              <input
                type="checkbox"
                checked={visibleChannels[i]}
                onChange={() => toggleChannel(i)}
              />
              <div 
                className="legend-color" 
                style={{ backgroundColor: color }}
              />
              <span>{name}</span>
            </div>
          );
        })}
        {(moonriseTime || moonsetTime) && (
          <div className="legend-separator">
            {moonriseTime && (
              <div className="legend-item moon-legend">
                <span className="moon-icon">🌙↑</span>
                <span>Moonrise {moonriseTime}</span>
              </div>
            )}
            {moonsetTime && (
              <div className="legend-item moon-legend">
                <span className="moon-icon">🌙↓</span>
                <span>Moonset {moonsetTime}</span>
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}