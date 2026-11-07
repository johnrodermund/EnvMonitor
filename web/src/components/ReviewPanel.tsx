import type { PiEntry, PiReportBody } from '../types';
import { reviewLine } from '../logic/dashboard';
import { reportLocalTime } from '../logic/format';

export function sourceText(r: PiReportBody): string {
  if (r.source === 'model' && r.model) {
    return r.duration_s === null ? r.model : `${r.model} · ${Math.round(r.duration_s)} s`;
  }
  return 'template';
}

export function ReviewPanel({ report }: { report: PiEntry<PiReportBody> | null }) {
  const line = reviewLine(report, Date.now());
  const r = report?.body ?? null;
  return (
    <section class="review">
      <h2>Latest review</h2>
      {r && (
        <>
          <div class="review-head">
            <span class={`badge sev-${r.severity}`}>{r.severity}</span>
            <strong>{r.headline}</strong>
          </div>
          <p>{r.summary}</p>
          {r.flags.length > 0 && (
            <ul class="flags">
              {r.flags.map((f, i) => (
                <li key={`${f.id}${i}`}>
                  <code>{f.id}</code> {typeof f.evidence === 'string' ? f.evidence : String(f.evidence ?? '')}
                </li>
              ))}
            </ul>
          )}
          <div class="muted small">
            {reportLocalTime(r.created_at)} · {sourceText(r)}
          </div>
        </>
      )}
      {line && <p class={'small ' + (r ? 'warn-text' : 'muted')}>{line}</p>}
    </section>
  );
}
