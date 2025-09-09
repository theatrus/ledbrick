// LEDBrick UI Controller
// Handles all UI interactions and chart visualization

let scheduleChart = null;
let currentSchedule = null;
let editingPointIndex = -1;
let lastServerState = {};

// Channel colors for the chart - will be loaded from schedule
let channelColors = [
    '#FFFFFF', // Default white
    '#0000FF', // Blue
    '#00FFFF', // Cyan
    '#00FF00', // Green
    '#FF0000', // Red
    '#FF00FF', // Magenta
    '#FFFF00', // Yellow
    '#FF8000'  // Orange
];

// Channel names (can be customized)
const channelNames = [
    'Channel 1',
    'Channel 2', 
    'Channel 3',
    'Channel 4',
    'Channel 5',
    'Channel 6',
    'Channel 7',
    'Channel 8'
];

// Initialize chart
function initializeChart() {
    const ctx = document.getElementById('scheduleChart').getContext('2d');
    scheduleChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: []
        },
        options: {
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
                        title: function(tooltipItems) {
                            return 'Time: ' + tooltipItems[0].label;
                        },
                        label: function(context) {
                            return context.dataset.label + ': ' + context.parsed.y + '%';
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
                        color: 'rgba(0, 0, 0, 0.1)'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Intensity (%)'
                    },
                    min: 0,
                    max: 100,
                    ticks: {
                        stepSize: 10
                    },
                    grid: {
                        display: true,
                        color: 'rgba(0, 0, 0, 0.1)'
                    }
                }
            }
        }
    });

    // Create custom legend
    createChannelLegend();
}

function createChannelLegend() {
    const legendContainer = document.getElementById('channelLegend');
    legendContainer.innerHTML = '';
    
    for (let i = 0; i < 8; i++) {
        const legendItem = document.createElement('div');
        legendItem.className = 'legend-item';
        legendItem.innerHTML = `
            <input type="checkbox" id="ch${i+1}Toggle" checked onchange="toggleChannel(${i})">
            <span class="legend-color" style="background-color: ${channelColors[i]}"></span>
            <label for="ch${i+1}Toggle">${channelNames[i]}</label>
        `;
        legendContainer.appendChild(legendItem);
    }
}

function toggleChannel(channelIndex) {
    if (scheduleChart && scheduleChart.data.datasets[channelIndex]) {
        scheduleChart.data.datasets[channelIndex].hidden = !scheduleChart.data.datasets[channelIndex].hidden;
        scheduleChart.update();
    }
}

function updateScheduleChart(schedule) {
    if (!scheduleChart) return;

    // Sort points by time
    const points = schedule.schedule_points.sort((a, b) => a.time_minutes - b.time_minutes);
    
    // If we have sunrise/sunset, extend to 24 hours
    const fullDayPoints = interpolateFullDay(points);
    
    // Create time labels
    const labels = fullDayPoints.map(p => {
        const hours = Math.floor(p.time_minutes / 60);
        const minutes = p.time_minutes % 60;
        return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
    });

    // Create datasets for each channel
    const datasets = [];
    for (let ch = 0; ch < 8; ch++) {
        const data = fullDayPoints.map(p => p.pwm_values[ch]);
        datasets.push({
            label: channelNames[ch],
            data: data,
            borderColor: channelColors[ch],
            backgroundColor: channelColors[ch] + '20',
            borderWidth: 2,
            pointRadius: 3,
            pointHoverRadius: 5,
            tension: 0.4,
            hidden: !document.getElementById(`ch${ch+1}Toggle`)?.checked
        });
    }

    scheduleChart.data.labels = labels;
    scheduleChart.data.datasets = datasets;
    scheduleChart.update();
}

function interpolateFullDay(points) {
    // Always generate full 24-hour range regardless of schedule points
    const result = [];
    
    // Generate time points every 15 minutes for smooth chart (96 points total)
    for (let minutes = 0; minutes <= 1440; minutes += 15) {
        // Handle end-of-day: 1440 minutes = 24:00 = next day 00:00
        const timeMinutes = minutes >= 1440 ? 1439 : minutes;
        
        // Interpolate values at this time
        let pwmValues = [];
        let currentValues = [];
        
        if (points.length === 0) {
            // No schedule points - default to all off
            pwmValues = new Array(8).fill(0);
            currentValues = new Array(8).fill(0);
        } else {
            // Find surrounding points for interpolation
            const interpolated = interpolateAtTime(points, timeMinutes);
            pwmValues = interpolated.pwm_values;
            currentValues = interpolated.current_values;
        }
        
        result.push({
            time_minutes: timeMinutes,
            pwm_values: pwmValues,
            current_values: currentValues
        });
    }
    
    return result;
}

