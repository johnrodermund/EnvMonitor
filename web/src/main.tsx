import { render } from 'preact';
import { App } from './App';
import { DEFAULT_APPEARANCE, applyToRoot, cachedAppearance } from './logic/appearance';
import './styles.css';

// Dev only: ?scenario=NAME on the page URL picks the mock API fixtures (see web/mock/plugin.ts).
if (import.meta.env.DEV) {
  const s = new URLSearchParams(location.search).get('scenario');
  if (s) document.cookie = `scenario=${encodeURIComponent(s)}; path=/; SameSite=Lax`;
}

// Apply this browser's last-seen look before the first paint; the device's copy replaces it once loaded
applyToRoot(cachedAppearance() ?? DEFAULT_APPEARANCE);

render(<App />, document.getElementById('app')!);
