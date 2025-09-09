// LEDBrick UI Controller
// Handles all UI interactions and chart visualization

let scheduleChart = null;
let currentSchedule = null;
let editingPointIndex = -1;

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
    if (points.length === 0) return [];
    
    // Add points at 00:00 and 23:59 if not present
    const fullPoints = [...points];
    
    // Check if we have a point at midnight
    if (fullPoints[0].time_minutes > 0) {
        // Add point at midnight with same values as last point of day
        const lastPoint = fullPoints[fullPoints.length - 1];
        fullPoints.unshift({
            time_minutes: 0,
            pwm_values: [...lastPoint.pwm_values],
            current_values: [...lastPoint.current_values]
        });
    }
    
    // Check if we have a point at end of day
    if (fullPoints[fullPoints.length - 1].time_minutes < 1439) {
        // Add point at 23:59 with same values as first point
        const firstPoint = fullPoints[0];
        fullPoints.push({
            time_minutes: 1439,
            pwm_values: [...firstPoint.pwm_values],
            current_values: [...firstPoint.current_values]
        });
    }
    
    return fullPoints;
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
        
        // Type
        const typeCell = row.insertCell(1);
        typeCell.textContent = point.type || 'Fixed';
        if (point.type && point.type !== 'Fixed') {
            typeCell.classList.add('astronomical-type');
        }
        
        // Channel values
        for (let i = 0; i < 8; i++) {
            const cell = row.insertCell(i + 2);
            const value = point.pwm_values[i];
            cell.textContent = value + '%';
            
            // Color code the cell based on intensity
            const intensity = value / 100;
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
    
    // Set type
    document.getElementById('pointType').value = point.type || 'fixed';
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
    
    // Set astronomical fields if needed
    if (point.astro_event) {
        document.getElementById('astroEvent').value = point.astro_event;
        document.getElementById('astroOffset').value = point.astro_offset || 0;
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
    let timeMinutes;
    let pointData = {};
    
    if (pointType === 'fixed') {
        const timeStr = document.getElementById('pointTime').value;
        const [hours, minutes] = timeStr.split(':').map(Number);
        timeMinutes = hours * 60 + minutes;
        pointData.type = 'fixed';
    } else {
        // For astronomical points, use a placeholder time for now
        // The backend will calculate the actual time based on location
        if (pointType === 'sunrise') {
            timeMinutes = 360; // 6:00 AM placeholder
        } else if (pointType === 'sunset') {
            timeMinutes = 1080; // 6:00 PM placeholder
        } else {
            // Custom astronomical event
            const baseEvent = document.getElementById('astroEvent').value;
            const offset = parseInt(document.getElementById('astroOffset').value);
            
            // Use placeholder times
            const baseTimes = {
                'sunrise': 360,
                'sunset': 1080,
                'civil_dawn': 330,
                'civil_dusk': 1110,
                'nautical_dawn': 300,
                'nautical_dusk': 1140
            };
            
            timeMinutes = (baseTimes[baseEvent] || 360) + offset;
            pointData.type = 'astronomical';
            pointData.astro_event = baseEvent;
            pointData.astro_offset = offset;
        }
    }
    
    // Get channel values
    const pwmValues = [];
    for (let i = 1; i <= 8; i++) {
        pwmValues.push(parseFloat(document.getElementById(`ch${i}Value`).value));
    }
    
    pointData.time_minutes = timeMinutes;
    pointData.pwm_values = pwmValues;
    pointData.current_values = new Array(8).fill(2.0);
    
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

async function updateStatus() {
    try {
        const status = await ledbrickAPI.getStatus();
        
        // Update all status fields
        document.getElementById('schedulerStatus').textContent = 
            'Scheduler: ' + (status.enabled ? 'ON' : 'OFF');
        document.getElementById('schedulerStatus').className = 
            'scheduler-status ' + (status.enabled ? 'enabled' : 'disabled');
        
        document.getElementById('currentTime').textContent = 
            'Time: ' + (status.time_formatted || '--:--');
        
        document.getElementById('schedulePoints').textContent = 
            (status.schedule_points || '0') + ' points';
        
        // Update PWM scale
        const pwmScale = status.pwm_scale * 100;
        document.getElementById('pwmScale').value = pwmScale;
        document.getElementById('pwmScaleValue').textContent = pwmScale + '%';
        
        // Update toggle button
        const toggleBtn = document.getElementById('toggleScheduler');
        toggleBtn.innerHTML = `
            <span class="icon">${status.enabled ? '⏸️' : '▶️'}</span>
            <span>${status.enabled ? 'Disable' : 'Enable'} Scheduler</span>
        `;
        
    } catch (error) {
        console.error('Failed to update status:', error);
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
    }
    
    // Try to connect and update
    setTimeout(async () => {
        if (savedUrl) {
            try {
                await testConnection();
                // Load schedule after successful connection
                await refreshSchedule();
                console.log('Schedule loaded successfully');
            } catch (error) {
                console.error('Failed to connect or load schedule:', error);
            }
        }
    }, 1000);
    
    // Update status periodically
    setInterval(updateStatus, 10000); // Every 10 seconds
});

// Channel color configuration functions
function initializeChannelConfig() {
    const grid = document.getElementById('channelConfigGrid');
    if (!grid || !currentSchedule) return;
    
    grid.innerHTML = '';
    
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
        
        const item = document.createElement('div');
        item.className = 'channel-config-item';
        
        const colorInput = document.createElement('input');
        colorInput.type = 'color';
        colorInput.className = 'channel-color-picker';
        colorInput.id = `channel-color-${i}`;
        colorInput.value = config.rgb_hex;
        colorInput.onchange = (e) => {
            channelColors[i] = e.target.value;
            updateScheduleChart(currentSchedule);
        };
        
        const label = document.createElement('label');
        label.className = 'channel-config-label';
        label.htmlFor = `channel-color-${i}`;
        label.textContent = config.name;
        
        item.appendChild(colorInput);
        item.appendChild(label);
        grid.appendChild(item);
    }
}

async function saveChannelColors() {
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
            currentSchedule.channel_configs.push({
                rgb_hex: colorInput ? colorInput.value : channelColors[i],
                name: channelNames[i] || `Channel ${i + 1}`,
                max_current: 2.0 // This will be read from ESPHome component
            });
        }
        
        // Save the entire schedule with updated channel configs
        await ledbrickAPI.importSchedule(currentSchedule);
        
        // Show success message
        const status = document.getElementById('connectionStatus');
        const oldClass = status.className;
        const oldText = status.textContent;
        status.className = 'status success';
        status.textContent = 'Channel colors saved successfully!';
        
        setTimeout(() => {
            status.className = oldClass;
            status.textContent = oldText;
        }, 3000);
        
    } catch (error) {
        console.error('Failed to save channel colors:', error);
        alert('Failed to save channel colors: ' + error.message);
    }
}