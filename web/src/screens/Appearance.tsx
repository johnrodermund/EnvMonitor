import { useContext, useState } from 'preact/hooks';
import { ApiError, saveSettings } from '../api';
import { AppearanceContext } from '../appearance-context';
import type { Appearance } from '../logic/appearance';

type Key = keyof Appearance;

const OPTIONS: { key: Key; label: string; help?: string; choices: { value: string; label: string; swatch?: string }[] }[] = [
  {
    key: 'theme',
    label: 'Theme',
    choices: [
      { value: 'system', label: 'System' },
      { value: 'light', label: 'Light' },
      { value: 'dark', label: 'Dark' },
    ],
  },
  {
    key: 'accent',
    label: 'Accent colour',
    choices: [
      { value: 'green', label: 'Green', swatch: '#2f9e66' },
      { value: 'blue', label: 'Blue', swatch: '#3b74d6' },
      { value: 'teal', label: 'Teal', swatch: '#1b9aa1' },
      { value: 'purple', label: 'Purple', swatch: '#8a5cd6' },
      { value: 'orange', label: 'Orange', swatch: '#d9691c' },
    ],
  },
  {
    key: 'text_size',
    label: 'Text size',
    choices: [
      { value: 'normal', label: 'Normal' },
      { value: 'large', label: 'Large' },
      { value: 'xlarge', label: 'Extra large' },
    ],
  },
  {
    key: 'temp_unit',
    label: 'Temperature',
    help: 'Display only: the device keeps logging °F, and ranges stay in °F.',
    choices: [
      { value: 'F', label: '°F' },
      { value: 'C', label: '°C' },
    ],
  },
  {
    key: 'time_format',
    label: 'Time',
    choices: [
      { value: '24', label: '24 hour' },
      { value: '12', label: '12 hour' },
    ],
  },
  {
    key: 'chart_window',
    label: 'Charts open on',
    choices: [
      { value: '6', label: '6 h' },
      { value: '24', label: '24 h' },
      { value: 'all', label: 'All' },
    ],
  },
  {
    key: 'tile_details',
    label: 'Dashboard tiles',
    help: 'The average and 24 h trend lines under each value.',
    choices: [
      { value: 'true', label: 'Show details' },
      { value: 'false', label: 'Values only' },
    ],
  },
];

/** Saved on the device, so every phone and browser shows the same look. Each choice saves at once. */
export function AppearanceScreen() {
  const { appearance, setAppearance } = useContext(AppearanceContext);
  const [message, setMessage] = useState<{ text: string; error: boolean } | null>(null);

  const choose = async (key: Key, value: string) => {
    const before = appearance;
    const next = { ...appearance, [key]: key === 'tile_details' ? value === 'true' : value } as Appearance;
    setAppearance(next);
    try {
      await saveSettings({ [key]: value });
      setMessage({ text: 'Saved on the device', error: false });
    } catch (e) {
      setAppearance(before);
      setMessage({ text: e instanceof ApiError ? e.message : 'Could not save', error: true });
    }
  };

  return (
    <div>
      <h1>Appearance</h1>
      <p class="statusline">Saved on the device, so every phone and browser shows the same look.</p>
      <section class="panel">
        {OPTIONS.map((o) => (
          <div key={o.key}>
            <p class="setting-label" id={`ap-${o.key}`}>
              {o.label}
            </p>
            {o.help && <p class="muted small" style={{ margin: '2px 0 0' }}>{o.help}</p>}
            <div class="choices" role="group" aria-labelledby={`ap-${o.key}`}>
              {o.choices.map((c) => (
                <button key={c.value} aria-pressed={String(appearance[o.key]) === c.value} onClick={() => choose(o.key, c.value)}>
                  {c.swatch && <span class="swatch" style={{ background: c.swatch }} />}
                  {c.label}
                </button>
              ))}
            </div>
          </div>
        ))}
        {message && (
          <p class={message.error ? 'err small' : 'muted small'} role={message.error ? 'alert' : undefined}>
            {message.text}
          </p>
        )}
      </section>
    </div>
  );
}
