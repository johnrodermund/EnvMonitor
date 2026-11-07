import { render } from 'preact';

// Dev only: ?scenario=NAME on the page URL picks the mock API fixtures (see web/mock/plugin.ts).
if (import.meta.env.DEV) {
  const s = new URLSearchParams(location.search).get('scenario');
  if (s) document.cookie = `scenario=${encodeURIComponent(s)}; path=/; SameSite=Lax`;
}

function App() {
  return <h1>EnvMonitor</h1>;
}

render(<App />, document.getElementById('app')!);