// Helper function to interpolate values at a specific time
function interpolateAtTime(points, targetTime) {
    if (points.length === 0) {
        return {
            pwm_values: new Array(8).fill(0),
            current_values: new Array(8).fill(0)
        };
    }
    
    if (points.length === 1) {
        return {
            pwm_values: [...points[0].pwm_values],
            current_values: [...points[0].current_values]
        };
    }
    
    // Sort points by time
    const sortedPoints = [...points].sort((a, b) => a.time_minutes - b.time_minutes);
    
    // Handle wrap-around for 24-hour schedule
    // Find the right interpolation segment
    for (let i = 0; i < sortedPoints.length; i++) {
        const currentPoint = sortedPoints[i];
        const nextPoint = sortedPoints[(i + 1) % sortedPoints.length];
        
        let segmentStart = currentPoint.time_minutes;
        let segmentEnd = nextPoint.time_minutes;
        
        // Handle day wrap-around (e.g., 23:00 to 01:00)
        if (segmentEnd < segmentStart) {
            segmentEnd += 1440;
        }
        
        // Check if target time is in this segment
        let adjustedTargetTime = targetTime;
        if (targetTime < segmentStart && segmentEnd > 1440) {
            adjustedTargetTime += 1440;
        }
        
        if (adjustedTargetTime >= segmentStart && adjustedTargetTime <= segmentEnd) {
            // Interpolate between currentPoint and nextPoint
            const t = segmentEnd === segmentStart ? 0 : (adjustedTargetTime - segmentStart) / (segmentEnd - segmentStart);
            
            const pwmValues = [];
            const currentValues = [];
            
            for (let ch = 0; ch < 8; ch++) {
                const startPwm = currentPoint.pwm_values[ch] || 0;
                const endPwm = nextPoint.pwm_values[ch] || 0;
                const startCurrent = currentPoint.current_values[ch] || 0;
                const endCurrent = nextPoint.current_values[ch] || 0;
                
                pwmValues.push(startPwm + (endPwm - startPwm) * t);
                currentValues.push(startCurrent + (endCurrent - startCurrent) * t);
            }
            
            return { pwm_values: pwmValues, current_values: currentValues };
        }
    }
    
    // If we get here, just return the first point's values
    return {
        pwm_values: [...sortedPoints[0].pwm_values],
        current_values: [...sortedPoints[0].current_values]
    };
}

function updateScheduleTable(schedule) {
    const tbody = document.getElementById('scheduleTableBody');
    tbody.innerHTML = '';

    if (!schedule || !schedule.schedule_points) return;

    // Sort points by time
    const points = schedule.schedule_points.sort((a, b) => a.time_minutes - b.time_minutes);

    points.forEach((point, index) => {
        const row = tbody.insertRow();
        
        // Time
        const hours = Math.floor(point.time_minutes / 60);
        const minutes = point.time_minutes % 60;
        const timeStr = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
        row.insertCell(0).textContent = timeStr;
        
        // Type - handle time_type for dynamic points
        const typeCell = row.insertCell(1);
        let typeText = 'Fixed';
        if (point.time_type) {
            // Convert time_type to display text
            const typeMap = {
                'fixed': 'Fixed',
                'sunrise_relative': 'Sunrise',
                'sunset_relative': 'Sunset',
                'civil_dawn_relative': 'Civil Dawn',
                'civil_dusk_relative': 'Civil Dusk',
                'nautical_dawn_relative': 'Nautical Dawn',
                'nautical_dusk_relative': 'Nautical Dusk'
            };
            typeText = typeMap[point.time_type] || point.time_type;
            
            // Add offset if present
            if (point.offset_minutes !== undefined && point.offset_minutes !== 0) {
                const sign = point.offset_minutes > 0 ? '+' : '';
                typeText += ` ${sign}${point.offset_minutes}m`;
            }
        }
        typeCell.textContent = typeText;
        if (point.time_type && point.time_type !== 'fixed') {
            typeCell.classList.add('astronomical-type');
        }
        
        // Channel values - show both PWM and current
        for (let i = 0; i < 8; i++) {
            const cell = row.insertCell(i + 2);
            const pwmValue = point.pwm_values[i];
            const currentValue = point.current_values ? point.current_values[i] : null;
            
            // Show PWM% and current if available
            let cellText = pwmValue + '%';
            if (currentValue !== null && currentValue !== undefined) {
                cellText += `\n${currentValue.toFixed(1)}A`;
            }
            cell.textContent = cellText;
            cell.style.whiteSpace = 'pre-line';
            cell.style.fontSize = '11px';
            
            // Color code the cell based on intensity
            const intensity = pwmValue / 100;
            const color = channelColors[i];
            const rgb = hexToRgb(color);
            cell.style.backgroundColor = `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, ${intensity * 0.3})`;
            cell.style.color = intensity > 0.5 ? '#000' : '#666';
        }
        
        // Actions
        const actionsCell = row.insertCell(10);
        actionsCell.innerHTML = `
            <button onclick="editPoint(${index})" class="small-button">✏️</button>
            <button onclick="deletePoint(${index})" class="small-button danger">🗑️</button>
        `;
    });
}

