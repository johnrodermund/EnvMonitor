import { useContext } from 'preact/hooks';
import { getEvents } from '../api';
import { EventsTable } from '../components/EventsTable';
import { DASH } from '../logic/format';
import { StatusContext } from '../status-context';
import { usePoll } from '../usePoll';
import type { Num } from '../types';

const n = (v: Num | undefined) => (v === null || v === undefined ? DASH : String(v));

/** Top-ups, feedings and other events, plus how their backup to the Pi is going. */
export function Events() {
  const { data: status } = useContext(StatusContext);
  const eventsPoll = usePoll(getEvents, 60000);
  const sync = status?.events;

  return (
    <div>
      <h1>Events</h1>
      <p class="statusline">Top-ups are detected about 1.5 hours after they happen. Events also show as lines on the pH and EC charts.</p>
      <EventsTable
        events={eventsPoll.data?.events ?? null}
        truncated={eventsPoll.data?.truncated}
        error={eventsPoll.error?.message ?? null}
        onChanged={eventsPoll.refresh}
      />
      {sync && (
        <section class="panel">
          <h2>Backup to the Pi</h2>
          <p>
            Stored on the device: <strong>{n(sync.count)}</strong> · Not backed up yet: <strong>{n(sync.unsynced)}</strong> · Last sync
            HTTP: <strong>{sync.last_sync_http || 'none'}</strong>
          </p>
          <p class="small">
            Lost before backup (store full): <strong>{n(sync.dropped_unsynced)}</strong> · Refused by the Pi: <strong>{n(sync.refused)}</strong>
          </p>
          {(sync.clock_ahead ?? 0) > 0 && (
            <p class="warn-text small">{sync.clock_ahead} event(s) carry a time over a day ahead and won't sync until the clock catches up.</p>
          )}
        </section>
      )}
    </div>
  );
}
