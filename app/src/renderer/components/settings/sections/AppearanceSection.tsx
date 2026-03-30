import type { ThemeMode } from '../../../shared/types';

interface AppearanceSectionProps {
  theme: ThemeMode;
  onSetTheme: (theme: ThemeMode) => void;
}

export function AppearanceSection({ theme, onSetTheme }: AppearanceSectionProps) {
  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Appearance</h3>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Theme</strong>
          <span>Dashboard color scheme</span>
        </div>
        <select className="setting-select" style={{ minWidth: '120px' }} value={theme} onChange={(event) => onSetTheme(event.target.value as ThemeMode)}>
          <option value="auto">System</option>
          <option value="dark">Dark</option>
          <option value="light">Light</option>
        </select>
      </div>
    </div>
  );
}
