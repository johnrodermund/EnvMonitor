import { useCallback, useEffect, useRef, useState } from 'preact/hooks';
import { ApiError } from './api';

export function usePoll<T>(fn: () => Promise<T>, intervalMs: number) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<ApiError | null>(null);
  const [at, setAt] = useState<number | null>(null);
  const [tick, setTick] = useState(0);
  const fnRef = useRef(fn);
  fnRef.current = fn;

  useEffect(() => {
    let alive = true;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const run = async () => {
      try {
        const d = await fnRef.current();
        if (!alive) return;
        setData(d);
        setError(null);
        setAt(Date.now());
      } catch (e) {
        if (!alive) return;
        setError(e instanceof ApiError ? e : new ApiError(0, e instanceof Error ? e.message : 'error'));
      }
      if (alive) timer = setTimeout(run, intervalMs);
    };
    run();
    return () => {
      alive = false;
      if (timer) clearTimeout(timer);
    };
  }, [intervalMs, tick]);

  const refresh = useCallback(() => setTick((n) => n + 1), []);
  return { data, error, at, refresh };
}
