import { useContext, useState } from 'preact/hooks';
import { ApiError, postEvent } from '../api';
import { EVENT_KINDS, eventDelta, eventLabel, fromLocalInput, kindLabel, markerStyle, rowActions, toLocalInput } from '../logic/events';
import { fmt, reportLocalTime } from '../logic/format';
import type { EventChange, EventDetail, EventKind, TankEvent } from '../types';
import { AppearanceContext } from '../appearance-context';

const why = (e: unknown) => (e instanceof ApiError ? e.message : 'Request failed');
const val = (e: Event) => (e.target as HTMLInputElement).value;

function signed(v: number | null, digits: number): string {
  const text = fmt(v, digits);
  return v !== null && v > 0 ? `+${text}` : text;
}

interface Draft {
  kind: EventKind;
  detail: EventDetail;
  time: string;
  note: string;
}

/** The log and edit form. `original` is the event being edited, absent when logging a new one. */
function EventDialog({ original, onClose, onSaved }: { original?: TankEvent; onClose: () => void; onSaved: () => void }) {
  const startTime = original ? toLocalInput(original.occurred_at) : toLocalInput(new Date().toISOString());
  const [d, setD] = useState<Draft>({
    kind: original?.kind ?? 'topup',
    detail: original?.detail ?? 'di',
    time: startTime,
    note: original?.note ?? '',
  });
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const save = async () => {
    const change: EventChange = original ? { op: 'update', seq: original.seq } : { op: 'create' };
    change.kind = d.kind;
    if (d.kind === 'topup') change.detail = d.detail;
    change.note = d.note;
    // Editing confirms a detected event; a rejected one stays rejected until the owner confirms it
    if (original?.status === 'rejected') change.status = 'rejected';
    // An unchanged time is left out: the input drops seconds, and a moved event recomputes its deltas
    if (!original || d.time !== startTime) {
      const iso = fromLocalInput(d.time);
      if (!iso) return setError('Enter a date and time');
      change.occurred_at = iso;
    }
    setBusy(true);
    try {
      await postEvent(change);
      onSaved();
      onClose();
    } catch (e) {
      setError(why(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div class="modal" role="dialog" aria-modal="true" aria-labelledby="event-h">
      <div class="modal-box panel">
        <h2 id="event-h">{original ? 'Edit event' : 'Log event'}</h2>
        <label>
          Kind
          <select value={d.kind} onChange={(e) => setD({ ...d, kind: val(e) as EventKind })}>
            {EVENT_KINDS.map((k) => (
              <option key={k} value={k}>
                {kindLabel(k)}
              </option>
            ))}
          </select>
        </label>
        {d.kind === 'topup' && (
          <label>
            Water
            <select value={d.detail} onChange={(e) => setD({ ...d, detail: val(e) as EventDetail })}>
              <option value="di">DI</option>
              <option value="tap">Tap</option>
              <option value="unknown">Unknown</option>
            </select>
          </label>
        )}
        <label>
          Time
          <input type="datetime-local" value={d.time} onInput={(e) => setD({ ...d, time: val(e) })} />
        </label>
        <label>
          Note
          <textarea maxLength={200} rows={2} value={d.note} onInput={(e) => setD({ ...d, note: val(e) })} />
        </label>
        {error && (
          <p class="err small" role="alert">
            {error}
          </p>
        )}
        <div class="actions">
          <button class="primary" disabled={busy} onClick={save}>
            Save
          </button>
          <button onClick={onClose}>Cancel</button>
        </div>
      </div>
    </div>
  );
}

/** Events newest first, with Confirm/Reject for suggestions, Delete for manual events and Edit for all. */
export function EventsTable({
  events,
  truncated,
  error,
  onChanged,
}: {
  events: TankEvent[] | null;
  truncated?: boolean;
  error: string | null;
  onChanged: () => void;
}) {
  const { appearance } = useContext(AppearanceContext);
  const [editing, setEditing] = useState<TankEvent | null>(null);
  const [logging, setLogging] = useState(false);
  const [message, setMessage] = useState<string | null>(null);

  const act = async (change: EventChange) => {
    try {
      await postEvent(change);
      setMessage(null);
      onChanged();
    } catch (e) {
      setMessage(why(e));
    }
  };
  const remove = (e: TankEvent) => {
    if (confirm(`Delete this ${eventLabel(e).toLowerCase()} event?`)) act({ op: 'delete', seq: e.seq });
  };

  return (
    <section class="panel">
      <div class="events-head">
        <h2>Events</h2>
        <button class="primary" onClick={() => setLogging(true)}>
          Log event
        </button>
      </div>
      {message && (
        <p class="err small" role="alert">
          {message}
        </p>
      )}
      {!events ? (
        <p class="muted small">{error ? `Could not load events: ${error}` : 'Loading…'}</p>
      ) : events.length === 0 ? (
        <p class="muted small">No events yet. Top-ups are detected about an hour after they happen.</p>
      ) : (
        <ul class="events">
          {events.map((e) => (
            <li key={e.seq} class={`event status-${e.status}`}>
              <div class="event-line">
                <span class="event-swatch" style={{ borderColor: `var(${markerStyle(e).color})` }} />
                <strong>{eventLabel(e)}</strong>
                <span class="muted">{reportLocalTime(e.occurred_at, appearance.time_format)}</span>
                <span class={`badge ev-${e.status}`}>{e.status}</span>
                {e.refused ? (
                  <span class="err small">refused by the Pi</span>
                ) : (
                  !e.backed_up && <span class="warn-text small">not backed up</span>
                )}
              </div>
              <div class="event-line small muted">
                <span>ΔEC {signed(eventDelta(e, 'ec'), 1)}</span>
                <span>ΔpH {signed(eventDelta(e, 'ph'), 2)}</span>
                <span>{e.source}</span>
              </div>
              {e.note && <p class="event-note small">{e.note}</p>}
              <div class="actions event-actions">
                {rowActions(e).map((a) =>
                  a === 'confirm_di' ? (
                    <button key={a} onClick={() => act({ op: 'confirm', seq: e.seq, detail: 'di' })}>Confirm DI</button>
                  ) : a === 'confirm_tap' ? (
                    <button key={a} onClick={() => act({ op: 'confirm', seq: e.seq, detail: 'tap' })}>Confirm tap</button>
                  ) : a === 'confirm' ? (
                    <button key={a} onClick={() => act({ op: 'confirm', seq: e.seq })}>Confirm</button>
                  ) : a === 'reject' ? (
                    <button key={a} onClick={() => act({ op: 'reject', seq: e.seq })}>Reject</button>
                  ) : a === 'delete' ? (
                    <button key={a} class="danger" onClick={() => remove(e)}>
                      Delete
                    </button>
                  ) : (
                    <button key={a} onClick={() => setEditing(e)}>Edit</button>
                  ),
                )}
              </div>
            </li>
          ))}
        </ul>
      )}
      {truncated && <p class="muted small">Older events are stored on the device but left out here to save memory.</p>}
      {logging && <EventDialog onClose={() => setLogging(false)} onSaved={onChanged} />}
      {editing && <EventDialog original={editing} onClose={() => setEditing(null)} onSaved={onChanged} />}
    </section>
  );
}
