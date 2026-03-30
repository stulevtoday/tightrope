import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import './styles/app.css';

const container = document.getElementById('app');
if (!container) {
  throw new Error('Renderer root #app not found');
}

createRoot(container).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