function hexToRgb(hex) {
    const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return result ? {
        r: parseInt(result[1], 16),
        g: parseInt(result[2], 16),
        b: parseInt(result[3], 16)
    } : null;
}

function showAddPointDialog() {
    editingPointIndex = -1;
    document.getElementById('dialogTitle').textContent = 'Add Schedule Point';
    document.getElementById('pointDialog').style.display = 'flex';
    resetPointDialog();
}

function editPoint(index) {
    if (!currentSchedule || !currentSchedule.schedule_points[index]) return;
    
    editingPointIndex = index;
    const point = currentSchedule.schedule_points[index];
    
    document.getElementById('dialogTitle').textContent = 'Edit Schedule Point';
    
    // Set type based on time_type
    let pointType = 'fixed';
    if (point.time_type) {
        if (point.time_type === 'sunrise_relative') pointType = 'sunrise';
        else if (point.time_type === 'sunset_relative') pointType = 'sunset';
        else if (point.time_type !== 'fixed') pointType = 'astronomical';
    }
    document.getElementById('pointType').value = pointType;
    updatePointTypeFields();
    
    // Set time
    const hours = Math.floor(point.time_minutes / 60);
    const minutes = point.time_minutes % 60;
    document.getElementById('pointTime').value = 
        `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
    
    // Set channel values
    for (let i = 0; i < 8; i++) {
        const value = point.pwm_values[i];
        document.getElementById(`ch${i+1}Value`).value = value;
        document.querySelector(`.channel-input[data-channel="${i+1}"] input[type="range"]`).value = value;
    }
    
    // Set astronomical fields based on time_type
    if (point.time_type && point.time_type !== 'fixed') {
        // Extract base event from time_type
        const eventMap = {
            'sunrise_relative': 'sunrise',
            'sunset_relative': 'sunset',
            'civil_dawn_relative': 'civil_dawn',
            'civil_dusk_relative': 'civil_dusk',
            'nautical_dawn_relative': 'nautical_dawn',
            'nautical_dusk_relative': 'nautical_dusk'
        };
        const baseEvent = eventMap[point.time_type] || 'sunrise';
        document.getElementById('astroEvent').value = baseEvent;
        document.getElementById('astroOffset').value = point.offset_minutes || 0;
    }
    
    document.getElementById('pointDialog').style.display = 'flex';
}

function deletePoint(index) {
    if (!confirm('Delete this schedule point?')) return;
    
    if (currentSchedule && currentSchedule.schedule_points) {
        currentSchedule.schedule_points.splice(index, 1);
        // Update the schedule on the device
        importScheduleData(currentSchedule);
    }
}

function resetPointDialog() {
    document.getElementById('pointType').value = 'fixed';
    document.getElementById('pointTime').value = '12:00';
    document.getElementById('astroEvent').value = 'sunrise';
    document.getElementById('astroOffset').value = '0';
    updatePointTypeFields();
    setAllChannels(0);
}

function closePointDialog() {
    document.getElementById('pointDialog').style.display = 'none';
}

function updatePointTypeFields() {
    const pointType = document.getElementById('pointType').value;
    document.getElementById('fixedTimeFields').style.display = 
        pointType === 'fixed' ? 'block' : 'none';
    document.getElementById('astronomicalFields').style.display = 
        ['sunrise', 'sunset', 'astronomical'].includes(pointType) ? 'block' : 'none';
}

function setAllChannels(value) {
    for (let i = 1; i <= 8; i++) {
        document.getElementById(`ch${i}Value`).value = value;
        document.querySelector(`.channel-input[data-channel="${i}"] input[type="range"]`).value = value;
    }
}

async function saveSchedulePoint() {
    const pointType = document.getElementById('pointType').value;
    let pointData = {};
    
    if (pointType === 'fixed') {
        const timeStr = document.getElementById('pointTime').value;
        const [hours, minutes] = timeStr.split(':').map(Number);
        pointData.time_minutes = hours * 60 + minutes;
        pointData.time_type = 'fixed';
    } else {
        // For astronomical points, set time_type and offset
        const offset = parseInt(document.getElementById('astroOffset').value || '0');
        
        if (pointType === 'sunrise') {
            pointData.time_type = 'sunrise_relative';
            pointData.offset_minutes = offset;
            // Backend will calculate actual time
            pointData.time_minutes = 360; // placeholder
        } else if (pointType === 'sunset') {
            pointData.time_type = 'sunset_relative';
            pointData.offset_minutes = offset;
            pointData.time_minutes = 1080; // placeholder
        } else {
            // Custom astronomical event
            const baseEvent = document.getElementById('astroEvent').value;
            const eventTypeMap = {
                'sunrise': 'sunrise_relative',
                'sunset': 'sunset_relative',
                'civil_dawn': 'civil_dawn_relative',
                'civil_dusk': 'civil_dusk_relative',
                'nautical_dawn': 'nautical_dawn_relative',
                'nautical_dusk': 'nautical_dusk_relative'
            };
            pointData.time_type = eventTypeMap[baseEvent] || 'sunrise_relative';
            pointData.offset_minutes = offset;
            
            // Placeholder times
            const baseTimes = {
                'sunrise': 360,
                'sunset': 1080,
                'civil_dawn': 330,
                'civil_dusk': 1110,
                'nautical_dawn': 300,
                'nautical_dusk': 1140
            };
            pointData.time_minutes = baseTimes[baseEvent] || 360;
        }
    }
    
    // Get channel values
    const pwmValues = [];
    const currentValues = [];
    for (let i = 1; i <= 8; i++) {
        const pwmValue = parseFloat(document.getElementById(`ch${i}Value`).value);
        pwmValues.push(pwmValue);
        
        // Calculate current based on PWM and max current from channel config
        if (currentSchedule && currentSchedule.channel_configs && currentSchedule.channel_configs[i-1]) {
            const maxCurrent = currentSchedule.channel_configs[i-1].max_current || 2.0;
            currentValues.push((pwmValue / 100) * maxCurrent);
        } else {
            currentValues.push(pwmValue / 100 * 2.0); // Default 2A max
        }
    }
    
    pointData.pwm_values = pwmValues;
    pointData.current_values = currentValues;
    
    try {
        if (editingPointIndex >= 0) {
            // Update existing point
            currentSchedule.schedule_points[editingPointIndex] = pointData;
            await importScheduleData(currentSchedule);
        } else {
            // Add new point
            if (!currentSchedule) {
                currentSchedule = { schedule_points: [] };
            }
            currentSchedule.schedule_points.push(pointData);
            await importScheduleData(currentSchedule);
        }
        
        showStatus('Schedule point saved', 'success');
        closePointDialog();
        setTimeout(refreshSchedule, 500);
    } catch (error) {
        showStatus('Failed to save point: ' + error.error, 'error');
    }
}

async function refreshSchedule() {
    try {
        currentSchedule = await ledbrickAPI.getSchedule();
        initializeChannelConfig(); // Initialize channel color configuration
        updateScheduleChart(currentSchedule);
        updateScheduleTable(currentSchedule);
        document.getElementById('schedulePoints').textContent = 
            (currentSchedule.schedule_points ? currentSchedule.schedule_points.length : 0) + ' points';
    } catch (error) {
        showStatus('Failed to load schedule: ' + error.error, 'error');
    }
}

async function clearSchedule() {
    if (!confirm('Are you sure you want to clear the entire schedule?')) return;
    
    try {
        await ledbrickAPI.clearSchedule();
        showStatus('Schedule cleared', 'success');
        setTimeout(refreshSchedule, 500);
    } catch (error) {
        showStatus('Failed to clear schedule: ' + error.error, 'error');
    }
}

async function loadPreset(presetName) {
    try {
        const result = await ledbrickAPI.loadPreset(presetName);
        showStatus(result.message || `Loaded preset: ${presetName}`, 'success');
        setTimeout(refreshSchedule, 1000);
    } catch (error) {
        showStatus('Failed to load preset: ' + error.error, 'error');
    }
}

async function exportSchedule() {
    try {
        const schedule = await ledbrickAPI.getSchedule();
        const formatted = JSON.stringify(schedule, null, 2);
        document.getElementById('jsonData').value = formatted;
        showJsonStatus('Schedule exported successfully', 'success');
    } catch (error) {
        showJsonStatus('Failed to export schedule: ' + error.error, 'error');
    }
}

async function importSchedule() {
    const jsonData = document.getElementById('jsonData').value.trim();
    if (!jsonData) {
        showJsonStatus('Please enter JSON data', 'error');
        return;
    }
    
    try {
        // Validate JSON
        const parsed = JSON.parse(jsonData);
        await importScheduleData(parsed);
    } catch (error) {
        if (error instanceof SyntaxError) {
            showJsonStatus('Invalid JSON format', 'error');
        } else {
            showJsonStatus('Failed to import: ' + (error.error || error.message), 'error');
        }
    }
}

async function importScheduleData(scheduleData) {
    try {
        await ledbrickAPI.importSchedule(scheduleData);
        showStatus('Schedule imported successfully', 'success');
        setTimeout(refreshSchedule, 500);
    } catch (error) {
        showStatus('Failed to import schedule: ' + error.error, 'error');
    }
}

async function testConnection() {
    const status = document.getElementById('connectionStatus');
    status.className = 'status-indicator connecting';
    status.textContent = 'Connecting...';
    
    // Save settings
    const deviceUrl = document.getElementById('deviceUrl').value;
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    
    localStorage.setItem('deviceUrl', deviceUrl);
    ledbrickAPI.baseUrl = deviceUrl;
    ledbrickAPI.setAuth(username, password);
    
    try {
        const result = await ledbrickAPI.getStatus();
        status.className = 'status-indicator connected';
        status.textContent = 'Connected';
        updateStatus();
        refreshSchedule();
    } catch (error) {
        status.className = 'status-indicator disconnected';
        status.textContent = 'Disconnected';
        showStatus('Connection failed: ' + (error.error || error.message || 'Unknown error'), 'error');
    }
}

async function toggleScheduler() {
    try {
        const status = await ledbrickAPI.getStatus();
        await ledbrickAPI.setSchedulerEnabled(!status.enabled);
        showStatus('Scheduler ' + (status.enabled ? 'disabled' : 'enabled'), 'success');
        updateStatus();
    } catch (error) {
        showStatus('Failed to toggle scheduler: ' + error.error, 'error');
    }
}

async function updatePWMScale(value) {
    try {
        await ledbrickAPI.setPWMScale(value);
        document.getElementById('pwmScaleValue').textContent = value + '%';
        showStatus(`Brightness set to ${value}%`, 'success');
    } catch (error) {
        showStatus('Failed to set brightness: ' + error.error, 'error');
    }
}

// Track last status to avoid unnecessary updates

async function updateStatus() {
    try {
        const status = await ledbrickAPI.getStatus();
        
        // Only update elements when values actually change
        
        // Scheduler status
        const schedulerText = 'Scheduler: ' + (status.enabled ? 'ON' : 'OFF');
        const schedulerElement = document.getElementById('schedulerStatus');
        if (schedulerElement.textContent !== schedulerText) {
            schedulerElement.textContent = schedulerText;
            schedulerElement.className = 'scheduler-status ' + (status.enabled ? 'enabled' : 'disabled');
        }
        
        // Current time
        const timeText = 'Time: ' + (status.time_formatted || '--:--');
        const timeElement = document.getElementById('currentTime');
        if (timeElement.textContent !== timeText) {
            timeElement.textContent = timeText;
        }
        
        // Schedule points count
        const pointsText = (status.schedule_points || '0') + ' points';
        const pointsElement = document.getElementById('schedulePoints');
        if (pointsElement.textContent !== pointsText) {
            pointsElement.textContent = pointsText;
        }
        
        // PWM scale - only update if not currently being edited
        const pwmScale = Math.round(status.pwm_scale * 100);
        const pwmSlider = document.getElementById('pwmScale');
        const pwmValue = document.getElementById('pwmScaleValue');
        if (pwmSlider && !pwmSlider.matches(':focus') && Math.abs(parseInt(pwmSlider.value) - pwmScale) > 0) {
            pwmSlider.value = pwmScale;
            pwmValue.textContent = pwmScale + '%';
        }
        
        // Location - only update if server state changed
        const latInput = document.getElementById('latitude');
        const lonInput = document.getElementById('longitude');
        if (status.latitude !== undefined && latInput && (!lastServerState || Math.abs(lastServerState.latitude - status.latitude) > 0.0001)) {
            latInput.value = status.latitude.toFixed(4);
        }
        if (status.longitude !== undefined && lonInput && (!lastServerState || Math.abs(lastServerState.longitude - status.longitude) > 0.0001)) {
            lonInput.value = status.longitude.toFixed(4);
        }
        
        // Time shift settings - only update if server state changed
        const astroProjection = document.getElementById('astronomicalProjection');
        if (status.astronomical_projection !== undefined && astroProjection && 
            (!lastServerState || lastServerState.astronomical_projection !== status.astronomical_projection)) {
            astroProjection.checked = status.astronomical_projection;
            updateTimeShiftUI();
        }
        
        const hoursInput = document.getElementById('timeShiftHours');
        if (status.time_shift_hours !== undefined && hoursInput && 
            (!lastServerState || lastServerState.time_shift_hours !== status.time_shift_hours)) {
            hoursInput.value = status.time_shift_hours;
        }
        
        const minutesInput = document.getElementById('timeShiftMinutes');
        if (status.time_shift_minutes !== undefined && minutesInput && 
            (!lastServerState || lastServerState.time_shift_minutes !== status.time_shift_minutes)) {
            minutesInput.value = status.time_shift_minutes;
        }
        
        // Update toggle button only if enabled state changed
        if (!lastServerState || lastServerState.enabled !== status.enabled) {
            const toggleBtn = document.getElementById('toggleScheduler');
            if (toggleBtn) {
                toggleBtn.innerHTML = `
                    <span class="icon">${status.enabled ? '⏸️' : '▶️'}</span>
                    <span>${status.enabled ? 'Disable' : 'Enable'} Scheduler</span>
                `;
            }
        }
        
        // Update current channel values display
        updateCurrentChannelValues(status.channels);
        
        // Update chart with current time line
        updateChartCurrentTimeLine(status.time_minutes);
        
        // Schedule refresh is handled explicitly when we know changes occurred
        // (after imports, preset loads, point edits, etc.)
        
        // Update moon simulation settings if provided
        updateMoonSimulationFromStatus(status);
        
        // Store current status for next comparison - only configuration values, not dynamic data
        lastServerState = {
            enabled: status.enabled,
            latitude: status.latitude,
            longitude: status.longitude,
            astronomical_projection: status.astronomical_projection,
            time_shift_hours: status.time_shift_hours,
            time_shift_minutes: status.time_shift_minutes,
            moon_simulation: status.moon_simulation ? {
                enabled: status.moon_simulation.enabled,
                phase_scaling: status.moon_simulation.phase_scaling,
                base_intensity: status.moon_simulation.base_intensity ? [...status.moon_simulation.base_intensity] : []
            } : null
        };
        
    } catch (error) {
        console.error('Failed to update status:', error);
    }
}

// Update current channel values display
function updateCurrentChannelValues(channels) {
    if (!channels || !Array.isArray(channels)) return;
    
    // Update current values display or create if it doesn't exist
    let currentValuesContainer = document.getElementById('currentChannelValues');
    if (!currentValuesContainer) {
        // Create current values display in the status bar
        const statusBar = document.querySelector('.status-bar');
        if (statusBar) {
            currentValuesContainer = document.createElement('div');
            currentValuesContainer.id = 'currentChannelValues';
            currentValuesContainer.className = 'current-values';
            statusBar.appendChild(currentValuesContainer);
        } else {
            return;
        }
    }
    
    // Update or create channel value displays
    channels.forEach((channel, index) => {
        let channelDisplay = document.getElementById(`currentCh${channel.id}`);
        if (!channelDisplay) {
            channelDisplay = document.createElement('div');
            channelDisplay.id = `currentCh${channel.id}`;
            channelDisplay.className = 'channel-current';
            currentValuesContainer.appendChild(channelDisplay);
        }
        
        const pwmPercent = Math.round(channel.pwm || 0);
        const currentAmps = (channel.current || 0).toFixed(2);
        const color = channelColors[index] || '#FFFFFF';
        
        channelDisplay.innerHTML = `
            <span class="channel-label" style="color: ${color}">Ch${channel.id}:</span>
            <span class="channel-value">${pwmPercent}% (${currentAmps}A)</span>
        `;
        channelDisplay.style.borderLeft = `3px solid ${color}`;
    });
}

// Update chart with current time vertical line using a dataset
function updateChartCurrentTimeLine(currentTimeMinutes) {
    if (!scheduleChart || currentTimeMinutes === undefined) return;
    
    // Convert minutes to time string
    const hours = Math.floor(currentTimeMinutes / 60);
    const minutes = currentTimeMinutes % 60;
    const currentTimeStr = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
    
    // Find or create the "now" line dataset
    let nowDatasetIndex = scheduleChart.data.datasets.findIndex(ds => ds.id === 'currentTime');
    
    if (nowDatasetIndex === -1) {
        // Create a new "now" line dataset
        const nowDataset = {
            id: 'currentTime',
            label: 'Current Time',
            data: [
                { x: currentTimeStr, y: 0 },
                { x: currentTimeStr, y: 100 }
            ],
            borderColor: 'rgba(255, 0, 0, 0.8)',
            backgroundColor: 'rgba(255, 0, 0, 0.1)',
            borderWidth: 3,
            pointRadius: 0,
            pointHoverRadius: 0,
            tension: 0,
            fill: false,
            spanGaps: false,
            showLine: true,
            borderDash: [5, 5],
            hidden: false
        };
        scheduleChart.data.datasets.push(nowDataset);
        nowDatasetIndex = scheduleChart.data.datasets.length - 1;
    } else {
        // Update existing dataset
        scheduleChart.data.datasets[nowDatasetIndex].data = [
            { x: currentTimeStr, y: 0 },
            { x: currentTimeStr, y: 100 }
        ];
    }
    
    // Only update the chart if it's not being actively modified
    if (!document.querySelector('.chart-container:hover')) {
        scheduleChart.update('none'); // Use 'none' mode for performance
    }
}

function showStatus(message, type = 'info') {
    // Create a toast notification
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;
    document.body.appendChild(toast);
    
    // Show the toast
    setTimeout(() => toast.classList.add('show'), 10);
    
    // Remove after 3 seconds
    setTimeout(() => {
        toast.classList.remove('show');
        setTimeout(() => document.body.removeChild(toast), 300);
    }, 3000);
}

function showJsonStatus(message, type = 'info') {
    const status = document.getElementById('jsonStatus');
    status.textContent = message;
    status.className = 'status-message ' + type;
    setTimeout(() => { status.textContent = ''; }, 5000);
}

// Location functions
function setLocation(lat, lon) {
    document.getElementById('latitude').value = lat;
    document.getElementById('longitude').value = lon;
}

async function saveLocation() {
    const latitude = parseFloat(document.getElementById('latitude').value);
    const longitude = parseFloat(document.getElementById('longitude').value);
    
    if (isNaN(latitude) || isNaN(longitude)) {
        showStatus('Invalid latitude or longitude', 'error');
        return;
    }
    
    try {
        const response = await ledbrickAPI.request('POST', '/api/location', {
            latitude: latitude,
            longitude: longitude
        });
        
        showStatus('Location saved successfully', 'success');
        
        // ESPHome entities will automatically update from the scheduler state
    } catch (error) {
        showStatus('Failed to save location: ' + error.error, 'error');
    }
}

// Time shift functions
function updateTimeShiftUI() {
    const enabled = document.getElementById('astronomicalProjection').checked;
    document.getElementById('timeShiftControls').style.display = enabled ? 'block' : 'none';
}

async function saveTimeShift() {
    const projection_enabled = document.getElementById('astronomicalProjection').checked;
    const time_shift_hours = parseInt(document.getElementById('timeShiftHours').value) || 0;
    const time_shift_minutes = parseInt(document.getElementById('timeShiftMinutes').value) || 0;
    
    try {
        const response = await ledbrickAPI.request('POST', '/api/time_shift', {
            astronomical_projection: projection_enabled,
            time_shift_hours: time_shift_hours,
            time_shift_minutes: time_shift_minutes
        });
        
        showStatus('Time settings saved successfully', 'success');
        
        // ESPHome entities will automatically update from the scheduler state
    } catch (error) {
        showStatus('Failed to save time settings: ' + error.error, 'error');
    }
}

// Update moon simulation UI from status
function updateMoonSimulationFromStatus(status) {
    if (!status.moon_simulation) return;
    
    const moonSim = status.moon_simulation;
    const lastMoonSim = lastServerState.moon_simulation;
    
    // Only update if server state actually changed
    if (!lastMoonSim || lastMoonSim.enabled !== moonSim.enabled) {
        const enabledCheckbox = document.getElementById('moonSimulationEnabled');
        if (enabledCheckbox && enabledCheckbox.checked !== moonSim.enabled) {
            enabledCheckbox.checked = moonSim.enabled;
            updateMoonSimulationUI();
        }
    }
    
    // Only update phase scaling if server state changed
    if (!lastMoonSim || lastMoonSim.phase_scaling !== moonSim.phase_scaling) {
        const phaseScalingCheckbox = document.getElementById('moonPhaseScaling');
        if (phaseScalingCheckbox && phaseScalingCheckbox.checked !== moonSim.phase_scaling) {
            phaseScalingCheckbox.checked = moonSim.phase_scaling;
        }
    }
    
    // Only update base intensity values if server state changed
    if (moonSim.base_intensity && Array.isArray(moonSim.base_intensity)) {
        for (let i = 0; i < Math.min(moonSim.base_intensity.length, 8); i++) {
            const serverValue = moonSim.base_intensity[i];
            const lastValue = lastMoonSim && lastMoonSim.base_intensity ? lastMoonSim.base_intensity[i] : undefined;
            
            // Only update if server value changed
            if (lastValue === undefined || Math.abs(lastValue - serverValue) > 0.05) {
                const slider = document.getElementById(`moonCh${i+1}`);
                const input = document.getElementById(`moonCh${i+1}Value`);
                
                if (slider && input) {
                    const value = serverValue.toFixed(1);
                    slider.value = value;
                    input.value = value;
                }
            }
        }
    }
}

// Moon simulation functions
function updateMoonSimulationUI() {
    const enabled = document.getElementById('moonSimulationEnabled').checked;
    document.getElementById('moonSimulationControls').style.display = enabled ? 'block' : 'none';
}

function setAllMoonChannels(value) {
    for (let i = 1; i <= 8; i++) {
        document.getElementById(`moonCh${i}`).value = value;
        document.getElementById(`moonCh${i}Value`).value = value;
    }
}

async function saveMoonSimulation() {
    const enabled = document.getElementById('moonSimulationEnabled').checked;
    const phaseScaling = document.getElementById('moonPhaseScaling').checked;
    
    // Collect base intensity values
    const baseIntensity = [];
    for (let i = 1; i <= 8; i++) {
        const value = parseFloat(document.getElementById(`moonCh${i}Value`).value) || 0;
        baseIntensity.push(value);
    }
    
    const moonConfig = {
        enabled: enabled,
        phase_scaling: phaseScaling,
        base_intensity: baseIntensity
    };
    
    try {
        const response = await ledbrickAPI.request('POST', '/api/moon_simulation', moonConfig);
        showStatus('Moon simulation settings saved successfully', 'success');
    } catch (error) {
        showStatus('Failed to save moon simulation settings: ' + error.error, 'error');
    }
}

// Initialize
document.addEventListener('DOMContentLoaded', function() {
    initializeChart();
    
    // Load saved connection settings
    const savedUrl = localStorage.getItem('deviceUrl');
    if (savedUrl) {
        document.getElementById('deviceUrl').value = savedUrl;
        ledbrickAPI.baseUrl = savedUrl;
    }
    
    // Set up channel input synchronization
    for (let i = 1; i <= 8; i++) {
        const numberInput = document.getElementById(`ch${i}Value`);
        const rangeInput = document.querySelector(`.channel-input[data-channel="${i}"] input[type="range"]`);
        
        numberInput.addEventListener('input', function() {
            rangeInput.value = this.value;
        });
        
        rangeInput.addEventListener('input', function() {
            numberInput.value = this.value;
        });
        
        // Set up moon simulation input synchronization
        const moonNumberInput = document.getElementById(`moonCh${i}Value`);
        const moonRangeInput = document.getElementById(`moonCh${i}`);
        
        if (moonNumberInput && moonRangeInput) {
            moonNumberInput.addEventListener('input', function() {
                moonRangeInput.value = this.value;
            });
            
            moonRangeInput.addEventListener('input', function() {
                moonNumberInput.value = this.value;
            });
        }
    }
    
    
    // Automatically load schedule on page load
    setTimeout(async () => {
        try {
            // No need to test connection - we're on the device itself
            await refreshSchedule();
            console.log('Schedule loaded successfully');
        } catch (error) {
            console.error('Failed to load schedule:', error);
        }
    }, 500);
    
    // Update status periodically
    setInterval(updateStatus, 10000); // Every 10 seconds
});

// Channel configuration functions
function initializeChannelConfig() {
    const tbody = document.getElementById('channelConfigTableBody');
    if (!tbody || !currentSchedule) return;
    
    tbody.innerHTML = '';
    
    const numChannels = currentSchedule.num_channels || 8;
    const configs = currentSchedule.channel_configs || [];
    
    for (let i = 0; i < numChannels; i++) {
        const config = configs[i] || {
            rgb_hex: channelColors[i],
            name: `Channel ${i + 1}`,
            max_current: 2.0
        };
        
        // Update global channel colors array
        channelColors[i] = config.rgb_hex;
        
        const row = document.createElement('tr');
        
        // Channel number
        const chCell = document.createElement('td');
        chCell.textContent = `${i + 1}`;
        row.appendChild(chCell);
        
        // Color picker
        const colorCell = document.createElement('td');
        const colorInput = document.createElement('input');
        colorInput.type = 'color';
        colorInput.className = 'channel-color-picker';
        colorInput.id = `channel-color-${i}`;
        colorInput.value = config.rgb_hex;
        colorInput.onchange = (e) => {
            channelColors[i] = e.target.value;
            updateScheduleChart(currentSchedule);
        };
        colorCell.appendChild(colorInput);
        row.appendChild(colorCell);
        
        // Channel name
        const nameCell = document.createElement('td');
        const nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.className = 'channel-name-input';
        nameInput.id = `channel-name-${i}`;
        nameInput.value = config.name;
        nameCell.appendChild(nameInput);
        row.appendChild(nameCell);
        
        // Max current
        const currentCell = document.createElement('td');
        const currentInput = document.createElement('input');
        currentInput.type = 'number';
        currentInput.className = 'channel-current-input';
        currentInput.id = `channel-current-${i}`;
        currentInput.value = config.max_current.toFixed(1);
        currentInput.min = '0.1';
        currentInput.max = '2.0';
        currentInput.step = '0.1';
        currentCell.appendChild(currentInput);
        row.appendChild(currentCell);
        
        tbody.appendChild(row);
    }
}

async function saveChannelConfig() {
    if (!currentSchedule) {
        alert('No schedule loaded');
        return;
    }
    
    try {
        // Update the current schedule with new channel configs
        const numChannels = currentSchedule.num_channels || 8;
        currentSchedule.channel_configs = [];
        
        for (let i = 0; i < numChannels; i++) {
            const colorInput = document.getElementById(`channel-color-${i}`);
            const nameInput = document.getElementById(`channel-name-${i}`);
            const currentInput = document.getElementById(`channel-current-${i}`);
            
            currentSchedule.channel_configs.push({
                rgb_hex: colorInput ? colorInput.value : channelColors[i],
                name: nameInput ? nameInput.value : `Channel ${i + 1}`,
                max_current: currentInput ? parseFloat(currentInput.value) : 2.0
            });
        }
        
        // Save the entire schedule with updated channel configs
        await ledbrickAPI.importSchedule(currentSchedule);
        
        // Show success message
        showStatus('Channel configuration saved successfully!', 'success');
        
        setTimeout(() => {
            status.className = oldClass;
            status.textContent = oldText;
        }, 3000);
        
    } catch (error) {
        console.error('Failed to save channel colors:', error);
        alert('Failed to save channel colors: ' + error.message);
    }
}