// LEDBrick Web Control JavaScript

// Global settings
let deviceUrl = 'http://ledbrickplus.local';
let authHeader = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    // Load saved settings
    const savedUrl = localStorage.getItem('deviceUrl');
    if (savedUrl) {
        document.getElementById('deviceUrl').value = savedUrl;
        deviceUrl = savedUrl;
    }
    
    // Set up channel sliders
    for (let i = 1; i <= 4; i++) {
        const slider = document.getElementById(`ch${i}pwm`);
        const display = document.getElementById(`ch${i}val`);
        slider.addEventListener('input', function() {
            display.textContent = this.value + '%';
        });
    }
    
    // Update status on load
    setTimeout(updateStatus, 1000);
});

// Make authenticated request
async function makeRequest(path, method = 'GET', body = null) {
    const url = deviceUrl + path;
    const options = {
        method: method,
        headers: {}
    };
    
    // Add auth header if credentials provided
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    if (username && password) {
        options.headers['Authorization'] = 'Basic ' + btoa(username + ':' + password);
    }
    
    // Add body if provided
    if (body) {
        if (method === 'POST' && typeof body === 'object') {
            // For URL-encoded form data
            const params = new URLSearchParams();
            for (const key in body) {
                params.append(key, body[key]);
            }
            options.body = params;
            options.headers['Content-Type'] = 'application/x-www-form-urlencoded';
        } else {
            options.body = body;
        }
    }
    
    try {
        const response = await fetch(url, options);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.text();
    } catch (error) {
        console.error('Request failed:', error);
        throw error;
    }
}

// Test connection
async function testConnection() {
    const status = document.getElementById('connectionStatus');
    status.style.display = 'block';
    status.className = 'status';
    status.textContent = 'Testing connection...';
    
    // Save URL
    deviceUrl = document.getElementById('deviceUrl').value;
    localStorage.setItem('deviceUrl', deviceUrl);
    
    try {
        // Try to get the root page
        await makeRequest('/');
        status.className = 'status success';
        status.textContent = 'Connection successful!';
        
        // Update system status
        updateStatus();
    } catch (error) {
        status.className = 'status error';
        status.textContent = 'Connection failed: ' + error.message;
    }
}

// Toggle scheduler
async function toggleScheduler() {
    try {
        // First get current state
        const response = await makeRequest('/switch/web_scheduler_enable');
        const data = JSON.parse(response);
        const currentState = data.state === 'ON';
        
        // Toggle it
        const action = currentState ? 'turn_off' : 'turn_on';
        await makeRequest(`/switch/web_scheduler_enable/${action}`, 'POST');
        
        showStatus('Scheduler ' + (currentState ? 'disabled' : 'enabled'), 'success');
        updateStatus();
    } catch (error) {
        showStatus('Failed to toggle scheduler: ' + error.message, 'error');
    }
}

// Set PWM scale
async function setPWMScale(value) {
    try {
        await makeRequest(`/number/pwm_scale/set`, 'POST', { value: value });
        document.getElementById('pwmScale').value = value;
        document.getElementById('pwmScaleValue').textContent = value + '%';
        showStatus(`PWM scale set to ${value}%`, 'success');
    } catch (error) {
        showStatus('Failed to set PWM scale: ' + error.message, 'error');
    }
}

// Update PWM scale from slider
function updatePWMScale(value) {
    document.getElementById('pwmScaleValue').textContent = value + '%';
    // Debounce the actual update
    clearTimeout(window.pwmScaleTimeout);
    window.pwmScaleTimeout = setTimeout(() => setPWMScale(value), 500);
}

// Load preset
async function loadPreset(presetName) {
    try {
        // Set the select component
        await makeRequest('/select/preset_selector/set', 'POST', { option: presetName });
        showStatus(`Loaded preset: ${presetName}`, 'success');
        
        // Update status after a delay
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to load preset: ' + error.message, 'error');
    }
}

