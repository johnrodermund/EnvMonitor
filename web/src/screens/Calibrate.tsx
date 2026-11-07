import { Progress } from '../components/Progress';
import { useWizard } from './useWizard';

const raw = (v: number | null | undefined) => (v === null || v === undefined ? '—' : String(Math.round(v)));

export function Calibrate() {
  const { w, data, msg, sending, offline, busy, begin, next, cancel, reset } = useWizard('calibrate');
  if (!data || !w) return <p class="muted">{offline ? 'Device not responding' : 'Loading…'}</p>;
  const lock = sending || busy || offline;

  return (
    <div>
      <h1>Calibrate</h1>
      <p class="muted">Two-point pH calibration with pH 4 and pH 7 buffer. Raw values are ADC counts.</p>
      <Progress status={data} />
      {w.step === 'intro' && (
        <>
          <p>Have pH 4 and pH 7 buffer ready, and rinse the probe. Regular readings pause while you calibrate.</p>
          <div class="actions">
            <button class="primary" disabled={lock} onClick={() => begin('calibrate_start')}>Start</button>
          </div>
        </>
      )}
      {w.step === 'starting' && <p>Pausing regular readings…</p>}
      {w.step === 'ready' && (
        <>
          <p>Step 1 of 2. Put the probe in the pH 4 buffer, wait for it to settle, then press Measure pH 4.</p>
          <div class="actions">
            <button class="primary" disabled={lock} onClick={() => next('calibrate_ph4')}>Measure pH 4</button>
          </div>
        </>
      )}
      {w.step === 'ph4' && <p>Measuring pH 4, keep the probe still.</p>}
      {w.step === 'ph4done' && (
        <>
          <p>pH 4 raw value: <strong>{raw(data.cycle.pending_ph4_adc)}</strong></p>
          <p>Step 2 of 2. Rinse the probe, put it in the pH 7 buffer, wait for it to settle, then press Measure pH 7.</p>
          <div class="actions">
            <button class="primary" disabled={lock} onClick={() => next('calibrate_ph7')}>Measure pH 7</button>
          </div>
        </>
      )}
      {w.step === 'ph7' && <p>Measuring pH 7, keep the probe still.</p>}
      {w.step === 'done' && (
        <>
          <p>Calibration saved.</p>
          <table class="cal-table">
            <thead><tr><th></th><th>Old</th><th>New</th></tr></thead>
            <tbody>
              <tr><th>pH 4 raw</th><td>{raw(w.oldPh4)}</td><td>{raw(data.calibration.ph4_adc)}</td></tr>
              <tr><th>pH 7 raw</th><td>{raw(w.oldPh7)}</td><td>{raw(data.calibration.ph7_adc)}</td></tr>
            </tbody>
          </table>
          <div class="actions">
            <button class="primary" onClick={reset}>Done</button>
          </div>
        </>
      )}
      {w.step === 'cancelled' && (
        <>
          <p class="err">{w.message ?? 'Calibration cancelled'}</p>
          <div class="actions">
            <button onClick={reset}>Start again</button>
          </div>
        </>
      )}
      {(w.step === 'starting' || w.step === 'ready' || w.step === 'ph4' || w.step === 'ph4done' || w.step === 'ph7') && (
        <div class="actions">
          <button disabled={sending || offline} onClick={cancel}>Cancel</button>
        </div>
      )}
      {msg && <p class="err small">{msg}</p>}
    </div>
  );
}
