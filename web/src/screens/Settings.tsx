import { useContext, useEffect, useState } from 'preact/hooks';
import {
  ApiError,
  command,
  getApiConfig,
  getRanges,
  getSettings,
  resetRanges,
  saveApiConfig,
  saveRanges,
  saveSettings,
} from '../api';
import { RangesTable } from '../components/RangesTable';
import {
  type MetricKey,
  type RangesText,
  parseRangesForm,
  rangesToText,
  validateInterval,
  validateRangesForm,
  validateTitle,
} from '../logic/rangesForm';
import { BUILTIN_RANGES } from '../logic/status';
import { StatusContext } from '../status-context';
import type { RangesResponse } from '../types';

const SOURCE = { device: 'Device', pi: 'Pi', builtin: 'Built-in' };
const why = (e: unknown) => (e instanceof ApiError ? e.message : 'Request failed');
const val = (e: Event) => (e.target as HTMLInputElement).value;

export function Settings() {
  const { data: status, refresh } = useContext(StatusContext);
  const [interval, setIntervalText] = useState('');
  const [title, setTitle] = useState('');
  const [ranges, setRanges] = useState<RangesResponse | null>(null);
  const [rText, setRText] = useState<RangesText>(rangesToText(BUILTIN_RANGES));
  const [url, setUrl] = useState('');
  const [device, setDevice] = useState('');
  const [token, setToken] = useState('');
  const [tokenSet, setTokenSet] = useState<boolean | null>(null);
  const [errs, setErrs] = useState<Record<string, string | null>>({});
  const [done, setDone] = useState<Record<string, string | null>>({});
  const [resetOpen, setResetOpen] = useState(false);
  const [typed, setTyped] = useState('');

  const note = (k: string, err: string | null, ok?: string) => {
    setErrs((p) => ({ ...p, [k]: err }));
    setDone((p) => ({ ...p, [k]: err ? null : (ok ?? null) }));
  };
  const run = async (k: string, fn: () => Promise<void>, ok = 'Saved') => {
    try {
      await fn();
      note(k, null, ok);
    } catch (e) {
      note(k, why(e));
    }
  };

  const loadRanges = async () => {
    const r = await getRanges();
    setRanges(r);
    setRText(rangesToText(r.ranges));
    return r;
  };
  const loadApi = async () => {
    const c = await getApiConfig();
    setUrl(c.url);
    setDevice(c.device);
    setTokenSet(c.token_set);
  };
  useEffect(() => {
    getSettings().then(
      (s) => {
        setIntervalText(String(s.interval_min));
        setTitle(s.title);
      },
      () => {},
    );
    loadRanges().catch(() => {});
    loadApi().catch(() => {});
  }, []);

  const field = (k: string) =>
    errs[k] ? (
      <p class="err small" role="alert">
        {errs[k]}
      </p>
    ) : done[k] ? (
      <p class="muted small">{done[k]}</p>
    ) : null;

  const saveInterval = () => {
    const e = validateInterval(interval);
    if (e) return note('interval', e);
    return run('interval', async () => {
      await saveSettings({ interval: Number(interval) });
      refresh();
    });
  };
  const saveTitle = () => {
    const e = validateTitle(title);
    if (e) return note('title', e);
    return run('title', async () => {
      await saveSettings({ title });
      refresh();
    });
  };
  const saveDeviceRanges = () => {
    const parsed = parseRangesForm(rText);
    const e = validateRangesForm(parsed);
    if (e) return note('ranges', e);
    return run(
      'ranges',
      async () => {
        await saveRanges(parsed);
        await loadRanges();
      },
      'Saved as device ranges',
    );
  };
  const usePi = async () => {
    try {
      await resetRanges();
      const r = await loadRanges();
      note(
        'ranges',
        null,
        r.source === 'pi' ? 'Now using Pi ranges' : 'Now using built-in ranges (no Pi ranges available)',
      );
    } catch (e) {
      note('ranges', why(e));
    }
  };
  const savePi = () =>
    run('api', async () => {
      // An empty token field means keep the stored token, so send none.
      const t = token;
      await saveApiConfig({ url: url.trim(), device: device.trim(), ...(t ? { token: t } : {}) });
      setToken('');
      await loadApi();
    });
  const restart = () => {
    if (confirm('Restart the device now? A reading in progress is lost and any calibration is discarded.'))
      run('power', () => command('restart'), 'Restarting…');
  };
  const doReset = () => {
    setResetOpen(false);
    setTyped('');
    run('power', () => command('reset'), 'Reset requested');
  };

  const api = status?.api;
  const upload = api
    ? `${api.delivered ?? 0} delivered, ${api.queued ?? 0} queued, ${api.failures ?? 0} failures, last HTTP ${api.last_http || 'none'}`
    : 'Unknown';

  return (
    <div>
      <h1>Settings</h1>

      <section class="panel">
        <h2>Reading interval</h2>
        <div class="row">
          <label>
            Minutes (2–720)
            <input type="text" inputMode="numeric" value={interval} onInput={(e) => setIntervalText(val(e))} />
          </label>
          <button class="primary" onClick={saveInterval}>
            Save
          </button>
        </div>
        {field('interval')}
      </section>

      <section class="panel">
        <h2>Title</h2>
        <div class="row">
          <label>
            Dashboard title
            <input type="text" value={title} onInput={(e) => setTitle(val(e))} />
          </label>
          <button class="primary" onClick={saveTitle}>
            Save
          </button>
        </div>
        {field('title')}
      </section>

      <section class="panel">
        <h2>Ranges</h2>
        <p class="muted small">
          Source in use: <strong>{ranges ? SOURCE[ranges.source] : '…'}</strong>
        </p>
        <RangesTable
          text={rText}
          pi={ranges?.pi ?? null}
          onChange={(k: MetricKey, i, v) => setRText((p) => ({ ...p, [k]: p[k].map((x, j) => (j === i ? v : x)) }))}
        />
        <div class="actions">
          <button class="primary" onClick={saveDeviceRanges}>
            Save as device ranges
          </button>
          <button disabled={ranges?.source !== 'device'} onClick={usePi}>
            Use Pi ranges
          </button>
        </div>
        {field('ranges')}
      </section>

      <section class="panel">
        <h2>Pi connection</h2>
        <label>
          URL
          <input type="text" inputMode="url" placeholder="http://192.168.12.230:8000" value={url} onInput={(e) => setUrl(val(e))} />
        </label>
        <label>
          Device name
          <input type="text" value={device} onInput={(e) => setDevice(val(e))} />
        </label>
        <label>
          Token: {tokenSet === null ? '…' : tokenSet ? 'set' : 'not set'}
          <input
            type="password"
            autocomplete="new-password"
            placeholder="Leave empty to keep the current token"
            value={token}
            onInput={(e) => setToken(val(e))}
          />
        </label>
        <div class="actions">
          <button class="primary" onClick={savePi}>
            Save
          </button>
        </div>
        {field('api')}
        <p class="muted small">Upload: {upload}</p>
      </section>

      <section class="panel">
        <h2>Device</h2>
        <div class="actions">
          <button onClick={restart}>Restart</button>
          <button class="danger" onClick={() => setResetOpen(true)}>
            Reset data…
          </button>
        </div>
        {field('power')}
      </section>

      {resetOpen && (
        <div class="modal" role="dialog" aria-modal="true" aria-labelledby="reset-h">
          <div class="modal-box">
            <h2 id="reset-h">Reset data</h2>
            <p>This erases all logged readings and restarts the device. Calibration and settings are kept. It cannot be undone. Type RESET to confirm.</p>
            <input type="text" aria-label="Type RESET" autocomplete="off" value={typed} onInput={(e) => setTyped(val(e))} />
            <div class="actions">
              <button class="danger" disabled={typed !== 'RESET'} onClick={doReset}>
                Reset
              </button>
              <button
                onClick={() => {
                  setResetOpen(false);
                  setTyped('');
                }}
              >
                Cancel
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
