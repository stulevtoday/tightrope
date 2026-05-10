import type { ThemeMode } from '../../../shared/types';
import { useTranslation } from 'react-i18next';

interface AppearanceSectionProps {
  theme: ThemeMode;
  onSetTheme: (theme: ThemeMode) => void;
}

export function AppearanceSection({ theme, onSetTheme }: AppearanceSectionProps) {
  const { t, i18n } = useTranslation();
  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>{t('settings.appearance_title')}</h3>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.appearance_theme')}</strong>
          <span>{t('settings.appearance_theme_desc')}</span>
        </div>
        <select className="setting-select" style={{ minWidth: '120px' }} value={theme} onChange={(event) => onSetTheme(event.target.value as ThemeMode)}>
          <option value="auto">{t('settings.theme_system')}</option>
          <option value="dark">{t('settings.theme_dark')}</option>
          <option value="light">{t('settings.theme_light')}</option>
        </select>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.language')}</strong>
          <span>{t('settings.language_desc')}</span>
        </div>
        <select className="setting-select" style={{ minWidth: '120px' }} value={i18n.language} onChange={(event) => i18n.changeLanguage(event.target.value)}>
          <option value="en">English</option>
          <option value="ru">Русский</option>
        </select>
      </div>
    </div>
  );
}
