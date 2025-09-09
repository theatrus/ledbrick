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

export function ScheduleChart({ schedule, currentTime }: ScheduleChartProps) {
  const chartRef = useRef<ChartJS<'line'>>(null);
  const [visibleChannels, setVisibleChannels] = useState<boolean[]>(
    new Array(schedule.num_channels).fill(true)
  );

  // Generate time labels for x-axis (every 15 minutes)
  const timeLabels = Array.from({ length: 96 }, (_, i) => {
    const minutes = i * 15;
    const hours = Math.floor(minutes / 60);
    const mins = minutes % 60;
    return `${hours.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}`;
  });

  // Interpolate schedule data for smooth chart
  const interpolateFullDay = (points: SchedulePoint[]): number[][] => {
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
        interpolatedData[ch].push(values.pwm_values[ch]);
      }
    }

    return interpolatedData;
  };

  // Interpolate values at a specific time
  const interpolateAtTime = (points: SchedulePoint[], targetTime: number): { pwm_values: number[] } => {
    if (points.length === 0) {
      return { pwm_values: new Array(schedule.num_channels).fill(0) };
    }

    if (points.length === 1) {
      return { pwm_values: [...points[0].pwm_values] };
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
      return { pwm_values: new Array(schedule.num_channels).fill(0) };
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

    // Interpolate PWM values
    const pwm_values = new Array(schedule.num_channels);
    for (let i = 0; i < schedule.num_channels; i++) {
      const prevValue = prevPoint.pwm_values[i] || 0;
      const nextValue = nextPoint.pwm_values[i] || 0;
      pwm_values[i] = prevValue + (nextValue - prevValue) * timeRatio;
    }

    return { pwm_values };
  };

  // Process schedule data - filter out astronomical points with time_minutes=0
  const validPoints = schedule.schedule_points.filter(point => {
    // Keep all fixed points and astronomical points with valid times
    return point.time_type === 'fixed' || point.time_minutes > 0;
  });
  const interpolatedData = interpolateFullDay(validPoints);
  
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

  // Chart options
  const options: ChartOptions<'line'> = {
    responsive: true,
    maintainAspectRatio: false,
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
            `${context.dataset.label}: ${context.parsed.y.toFixed(1)}%`
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
              yAdjust: -10
            }
          }
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
          text: 'PWM (%)'
        },
        min: 0,
        max: 100,
        ticks: {
          stepSize: 10
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

  return (
    <div className="card">
      <h2>Schedule Visualization</h2>
      <div className="chart-container">
        <Line
          ref={chartRef}
          data={{ labels: timeLabels, datasets }}
          options={options}
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
      </div>
    </div>
  );
}