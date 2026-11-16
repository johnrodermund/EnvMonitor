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
import { validateEcTempCoef } from '../logic/format';
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
  const [topupDrop, setTopupDrop] = useState('');
  const [feedingRise, setFeedingRise] = useState('');
  const [feedingOn, setFeedingOn] = useState(false);
  const [lightsOn, setLightsOn] = useState('07:00');
  const [lightsOff, setLightsOff] = useState('19:00');
  const [okEc, setOkEc] = useState('275');
  const [swingFactor, setSwingFactor] = useState('1.5');
  const [ecCoef, setEcCoef] = useState('');

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
        if (s.topup_min_drop !== undefined) setTopupDrop(String(s.topup_min_drop));
        if (s.feeding_min_rise !== undefined) setFeedingRise(String(s.feeding_min_rise));
        setFeedingOn(!!s.feeding_enabled);
        if (s.ec_temp_coef !== undefined) setEcCoef(String(s.ec_temp_coef));
        if (s.lights_on !== undefined) setLightsOn(s.lights_on);
        if (s.lights_off !== undefined) setLightsOff(s.lights_off);
        if (s.nutrients_ok_ec !== undefined) setOkEc(String(s.nutrients_ok_ec));
        if (s.swing_alert_factor !== undefined) setSwingFactor(String(s.swing_alert_factor));
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
  const saveEcCoef = () => {
    const e = validateEcTempCoef(ecCoef);
    if (e) return note('ecCoef', e);
    return run('ecCoef', async () => {
      await saveSettings({ ec_temp_coef: Number(ecCoef) });
      refresh();
    });
  };
  const saveDetection = () => {
    const drop = Number(topupDrop);
    const rise = Number(feedingRise);
    const ok = (v: number, t: string) => t.trim() !== '' && Number.isFinite(v) && v >= 0.5 && v <= 100;
    if (!ok(drop, topupDrop) || !ok(rise, feedingRise)) return note('detect', 'Enter numbers from 0.5 to 100');
    return run('detect', () => saveSettings({ topup_min_drop: drop, feeding_min_rise: rise, feeding_enabled: feedingOn }));
  };
  const saveSwing = () => {
    const clockOk = (t: string) => /^([01]\d|2[0-3]):[0-5]\d$/.test(t);
    if (!clockOk(lightsOn) || !clockOk(lightsOff)) return note('swing', 'Enter light times as HH:MM');
    if (lightsOn === lightsOff) return note('swing', 'Lights on and off must differ');
    const ec = Number(okEc);
    const factor = Number(swingFactor);
    if (okEc.trim() === '' || !Number.isFinite(ec) || ec < 0 || ec > 5000) return note('swing', 'Enter an EC from 0 to 5000');
    if (swingFactor.trim() === '' || !Number.isFinite(factor) || factor < 1.1 || factor > 5) {
      return note('swing', 'Enter an alert factor from 1.1 to 5');
    }
    return run('swing', () =>
      saveSettings({ lights_on: lightsOn, lights_off: lightsOff, nutrients_ok_ec: ec, swing_alert_factor: factor }),
    );
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
        <h2>EC temperature compensation</h2>
        <p class="muted small">
          EC is shown at 25 °C: EC₂₅ = EC ÷ (1 + coefficient × (water °C − 25)). Changing it re-applies to all readings on the device;
          readings already sent to the Pi keep the value they had. 0 shows raw EC. Run <code>tools/ec_temp_coef.py</code> on the Pi's
          data to suggest a value for your tank.
        </p>
        <div class="row">
          <label>
            Coefficient (% per °C, 0–5)
            <input type="text" inputMode="decimal" value={ecCoef} onInput={(e) => setEcCoef(val(e))} />
          </label>
          <button class="primary" onClick={saveEcCoef}>
            Save
          </button>
        </div>
        {field('ecCoef')}
      </section>

      <section class="panel">
        <h2>Event detection</h2>
        <p class="muted small">A top-up is a drop in median EC between the 6 readings before and the 6 after.</p>
        <label>
          Top-up: smallest EC drop (µS/cm)
          <input type="text" inputMode="decimal" value={topupDrop} onInput={(e) => setTopupDrop(val(e))} />
        </label>
        <label>
          Feeding: smallest EC rise (µS/cm)
          <input type="text" inputMode="decimal" value={feedingRise} onInput={(e) => setFeedingRise(val(e))} />
        </label>
        <label class="check">
          <input type="checkbox" checked={feedingOn} onChange={(e) => setFeedingOn((e.target as HTMLInputElement).checked)} />
          Detect feedings
        </label>
        <div class="actions">
          <button class="primary" onClick={saveDetection}>
            Save
          </button>
        </div>
        {field('detect')}
      </section>

      <section class="panel">
        <h2>pH swings</h2>
        <p class="muted small">
          Each day the device records the pH swing as an event. About 2 hours after each light change it compares the pH jump
          with this tank's usual and suggests buffer or nutrients when it's well above. Needs a reading interval of 6 minutes or
          more, so the 200-reading log holds a full day.
        </p>
        <label>
          Lights on
          <input type="time" value={lightsOn} onInput={(e) => setLightsOn(val(e))} />
        </label>
        <label>
          Lights off
          <input type="time" value={lightsOff} onInput={(e) => setLightsOff(val(e))} />
        </label>
        <p class="muted small">Update when the light timer changes photoperiod.</p>
        <label>
          Nutrients OK above EC (µS/cm)
          <input type="text" inputMode="decimal" value={okEc} onInput={(e) => setOkEc(val(e))} />
        </label>
        <p class="muted small">EC as shown. Above it a big swing means buffer (or a water change), below it nutrients.</p>
        <label>
          Alert factor
          <input type="text" inputMode="decimal" value={swingFactor} onInput={(e) => setSwingFactor(val(e))} />
        </label>
        <p class="muted small">1.5 = 50 % above this tank's usual.</p>
        <div class="actions">
          <button class="primary" onClick={saveSwing}>
            Save
          </button>
        </div>
        {field('swing')}
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