// Execute script
async function executeScript(scriptName) {
    try {
        await makeRequest(`/button/load_sunrise_demo_button/press`, 'POST');
        showStatus('Sunrise demo loaded', 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to execute script: ' + error.message, 'error');
    }
}

// Clear schedule
async function clearSchedule() {
    if (!confirm('Are you sure you want to clear the schedule?')) {
        return;
    }
    
    try {
        await makeRequest('/button/clear_schedule_button/press', 'POST');
        showStatus('Schedule cleared', 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to clear schedule: ' + error.message, 'error');
    }
}

// Save to flash
async function saveToFlash() {
    try {
        await makeRequest('/button/save_to_flash_button/press', 'POST');
        showStatus('Schedule saved to flash', 'success');
    } catch (error) {
        showStatus('Failed to save to flash: ' + error.message, 'error');
    }
}

// Add schedule point
async function addSchedulePoint() {
    try {
        // Get time in minutes
        const timeStr = document.getElementById('scheduleTime').value;
        const [hours, minutes] = timeStr.split(':').map(Number);
        const timeMinutes = hours * 60 + minutes;
        
        // Set time input
        await makeRequest('/number/schedule_time_input/set', 'POST', { value: timeMinutes });
        
        // Set channel values
        for (let i = 1; i <= 4; i++) {
            const value = document.getElementById(`ch${i}pwm`).value;
            await makeRequest(`/number/ch${i}_pwm_input/set`, 'POST', { value: value });
        }
        
        // Press the add button
        await makeRequest('/button/add_schedule_point_button/press', 'POST');
        
        showStatus(`Added schedule point at ${timeStr}`, 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to add schedule point: ' + error.message, 'error');
    }
}

// Export schedule
async function exportSchedule() {
    try {
        // Press export button to update the sensor
        await makeRequest('/button/export_schedule_json_button/press', 'POST');
        
        // Wait a bit for the sensor to update
        await new Promise(resolve => setTimeout(resolve, 500));
        
        // Get the JSON from the sensor
        const response = await makeRequest('/text_sensor/schedule_json_export');
        const data = JSON.parse(response);
        const scheduleJson = data.state;
        
        // Display in textarea
        document.getElementById('jsonData').value = formatJsonString(scheduleJson);
        
        showJsonStatus('Schedule exported successfully', 'success');
    } catch (error) {
        showJsonStatus('Failed to export schedule: ' + error.message, 'error');
    }
}

// Import schedule
async function importSchedule() {
    const jsonData = document.getElementById('jsonData').value.trim();
    if (!jsonData) {
        showJsonStatus('Please enter JSON data', 'error');
        return;
    }
    
    try {
        // Validate JSON
        JSON.parse(jsonData);
        
        // Set the JSON data in the text input
        // Note: This might fail if JSON is too long for web API
        await makeRequest('/text/schedule_json_import/set', 'POST', { value: jsonData });
        
        // Press import button
        await makeRequest('/button/import_schedule_json_button/press', 'POST');
        
        showJsonStatus('Schedule imported successfully', 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        if (error instanceof SyntaxError) {
            showJsonStatus('Invalid JSON format', 'error');
        } else {
            showJsonStatus('Failed to import schedule: ' + error.message, 'error');
        }
    }
}

// Format JSON in textarea
function formatJSON() {
    const textarea = document.getElementById('jsonData');
    try {
        const json = JSON.parse(textarea.value);
        textarea.value = JSON.stringify(json, null, 2);
        showJsonStatus('JSON formatted', 'success');
    } catch (error) {
        showJsonStatus('Invalid JSON format', 'error');
    }
}

// Format JSON string
function formatJsonString(jsonStr) {
    try {
        const json = JSON.parse(jsonStr);
        return JSON.stringify(json, null, 2);
    } catch (error) {
        return jsonStr;
    }
}

// Update system status
async function updateStatus() {
    try {
        // Get scheduler status
        const schedulerResp = await makeRequest('/switch/web_scheduler_enable');
        const schedulerData = JSON.parse(schedulerResp);
        document.getElementById('schedulerStatus').textContent = 
            schedulerData.state === 'ON' ? 'Enabled' : 'Disabled';
        
        // Get current time
        const timeResp = await makeRequest('/sensor/web_scheduler_time');
        const timeData = JSON.parse(timeResp);
        const timeMin = parseInt(timeData.value);
        const hours = Math.floor(timeMin / 60);
        const minutes = timeMin % 60;
        document.getElementById('currentTime').textContent = 
            `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
        
        // Get schedule status
        const statusResp = await makeRequest('/text_sensor/schedule_status_sensor');
        const statusData = JSON.parse(statusResp);
        document.getElementById('schedulePoints').textContent = statusData.state;
        
        // Get PWM scale
        const pwmResp = await makeRequest('/number/pwm_scale');
        const pwmData = JSON.parse(pwmResp);
        document.getElementById('pwmScale').value = pwmData.value;
        document.getElementById('pwmScaleValue').textContent = pwmData.value + '%';
        
    } catch (error) {
        console.error('Failed to update status:', error);
    }
}

// Show status message
function showStatus(message, type = '') {
    // Create a temporary status element
    const status = document.createElement('div');
    status.className = 'status ' + type;
    status.textContent = message;
    status.style.position = 'fixed';
    status.style.top = '20px';
    status.style.right = '20px';
    status.style.padding = '15px 25px';
    status.style.borderRadius = '4px';
    status.style.zIndex = '1000';
    status.style.boxShadow = '0 4px 6px rgba(0, 0, 0, 0.3)';
    
    document.body.appendChild(status);
    
    // Remove after 3 seconds
    setTimeout(() => {
        status.style.opacity = '0';
        status.style.transition = 'opacity 0.3s';
        setTimeout(() => document.body.removeChild(status), 300);
    }, 3000);
}

// Show JSON status
function showJsonStatus(message, type = '') {
    const status = document.getElementById('jsonStatus');
    status.style.display = 'block';
    status.className = 'status ' + type;
    status.textContent = message;
    
    // Hide after 5 seconds
    setTimeout(() => {
        status.style.display = 'none';
    }, 5000);
}

// Auto-refresh status every 30 seconds
setInterval(updateStatus, 30000);