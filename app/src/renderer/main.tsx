import { createRoot } from 'react-dom/client';
import { App } from './App';
import { ErrorBoundary } from './components/shared/ErrorBoundary';
import './i18n';
import './styles/app.css';

const container = document.getElementById('app');
if (!container) {
  throw new Error('Renderer root #app not found');
}

createRoot(container).render(
  <ErrorBoundary>
    <App />
  </ErrorBoundary>,
);
