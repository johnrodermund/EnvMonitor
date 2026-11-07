import { Progress } from '../components/Progress';
import { useWizard } from './useWizard';

const num = (v: number | null | undefined, d: number) => (v === null || v === undefined ? '—' : v.toFixed(d));

export function Measure() {
  const { w, data, msg, sending, offline, busy, begin, next, cancel, reset } = useWizard('measure');
  if (!data || !w) return <p class="muted">{offline ? 'Device not responding' : 'Loading…'}</p>;
  const lock = sending || busy || offline;

  return (
    <div>
      <h1>Measure</h1>
      <p class="muted">A check reading that is not saved to the log.</p>
      <Progress status={data} />
      {w.step === 'intro' && (
        <>
          <p>Put the probes in the solution you want to check, then press Start.</p>
          <div class="actions">
            <button class="primary" disabled={lock} onClick={() => begin('measure_start')}>Start</button>
          </div>
        </>
      )}
      {w.step === 'starting' && <p>Pausing regular readings…</p>}
      {w.step === 'ready' && (
        <>
          <p>Probes in the solution? Press Measure to take the reading.</p>
          <div class="actions">
            <button class="primary" disabled={lock} onClick={() => next('measure')}>Measure</button>
          </div>
        </>
      )}
      {w.step === 'measuring' && <p>Measuring, keep the probes still.</p>}
      {w.step === 'done' && (
        <>
          <div class="cards">
            <div class="card"><div class="card-label">pH</div><div class="card-value">{num(data.manual.ph, 2)}</div></div>
            <div class="card"><div class="card-label">EC <span class="unit">µS/cm</span></div><div class="card-value">{num(data.manual.ec, 0)}</div></div>
            <div class="card"><div class="card-label">Temperature <span class="unit">°F</span></div><div class="card-value">{num(data.manual.wt, 1)}</div></div>
          </div>
          <div class="actions">
            <button class="primary" onClick={reset}>Done</button>
          </div>
        </>
      )}
      {w.step === 'cancelled' && (
        <>
          <p class="err">{w.message ?? 'Measurement cancelled'}</p>
          <div class="actions">
            <button onClick={reset}>Start again</button>
          </div>
        </>
      )}
      {(w.step === 'starting' || w.step === 'ready' || w.step === 'measuring') && (
        <div class="actions">
          <button disabled={sending || offline} onClick={cancel}>Cancel</button>
        </div>
      )}
      {msg && <p class="err small">{msg}</p>}
    </div>
  );
}
