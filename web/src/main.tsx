import { render } from 'preact';
import { App } from './App';
import './styles.css';

// Dev only: ?scenario=NAME on the page URL picks the mock API fixtures (see web/mock/plugin.ts).
if (import.meta.env.DEV) {
  const s = new URLSearchParams(location.search).get('scenario');
  if (s) document.cookie = `scenario=${encodeURIComponent(s)}; path=/; SameSite=Lax`;
}

render(<App />, document.getElementById('app')!);
