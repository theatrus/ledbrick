// moon_phase is the position in the lunar cycle (0 new, 0.25 first quarter, 0.5 full,
// 0.75 last quarter), not how much of the moon is lit

// Share of the moon that is lit, 0-1. Firmware that predates moon_illumination only
// reports the phase.
export function moonIllumination(phase: number, illumination?: number): number {
  if (illumination !== undefined && illumination !== null) {
    return illumination;
  }
  return (1 - Math.cos(2 * Math.PI * phase)) / 2;
}

// Same names and boundaries as the Home Assistant "Moon Phase" sensor
export function moonPhaseName(phase: number): string {
  if (phase < 0.0625) return 'new moon';
  if (phase < 0.1875) return 'waxing crescent';
  if (phase < 0.3125) return 'first quarter';
  if (phase < 0.4375) return 'waxing gibbous';
  if (phase < 0.5625) return 'full moon';
  if (phase < 0.6875) return 'waning gibbous';
  if (phase < 0.8125) return 'last quarter';
  if (phase < 0.9375) return 'waning crescent';
  return 'new moon';
}

export function formatMoon(phase: number, illumination?: number): string {
  const lit = Math.round(moonIllumination(phase, illumination) * 100);
  return `${lit}% lit, ${moonPhaseName(phase)}`;
}
