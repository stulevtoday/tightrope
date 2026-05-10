import type { AppPage } from '../../shared/types';
import { useTranslation } from 'react-i18next';
import { useNavigationContext } from '../../state/context';

const NAV_ITEMS: Array<{ page: AppPage; titleKey: string; subtitleKey: string }> = [
  { page: 'router', titleKey: 'nav.router_title', subtitleKey: 'nav.router_subtitle' },
  { page: 'accounts', titleKey: 'nav.accounts_title', subtitleKey: 'nav.accounts_subtitle' },
  { page: 'sessions', titleKey: 'nav.sessions_title', subtitleKey: 'nav.sessions_subtitle' },
  { page: 'logs', titleKey: 'nav.logs_title', subtitleKey: 'nav.logs_subtitle' },
  { page: 'settings', titleKey: 'nav.settings_title', subtitleKey: 'nav.settings_subtitle' },
];

export function NavRail() {
  const { t } = useTranslation();
  const navigation = useNavigationContext();

  return (
    <aside className="nav-rail">
      <nav className="rail-group" aria-label={t('nav.objects_label')}>
        <p className="rail-label">{t('nav.objects_label')}</p>
        {NAV_ITEMS.map((item) => (
          <button
            key={item.page}
            className={`nav-item${item.page === navigation.currentPage ? ' active' : ''}`}
            data-page={item.page}
            type="button"
            onClick={() => navigation.setCurrentPage(item.page)}
          >
            <strong>{t(item.titleKey)}</strong>
            <span>{t(item.subtitleKey)}</span>
          </button>
        ))}
      </nav>
    </aside>
  );
}
