import type { ChannelDimming, LedGroup, LedModel, Schedule } from '../types';

// True when the channel is driven from its LED curves: its schedule points and
// moonlight hold one level (0-100 % of the light at max current) in pwm_values /
// base_intensity, and its current values are unused
export function isCurveChannel(schedule: Schedule | null | undefined, channel: number): boolean {
  return schedule?.channel_configs?.[channel]?.dimming?.mode === 'curve';
}

export function ledModelName(id: string, models: LedModel[]): string {
  return models.find(m => m.id === id)?.name || id;
}

// e.g. "4x LUXEON C Mint + 4x LUXEON C Cyan"
export function formatLeds(leds: LedGroup[], models: LedModel[]): string {
  return leds.map(group => `${group.count}x ${ledModelName(group.model, models)}`).join(' + ');
}

// e.g. "4x LUXEON C Mint + 4x LUXEON C Cyan (emitter default)"
export function formatLedMix(dimming: Pick<ChannelDimming, 'leds' | 'leds_default'>, models: LedModel[]): string {
  if (dimming.leds.length === 0) {
    return dimming.leds_default ? 'Emitter default' : 'None';
  }
  return formatLeds(dimming.leds, models) + (dimming.leds_default ? ' (emitter default)' : '');
}

// Lowest current all of the LEDs' curves cover; the device never drives a curve
// channel below it. Null when no model is known.
export function characterizedCurrent(leds: LedGroup[], models: LedModel[]): number | null {
  let lowest: number | null = null;
  for (const group of leds) {
    const model = models.find(m => m.id === group.model);
    if (model) {
      lowest = Math.max(lowest ?? 0, model.characterized_from);
    }
  }
  return lowest;
}

// Highest current all of the LEDs are rated for; curve mode stays at or below it.
// Null when no model is known.
export function ledMaxCurrent(leds: LedGroup[], models: LedModel[]): number | null {
  let highest: number | null = null;
  for (const group of leds) {
    const model = models.find(m => m.id === group.model);
    if (model) {
      highest = Math.min(highest ?? Infinity, model.max_current);
    }
  }
  return highest;
}

// e.g. "0.105 A"
export function formatAmps(amps: number): string {
  return `${Number(amps.toFixed(3))} A`;
}

export function sameLeds(a: LedGroup[], b: LedGroup[]): boolean {
  return a.length === b.length && a.every((group, i) => group.model === b[i].model && group.count === b[i].count);
}
