import { useEffect, useState } from 'react';

type WindowControlAction = 'close' | 'minimize' | 'maximize';

export function TitleBar() {
  const [isMac, setIsMac] = useState(false);
  const [isMaximized, setIsMaximized] = useState(false);

  useEffect(() => {
    const api = window.tightrope;
    const detectedPlatform =
      api?.platform ??
      (navigator.platform.toLowerCase().includes('mac') ? 'darwin' : 'unknown');
    const mac = detectedPlatform === 'darwin';
    setIsMac(mac);
    if (!api || !mac) return;

    void api.windowIsMaximized().then(setIsMaximized).catch(() => {
      setIsMaximized(false);
    });
  }, []);

  function onWindowControl(action: WindowControlAction): void {
    const api = window.tightrope;
    if (!api) return;

    if (action === 'close') {
      void api.windowClose();
      return;
    }

    if (action === 'minimize') {
      void api.windowMinimize();
      return;
    }

    void api.windowToggleMaximize()
      .then(() => api.windowIsMaximized())
      .then(setIsMaximized)
      .catch(() => {
        setIsMaximized((previous) => !previous);
      });
  }

  return (
    <header className="titlebar" aria-label="Window title">
      {isMac && (
        <div className="titlebar-controls" aria-label="Window controls">
          <button
            className="titlebar-control close"
            type="button"
            aria-label="Close window"
            onClick={() => onWindowControl('close')}
          />
          <button
            className="titlebar-control minimize"
            type="button"
            aria-label="Minimize window"
            onClick={() => onWindowControl('minimize')}
          />
          <button
            className={`titlebar-control maximize${isMaximized ? ' active' : ''}`}
            type="button"
            aria-label={isMaximized ? 'Restore window' : 'Maximize window'}
            onClick={() => onWindowControl('maximize')}
          />
        </div>
      )}

      <div className="titlecopy">
        <strong>tightrope</strong>
        <span>routing workbench</span>
      </div>
    </header>
  );
}
