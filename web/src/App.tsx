import type { ComponentType } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { getStatus } from './api';
import { StatusContext } from './status-context';
import { usePoll } from './usePoll';
import { Calibrate } from './screens/Calibrate';
import { Charts } from './screens/Charts';
import { Dashboard } from './screens/Dashboard';
import { Diagnostics } from './screens/Diagnostics';
import { Measure } from './screens/Measure';
import { Settings } from './screens/Settings';

const ROUTES: { path: string; label: string; view: ComponentType }[] = [
  { path: '/', label: 'Dashboard', view: Dashboard },
  { path: '/charts', label: 'Charts', view: Charts },
  { path: '/measure', label: 'Measure', view: Measure },
  { path: '/calibrate', label: 'Calibrate', view: Calibrate },
  { path: '/settings', label: 'Settings', view: Settings },
  { path: '/diag', label: 'Diag', view: Diagnostics },
];

function currentPath(): string {
  const p = location.hash.replace(/^#/, '') || '/';
  return ROUTES.some((r) => r.path === p) ? p : '/';
}

export function App() {
  const [path, setPath] = useState(currentPath());
  useEffect(() => {
    const on = () => setPath(currentPath());
    window.addEventListener('hashchange', on);
    return () => window.removeEventListener('hashchange', on);
  }, []);

  const [fast, setFast] = useState(false);
  const status = usePoll(getStatus, fast ? 2000 : 15000);
  const isBusy = status.data?.cycle.state === 'busy';
  useEffect(() => setFast(isBusy), [isBusy]);

  const View = (ROUTES.find((r) => r.path === path) ?? ROUTES[0]).view;
  return (
    <StatusContext.Provider value={status}>
      <div class="shell">
        <nav class="nav" aria-label="Main">
          {ROUTES.map((r) => (
            <a key={r.path} href={`#${r.path}`} class={r.path === path ? 'active' : ''} aria-current={r.path === path ? 'page' : undefined}>
              {r.label}
            </a>
          ))}
        </nav>
        <main class="main">
          {status.error && (
            <div class="banner" role="alert">
              Device not responding, retrying
            </div>
          )}
          <View />
        </main>
      </div>
    </StatusContext.Provider>
  );
}
