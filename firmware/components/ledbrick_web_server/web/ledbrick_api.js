// LEDBrick API Client
// Uses the REST API endpoints provided by the ledbrick_api component

class LEDBrickAPI {
    constructor(baseUrl = '') {
        this.baseUrl = baseUrl || window.location.origin;
        this.authHeader = null;
    }
    
    // Set authentication credentials
    setAuth(username, password) {
        if (username && password) {
            this.authHeader = 'Basic ' + btoa(username + ':' + password);
        } else {
            this.authHeader = null;
        }
    }
    
    // Make API request
    async request(method, path, body = null) {
        const options = {
            method: method,
            headers: {
                'Content-Type': 'application/json'
            }
        };
        
        if (this.authHeader) {
            options.headers['Authorization'] = this.authHeader;
        }
        
        if (body) {
            options.body = JSON.stringify(body);
        }
        
        const response = await fetch(this.baseUrl + path, options);
        const text = await response.text();
        
        if (!response.ok) {
            let error;
            try {
                error = JSON.parse(text);
            } catch (e) {
                error = { error: text, code: response.status };
            }
            throw error;
        }
        
        try {
            return JSON.parse(text);
        } catch (e) {
            return text;
        }
    }
    
    // Get current schedule as JSON
    async getSchedule() {
        return this.request('GET', '/api/schedule');
    }
    
    // Import schedule from JSON
    async importSchedule(scheduleJson) {
        // If it's an object, stringify it
        if (typeof scheduleJson === 'object') {
            scheduleJson = JSON.stringify(scheduleJson);
        }
        // Send as raw JSON string in body
        const options = {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: scheduleJson
        };
        
        if (this.authHeader) {
            options.headers['Authorization'] = this.authHeader;
        }
        
        const response = await fetch(this.baseUrl + '/api/schedule', options);
        return response.json();
    }
    
    // Get available presets
    async getPresets() {
        return this.request('GET', '/api/presets');
    }
    
    // Load a preset
    async loadPreset(presetName) {
        return this.request('POST', `/api/presets/${presetName}`);
    }
    
    // Get current status
    async getStatus() {
        return this.request('GET', '/api/status');
    }
    
    // Clear schedule
    async clearSchedule() {
        return this.request('POST', '/api/schedule/clear');
    }
    
    // Add schedule point
    async addSchedulePoint(timeMinutes, pwmValues, currentValues = null) {
        const data = {
            time_minutes: timeMinutes,
            pwm_values: pwmValues,
            current_values: currentValues || new Array(8).fill(2.0)
        };
        return this.request('POST', '/api/schedule/point', data);
    }
    
    // Control scheduler enable/disable (using our custom endpoints)
    async setSchedulerEnabled(enabled) {
        const action = enabled ? 'turn_on' : 'turn_off';
        return this.request('POST', `/switch/web_scheduler_enable/${action}`);
    }
    
    // Get scheduler state
    async getSchedulerEnabled() {
        const response = await this.request('GET', '/switch/web_scheduler_enable');
        return response.state === 'ON';
    }
    
    // Set PWM scale
    async setPWMScale(value) {
        // Use URL-encoded form data for ESPHome endpoints
        const params = new URLSearchParams();
        params.append('value', value);
        
        const options = {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            body: params
        };
        
        if (this.authHeader) {
            options.headers['Authorization'] = this.authHeader;
        }
        
        const response = await fetch(this.baseUrl + '/number/pwm_scale/set', options);
        return response.text();
    }
    
    // Get PWM scale
    async getPWMScale() {
        const response = await this.request('GET', '/number/pwm_scale');
        return response.value;
    }
}

// Create global instance
const ledbrickAPI = new LEDBrickAPI();

