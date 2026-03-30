import type { AppPage } from '../../shared/types';

interface NavRailProps {
  currentPage: AppPage;
  onSelectPage: (page: AppPage) => void;
}

const NAV_ITEMS: Array<{ page: AppPage; title: string; subtitle: string }> = [
  { page: 'router', title: 'Router', subtitle: 'Process, bind, health' },
  { page: 'accounts', title: 'Accounts', subtitle: 'Quota, tokens, usage' },
  { page: 'sessions', title: 'Sessions', subtitle: 'Affinity and sticky reuse' },
  { page: 'logs', title: 'Logs', subtitle: 'Requests and events' },
  { page: 'settings', title: 'Settings', subtitle: 'Routing, auth, keys' },
];

export function NavRail({ currentPage, onSelectPage }: NavRailProps) {
  return (
    <aside className="nav-rail">
      <nav className="rail-group" aria-label="Runtime objects">
        <p className="rail-label">Objects</p>
        {NAV_ITEMS.map((item) => (
          <button
            key={item.page}
            className={`nav-item${item.page === currentPage ? ' active' : ''}`}
            data-page={item.page}
            type="button"
            onClick={() => onSelectPage(item.page)}
          >
            <strong>{item.title}</strong>
            <span>{item.subtitle}</span>
          </button>
        ))}
      </nav>
    </aside>
  );
}
