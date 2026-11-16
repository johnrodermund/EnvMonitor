import { useContext, useState } from 'preact/hooks';
import { ApiError, command, getPi, getRanges } from '../api';
import { MetricCard } from '../components/MetricCard';
import { Progress } from '../components/Progress';
import { ReviewPanel } from '../components/ReviewPanel';
import { clock, hasReadings, nextReading } from '../logic/dashboard';
import { fmt, localTime, ecUnit } from '../logic/format';
import { BUILTIN_RANGES, levelFor, tagsFor } from '../logic/status';
import { StatusContext } from '../status-context';
import { usePoll } from '../usePoll';
import type { MetricStats } from '../types';
import { AppearanceContext } from '../appearance-context';
import { displayTemp, displayTempRate, tempUnitLabel } from '../logic/appearance';

export function Dashboard() {
  const { data: status, error, at, refresh } = useContext(StatusContext);
  const { appearance } = useContext(AppearanceContext);
  const unit = appearance.temp_unit;
  const tf = appearance.time_format;
  const pi = usePoll(getPi, 60000);
  const rangesPoll = usePoll(getRanges, 60000);
  const [msg, setMsg] = useState<string | null>(null);
  const [sending, setSending] = useState(false);

  if (!status) {
    return <p class="muted">{error ? 'Device not responding' : 'Loading…'}</p>;
  }

  const ranges = rangesPoll.data?.ranges ?? BUILTIN_RANGES;
  const last = status.last_reading;
  const piBody = pi.data?.stats.body ?? null;
  const flags = piBody?.flags ?? [];
  const busy = status.cycle.state !== 'idle';
  const stat = (k: 'ph' | 'ec' | 'water_temp_f'): MetricStats | null => piBody?.stats[k] ?? null;
  const noReadings = !hasReadings(status);

  const take = async () => {
    setSending(true);
    setMsg(null);
    try {
      await command('reading');
    } catch (e) {
      setMsg(e instanceof ApiError ? e.message : 'Request failed');
    }
    setSending(false);
    refresh();
  };

  const resume = async () => {
    setSending(true);
    setMsg(null);
    try {
      await command('cancel');
    } catch (e) {
      setMsg(e instanceof ApiError ? e.message : 'Request failed');
    }
    setSending(false);
    refresh();
  };

  const cards = [
    { key: 'ph' as const, label: 'pH', unit: '', slopeUnit: 'pH', digits: 2, value: last?.ph ?? null, avg: status.averages.ph, st: stat('ph'), range: ranges.ph },
    { key: 'ec' as const, label: 'EC', unit: ecUnit(status.ec_temp_coef), slopeUnit: 'µS/cm', digits: 0, value: last?.ec ?? null, avg: status.averages.ec, st: stat('ec'), range: ranges.ec },
    // Levels compare the logged °F with the °F ranges; only the shown numbers convert
    { key: 'temp' as const, label: 'Temperature', unit: tempUnitLabel(unit), slopeUnit: tempUnitLabel(unit), digits: 1, value: last?.wt ?? null, avg: status.averages.wt, st: stat('water_temp_f'), range: ranges.temp },
  ];

  return (
    <div class={error ? 'greyed' : ''}>
      <h1>{status.title || 'EnvMonitor'}</h1>
      <p class="statusline">
        Last reading {localTime(last?.time ?? null, tf)} · next {nextReading(last?.time ?? null, status.interval_min, tf)}
        {error && at ? ` · as of ${clock(new Date(at), tf)}` : ''}
      </p>
      <Progress status={status} />
      {noReadings ? (
        <p class="empty">No readings yet</p>
      ) : (
        <div class="cards">
          {cards.map((c) => (
            <MetricCard
              key={c.key}
              label={c.label}
              unit={c.unit}
              slopeUnit={c.slopeUnit}
              digits={c.digits}
              value={c.key === 'temp' ? displayTemp(c.value, unit) : c.value}
              mean={c.key === 'temp' ? displayTemp(c.avg[0], unit) : c.avg[0]}
              spread={c.key === 'temp' ? displayTempRate(c.avg[1], unit) : c.avg[1]}
              slopePerHour={c.key === 'temp' ? displayTempRate(c.st?.slope_per_hour ?? null, unit) : (c.st?.slope_per_hour ?? null)}
              details={appearance.tile_details}
              level={levelFor(c.value, c.range)}
              tags={tagsFor(c.key, flags)}
              extra={c.key === 'ph' && last?.ph_pm != null ? `± ${fmt(last.ph_pm, 2)}` : null}
            />
          ))}
        </div>
      )}
      <ReviewPanel report={pi.data?.report ?? null} />
      <div class="actions">
        <button class="primary" disabled={busy || sending || !!error} onClick={take}>
          Take reading now
        </button>
        {status.cycle.state === 'holding' && (
          <button disabled={sending || !!error} onClick={resume}>
            Resume readings
          </button>
        )}
        {busy && <span class="muted small">Device is busy</span>}
        {msg && <span class="err small">{msg}</span>}
      </div>
    </div>
  );
}