// Updated functions for the HTML interface
async function testConnection() {
    const status = document.getElementById('connectionStatus');
    status.style.display = 'block';
    status.className = 'status';
    status.textContent = 'Testing connection...';
    
    // Save settings
    const deviceUrl = document.getElementById('deviceUrl').value;
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    
    localStorage.setItem('deviceUrl', deviceUrl);
    ledbrickAPI.baseUrl = deviceUrl;
    ledbrickAPI.setAuth(username, password);
    
    try {
        const result = await ledbrickAPI.getStatus();
        status.className = 'status success';
        status.textContent = `Connected! Schedule has ${result.schedule_points} points.`;
        updateStatus();
        return true; // Return success
    } catch (error) {
        status.className = 'status error';
        status.textContent = 'Connection failed: ' + (error.error || error.message || 'Unknown error');
        throw error; // Re-throw to handle in caller
    }
}

async function toggleScheduler() {
    try {
        const currentState = await ledbrickAPI.getSchedulerEnabled();
        await ledbrickAPI.setSchedulerEnabled(!currentState);
        showStatus('Scheduler ' + (currentState ? 'disabled' : 'enabled'), 'success');
        updateStatus();
    } catch (error) {
        showStatus('Failed to toggle scheduler: ' + error.error, 'error');
    }
}

async function setPWMScale(value) {
    try {
        await ledbrickAPI.setPWMScale(value);
        document.getElementById('pwmScale').value = value;
        document.getElementById('pwmScaleValue').textContent = value + '%';
        showStatus(`Brightness set to ${value}%`, 'success');
    } catch (error) {
        showStatus('Failed to set brightness: ' + error.error, 'error');
    }
}

async function loadPreset(presetName) {
    try {
        const result = await ledbrickAPI.loadPreset(presetName);
        showStatus(result.message || `Loaded preset: ${presetName}`, 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to load preset: ' + error.error, 'error');
    }
}

async function clearSchedule() {
    if (!confirm('Are you sure you want to clear the schedule?')) {
        return;
    }
    
    try {
        const result = await ledbrickAPI.clearSchedule();
        showStatus(result.message || 'Schedule cleared', 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to clear schedule: ' + error.error, 'error');
    }
}

async function addSchedulePoint() {
    try {
        // Get time in minutes
        const timeStr = document.getElementById('scheduleTime').value;
        const [hours, minutes] = timeStr.split(':').map(Number);
        const timeMinutes = hours * 60 + minutes;
        
        // Get channel values
        const pwmValues = [];
        for (let i = 1; i <= 4; i++) {
            pwmValues.push(parseFloat(document.getElementById(`ch${i}pwm`).value));
        }
        // Add zeros for channels 5-8
        pwmValues.push(0, 0, 0, 0);
        
        const result = await ledbrickAPI.addSchedulePoint(timeMinutes, pwmValues);
        showStatus(result.message || `Added schedule point at ${timeStr}`, 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        showStatus('Failed to add schedule point: ' + error.error, 'error');
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
        JSON.parse(jsonData);
        
        const result = await ledbrickAPI.importSchedule(jsonData);
        showJsonStatus(result.message || 'Schedule imported successfully', 'success');
        setTimeout(updateStatus, 1000);
    } catch (error) {
        if (error instanceof SyntaxError) {
            showJsonStatus('Invalid JSON format', 'error');
        } else {
            showJsonStatus('Failed to import: ' + (error.error || error.message), 'error');
        }
    }
}

async function updateStatus() {
    try {
        const status = await ledbrickAPI.getStatus();
        
        // Update all status fields
        document.getElementById('schedulerStatus').textContent = 
            status.enabled ? 'Enabled' : 'Disabled';
        document.getElementById('currentTime').textContent = 
            status.time_formatted || '--:--';
        document.getElementById('schedulePoints').textContent = 
            status.schedule_points || '0';
        
        // Update PWM scale
        const pwmScale = status.pwm_scale * 100;
        document.getElementById('pwmScale').value = pwmScale;
        document.getElementById('pwmScaleValue').textContent = pwmScale + '%';
        
    } catch (error) {
        console.error('Failed to update status:', error);
    }
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    // Load saved settings
    const savedUrl = localStorage.getItem('deviceUrl');
    if (savedUrl) {
        document.getElementById('deviceUrl').value = savedUrl;
        ledbrickAPI.baseUrl = savedUrl;
    }
    
    // Channel sliders removed - using schedule-based control only
    
    // Update status after a delay
    setTimeout(updateStatus, 1000);
});