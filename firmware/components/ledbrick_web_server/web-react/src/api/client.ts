import type { Schedule, Status } from '../types';

class LEDBrickAPI {
  private baseUrl: string;
  private authHeader: string | null = null;

  constructor(baseUrl = '') {
    this.baseUrl = baseUrl || window.location.origin;
  }

  setAuth(username: string, password: string) {
    if (username && password) {
      this.authHeader = 'Basic ' + btoa(username + ':' + password);
    } else {
      this.authHeader = null;
    }
  }

  private async request<T>(method: string, path: string, body?: any): Promise<T> {
    const options: RequestInit = {
      method,
      headers: {
        'Content-Type': 'application/json',
      },
    };

    if (this.authHeader) {
      (options.headers as any)['Authorization'] = this.authHeader;
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
      return text as any;
    }
  }

  async getSchedule(): Promise<Schedule> {
    return this.request('GET', '/api/schedule');
  }

  async importSchedule(scheduleJson: string | object): Promise<any> {
    const jsonStr = typeof scheduleJson === 'object' 
      ? JSON.stringify(scheduleJson) 
      : scheduleJson;

    const response = await fetch(this.baseUrl + '/api/schedule', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        ...(this.authHeader ? { 'Authorization': this.authHeader } : {}),
      },
      body: jsonStr,
    });
    return response.json();
  }

  async getStatus(): Promise<Status> {
    return this.request('GET', '/api/status');
  }

  async clearSchedule() {
    return this.request('POST', '/api/schedule/clear');
  }

  async loadPreset(presetName: string) {
    return this.request('POST', `/api/presets/${presetName}`);
  }

  async getPresets() {
    return this.request('GET', '/api/presets');
  }

  async setSchedulerEnabled(enabled: boolean) {
    const action = enabled ? 'turn_on' : 'turn_off';
    return this.request('POST', `/switch/web_scheduler_enable/${action}`);
  }

  async getSchedulerEnabled(): Promise<boolean> {
    const response = await this.request<any>('GET', '/switch/web_scheduler_enable');
    return response.state === 'ON';
  }

  async setPwmScale(value: number) {
    const params = new URLSearchParams();
    params.append('value', value.toString());

    const response = await fetch(this.baseUrl + '/number/pwm_scale/set', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        ...(this.authHeader ? { 'Authorization': this.authHeader } : {}),
      },
      body: params,
    });
    return response.text();
  }

  async getPwmScale(): Promise<number> {
    const response = await this.request<any>('GET', '/number/pwm_scale');
    return response.value;
  }

  // Alias for consistency with component usage
  async setSchedule(schedule: Schedule): Promise<any> {
    return this.importSchedule(schedule);
  }

  async setLocation(latitude: number, longitude: number): Promise<any> {
    return this.request('POST', '/api/location', { latitude, longitude });
  }

  async setTimeShift(
    astronomical_projection: boolean, 
    time_shift_hours: number, 
    time_shift_minutes: number
  ): Promise<any> {
    return this.request('POST', '/api/time_shift', {
      astronomical_projection,
      time_shift_hours,
      time_shift_minutes
    });
  }

  async setMoonSimulation(moonData: any): Promise<any> {
    return this.request('POST', '/api/moon_simulation', moonData);
  }

  async setChannelManualControl(channel: number, pwm: number, current: number): Promise<any> {
    return this.request('POST', '/api/channel/control', { channel, pwm, current });
  }

  async updateChannelConfigs(configs: Array<{name: string, rgb_hex: string, max_current: number}>): Promise<any> {
    return this.request('POST', '/api/channel/configs', { configs });
  }

  async updateLocation(latitude: number, longitude: number, timezone_offset_hours: number): Promise<any> {
    return this.request('POST', '/api/location', { latitude, longitude, timezone_offset_hours });
  }

  async updateTimeProjection(astronomical_projection: boolean, time_shift_hours: number, time_shift_minutes: number): Promise<any> {
    return this.request('POST', '/api/time_projection', { 
      astronomical_projection, 
      time_shift_hours, 
      time_shift_minutes 
    });
  }

  // Temperature control API
  async getTemperatureConfig(): Promise<any> {
    try {
      return await this.request('GET', '/api/temperature/config');
    } catch (error: any) {
      if (error.message && error.message.includes('404')) {
        return null; // Temperature control not available
      }
      throw error;
    }
  }

  async updateTemperatureConfig(config: any): Promise<any> {
    return this.request('POST', '/api/temperature/config', config);
  }

  async getTemperatureStatus(): Promise<any> {
    try {
      return await this.request('GET', '/api/temperature/status');
    } catch (error: any) {
      if (error.message && error.message.includes('404')) {
        return null; // Temperature control not available
      }
      throw error;
    }
  }

  async getFanCurve(): Promise<any> {
    try {
      return await this.request('GET', '/api/temperature/fan-curve');
    } catch (error: any) {
      if (error.message && error.message.includes('404')) {
        return null; // Temperature control not available
      }
      throw error;
    }
  }
}

export const api = new LEDBrickAPI();