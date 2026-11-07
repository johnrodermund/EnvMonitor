import type { MetricKey, RangesText } from '../logic/rangesForm';
import type { MetricRange, Ranges } from '../types';

const ROWS: { key: MetricKey; label: string }[] = [
  { key: 'ph', label: 'pH' },
  { key: 'ec', label: 'EC' },
  { key: 'temp', label: 'Temp °F' },
];
const FIELDS = ['Valid low', 'Valid high', 'Target low', 'Target high'];

const pair = (p: [number, number]) => `${p[0]} – ${p[1]}`;
const piText = (m: MetricRange) => (
  <>
    Valid {pair(m.valid)}
    <br />
    Target {pair(m.target)}
  </>
);

export function RangesTable({
  text,
  pi,
  onChange,
}: {
  text: RangesText;
  pi: Ranges | null;
  onChange: (k: MetricKey, i: number, v: string) => void;
}) {
  return (
    <table class="ranges-table">
      <thead>
        <tr>
          <th></th>
          <th>This device</th>
          <th>Pi</th>
        </tr>
      </thead>
      <tbody>
        {ROWS.map((r) => (
          <tr key={r.key}>
            <th scope="row">{r.label}</th>
            <td>
              <div class="range-inputs">
                {FIELDS.map((f, i) => (
                  <input
                    key={f}
                    type="text"
                    inputMode="decimal"
                    aria-label={`${r.label} ${f.toLowerCase()}`}
                    placeholder={f}
                    value={text[r.key][i]}
                    onInput={(e) => onChange(r.key, i, (e.target as HTMLInputElement).value)}
                  />
                ))}
              </div>
            </td>
            <td class="small muted">{pi ? piText(pi[r.key]) : 'No Pi values'}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
