import { useCallback, useEffect, useRef, useState } from 'preact/hooks';
import type { ApiError } from './api';
import { createPoller } from './poller';

export function usePoll<T>(fn: () => Promise<T>, intervalMs: number) {
  const [state, setState] = useState<{ data: T | null; error: ApiError | null; at: number | null }>({
    data: null,
    error: null,
    at: null,
  });
  const fnRef = useRef(fn);
  fnRef.current = fn;
  const pollerRef = useRef<{ refresh(): void; stop(): void } | null>(null);

  // A new intervalMs restarts the poller; the hook state keeps the last good data across the restart.
  useEffect(() => {
    const p = createPoller(
      () => fnRef.current(),
      intervalMs,
      (s) => setState((prev) => ({ data: s.data ?? prev.data, error: s.error, at: s.at ?? prev.at })),
    );
    pollerRef.current = p;
    return () => p.stop();
  }, [intervalMs]);

  const refresh = useCallback(() => pollerRef.current?.refresh(), []);
  return { ...state, refresh };
}
