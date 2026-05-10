import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useTightropeService } from '../../state/context';

type WindowControlAction = 'close' | 'minimize' | 'maximize';

interface TitleBarProps {
  onOpenAbout?: () => void;
}

export function TitleBar({ onOpenAbout }: TitleBarProps) {
  const service = useTightropeService();
  const { t } = useTranslation();
  const [isMaximized, setIsMaximized] = useState(false);

  useEffect(() => {
    void service.windowIsMaximizedRequest()
      .then((maximized) => {
        setIsMaximized(maximized ?? false);
      })
      .catch(() => {
        setIsMaximized(false);
      });
  }, [service]);

  function onWindowControl(action: WindowControlAction): void {
    if (action === 'close') {
      void service.windowCloseRequest();
      return;
    }

    if (action === 'minimize') {
      void service.windowMinimizeRequest();
      return;
    }

    void service.windowToggleMaximizeRequest()
      .then((toggled) => {
        if (!toggled) {
          return null;
        }
        return service.windowIsMaximizedRequest();
      })
      .then((maximized) => {
        if (typeof maximized === 'boolean') {
          setIsMaximized(maximized);
        }
      })
      .catch(() => {
        setIsMaximized((previous) => !previous);
      });
  }

  return (
    <header className="titlebar" aria-label="Window title">
      <div className="titlebar-controls" aria-label="Window controls">
        <button
          className="titlebar-control close"
          type="button"
          aria-label={t('titlebar.close_window')}
          onClick={() => onWindowControl('close')}
        />
        <button
          className="titlebar-control minimize"
          type="button"
          aria-label={t('titlebar.minimize_window')}
          onClick={() => onWindowControl('minimize')}
        />
        <button
          className={`titlebar-control maximize${isMaximized ? ' active' : ''}`}
          type="button"
          aria-label={isMaximized ? t('titlebar.restore_window') : t('titlebar.maximize_window')}
          onClick={() => onWindowControl('maximize')}
        />
      </div>

      <div className="titlecopy">
        <strong>{t('titlebar.app_name')}</strong>
        <span>{t('titlebar.app_subtitle')}</span>
      </div>
      <div className="titlebar-spacer" />
      {onOpenAbout ? (
        <button
          className="titlebar-action"
          type="button"
          aria-label={t('titlebar.about_tightrope')}
          title={t('titlebar.about_tightrope')}
          onClick={onOpenAbout}
        >
          <svg
            width="14"
            height="14"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
            aria-hidden="true"
            focusable="false"
          >
            <circle cx="12" cy="12" r="9" />
            <line x1="12" y1="11" x2="12" y2="16" />
            <circle cx="12" cy="8" r="0.8" fill="currentColor" />
          </svg>
          <span className="titlebar-action-label">{t('titlebar.about')}</span>
        </button>
      ) : null}
    </header>
  );
}
