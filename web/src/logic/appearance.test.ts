import { describe, expect, it } from 'vitest';
import {
  DEFAULT_APPEARANCE,
  appearanceFrom,
  chartHours,
  displayTemp,
  displayTempRate,
  rootAttributes,
  tempUnitLabel,
} from './appearance';
import { clock, localTime, reportLocalTime } from './format';
import { nextReading } from './dashboard';

describe('appearance', () => {
  it('defaults to the system theme in green with deg F and 24 h time', () => {
    expect(appearanceFrom(null)).toEqual(DEFAULT_APPEARANCE);
    expect(DEFAULT_APPEARANCE).toEqual({
      theme: 'system', accent: 'green', text_size: 'normal', temp_unit: 'F', time_format: '24', chart_window: '24', tile_details: true,
    });
  });

  it('takes valid device settings and keeps defaults for missing or unknown values', () => {
    const a = appearanceFrom({ theme: 'dark', accent: 'pink', temp_unit: 'C', tile_details: false, interval_min: 10 });
    expect(a.theme).toBe('dark');
    expect(a.accent).toBe('green');
    expect(a.temp_unit).toBe('C');
    expect(a.tile_details).toBe(false);
  });

  it('converts temperatures for display only', () => {
    expect(displayTemp(77, 'F')).toBe(77);
    expect(displayTemp(77, 'C')).toBeCloseTo(25);
    expect(displayTemp(null, 'C')).toBeNull();
    expect(displayTempRate(0.9, 'C')).toBeCloseTo(0.5);  // a rate has no offset
    expect(displayTempRate(null, 'C')).toBeNull();
    expect(tempUnitLabel('C')).toBe('°C');
    expect(tempUnitLabel('F')).toBe('°F');
  });

  it('maps the chart window setting to hours', () => {
    expect(chartHours('6')).toBe(6);
    expect(chartHours('24')).toBe(24);
    expect(chartHours('all')).toBeNull();
  });

  it('sets html attributes, leaving the theme to the system unless chosen', () => {
    expect(rootAttributes(DEFAULT_APPEARANCE)).toEqual({ theme: null, accent: 'green', text: 'normal' });
    expect(rootAttributes({ ...DEFAULT_APPEARANCE, theme: 'light', accent: 'teal', text_size: 'xlarge' })).toEqual({
      theme: 'light', accent: 'teal', text: 'xlarge',
    });
  });

  it('formats times in 24 h or 12 h', () => {
    expect(clock(new Date(2026, 10, 16, 14, 5))).toBe('14:05');
    expect(clock(new Date(2026, 10, 16, 14, 5), '12')).toBe('2:05 PM');
    expect(clock(new Date(2026, 10, 16, 0, 7), '12')).toBe('12:07 AM');
    expect(clock(new Date(2026, 10, 16, 12, 0), '12')).toBe('12:00 PM');
    expect(localTime('2026-11-16T14:05:09')).toBe('2026-11-16 14:05');
    expect(localTime('2026-11-16T14:05:09', '12')).toBe('2026-11-16 2:05 PM');
    expect(reportLocalTime(new Date(2026, 10, 16, 9, 30).toISOString(), '12')).toBe('2026-11-16 9:30 AM');
    expect(nextReading('2026-11-16T14:05:00', 10, '12')).toBe('2:15 PM');
  });
});
