/** The web app's look, kept on the device (GET/POST /api/settings) so every browser shows the same. */
export interface Appearance {
  theme: 'system' | 'light' | 'dark';
  accent: 'green' | 'blue' | 'teal' | 'purple' | 'orange';
  text_size: 'normal' | 'large' | 'xlarge';
  temp_unit: 'F' | 'C';
  time_format: '24' | '12';
  chart_window: '6' | '24' | 'all';
  tile_details: boolean;
}

export type TimeFormat = Appearance['time_format'];
export type TempUnit = Appearance['temp_unit'];

export const DEFAULT_APPEARANCE: Appearance = {
  theme: 'system',
  accent: 'green',
  text_size: 'normal',
  temp_unit: 'F',
  time_format: '24',
  chart_window: '24',
  tile_details: true,
};

export const APPEARANCE_CHOICES = {
  theme: ['system', 'light', 'dark'],
  accent: ['green', 'blue', 'teal', 'purple', 'orange'],
  text_size: ['normal', 'large', 'xlarge'],
  temp_unit: ['F', 'C'],
  time_format: ['24', '12'],
  chart_window: ['6', '24', 'all'],
} as const;

type ChoiceKey = keyof typeof APPEARANCE_CHOICES;

/** The device's settings reply as an Appearance; a missing or unknown value keeps its default. */
export function appearanceFrom(settings: Record<string, unknown> | null | undefined): Appearance {
  const a: Appearance = { ...DEFAULT_APPEARANCE };
  if (!settings) return a;
  for (const key of Object.keys(APPEARANCE_CHOICES) as ChoiceKey[]) {
    const v = settings[key];
    if (typeof v === 'string' && (APPEARANCE_CHOICES[key] as readonly string[]).includes(v)) {
      (a as unknown as Record<string, string>)[key] = v;
    }
  }
  if (typeof settings.tile_details === 'boolean') a.tile_details = settings.tile_details;
  return a;
}

/** The device logs °F; °C is display only. */
export function displayTemp(f: number | null, unit: TempUnit): number | null {
  if (f === null || Number.isNaN(f)) return null;
  return unit === 'C' ? ((f - 32) * 5) / 9 : f;
}

/** A temperature change (per hour, spread) converts without the offset. */
export function displayTempRate(f: number | null, unit: TempUnit): number | null {
  if (f === null || Number.isNaN(f)) return null;
  return unit === 'C' ? (f * 5) / 9 : f;
}

export function tempUnitLabel(unit: TempUnit): string {
  return unit === 'C' ? '°C' : '°F';
}

export function chartHours(window: Appearance['chart_window']): number | null {
  return window === 'all' ? null : Number(window);
}

/** Attributes for <html>: data-theme only when chosen, so "system" follows prefers-color-scheme. */
export function rootAttributes(a: Appearance): { theme: 'light' | 'dark' | null; accent: string; text: string } {
  return { theme: a.theme === 'system' ? null : a.theme, accent: a.accent, text: a.text_size };
}

export function applyToRoot(a: Appearance, root: HTMLElement = document.documentElement): void {
  const attrs = rootAttributes(a);
  if (attrs.theme) root.dataset.theme = attrs.theme;
  else delete root.dataset.theme;
  root.dataset.accent = attrs.accent;
  root.dataset.text = attrs.text;
}

const CACHE_KEY = 'envmonitor-appearance';

/** The last appearance this browser saw, applied before the device answers so the page doesn't flash. */
export function cachedAppearance(): Appearance | null {
  try {
    const text = localStorage.getItem(CACHE_KEY);
    return text ? appearanceFrom(JSON.parse(text)) : null;
  } catch {
    return null;
  }
}

export function cacheAppearance(a: Appearance): void {
  try {
    localStorage.setItem(CACHE_KEY, JSON.stringify(a));
  } catch {
    // private window or storage blocked: the device copy still applies
  }
}
