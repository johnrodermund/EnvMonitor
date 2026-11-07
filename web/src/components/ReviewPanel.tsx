import type { PiEntry, PiReportBody } from '../types';

function evidenceText(e: unknown): string {
  if (e === null || e === undefined) return '';
  if (typeof e === 'string') return e;
  try {
    return JSON.stringify(e);
  } catch {
    return '';
  }
}

export function sourceText(r: PiReportBody): string {
  if (r.source === 'model' && r.model) {
    return r.duration_s === null ? r.model : `${r.model} · ${Math.round(r.duration_s)} s`;
  }
  return 'template';
}

function reportTime(iso: string): string {
  const m = /^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2})/.exec(iso);
  return m ? `${m[1]} ${m[2]} UTC` : iso;
}

export function ReviewPanel({ report }: { report: PiEntry<PiReportBody> | null }) {
  if (!report || report.none || !report.body) {
    return (
      <section class="review">
        <h2>Latest review</h2>
        <p class="muted">{report?.error ? `No Pi data yet · ${report.error}` : 'No Pi data yet'}</p>
      </section>
    );
  }
  const r = report.body;
  return (
    <section class="review">
      <h2>Latest review</h2>
      <div class="review-head">
        <span class={`badge sev-${r.severity}`}>{r.severity}</span>
        <strong>{r.headline}</strong>
      </div>
      <p>{r.summary}</p>
      {r.flags.length > 0 && (
        <ul class="flags">
          {r.flags.map((f, i) => (
            <li key={`${f.id}${i}`}>
              <code>{f.id}</code> {evidenceText(f.evidence)}
            </li>
          ))}
        </ul>
      )}
      <div class="muted small">
        {reportTime(r.created_at)} · {sourceText(r)}
      </div>
    </section>
  );
}
