import { useTranslation } from 'react-i18next';
import type { AppMetaResponse } from '../../shared/types';
import loadBalancerIcon from '../../assets/load_balancer.svg';

interface AboutDialogProps {
  open: boolean;
  platform: NodeJS.Platform | null;
  appMeta: AppMetaResponse | null;
  onClose: () => void;
}

function platformLabel(platform: NodeJS.Platform | null, t: (key: string) => string): string {
  if (platform === 'darwin') {
    return t('dialogs.about_platform_macos');
  }
  if (platform === 'win32') {
    return t('dialogs.about_platform_windows');
  }
  if (platform === 'linux') {
    return t('dialogs.about_platform_linux');
  }
  return t('dialogs.about_platform_desktop');
}

export function AboutDialog({ open, platform, appMeta, onClose }: AboutDialogProps) {
  const { t } = useTranslation();
  if (!open) {
    return null;
  }

  const year = new Date().getFullYear();
  const buildLabel = appMeta ? `${appMeta.version} (${appMeta.buildChannel})` : t('dialogs.about_build_unavailable');

  return (
    <dialog open id="aboutDialog" onClick={(event) => event.currentTarget === event.target && onClose()}>
      <header className="dialog-header about-header">
        <div className="about-header-copy">
          <span className="about-kicker">{t('dialogs.about_title')}</span>
          <h3>{t('dialogs.about_app_title')}</h3>
        </div>
        <button className="dialog-close" type="button" aria-label={t('common.close')} onClick={onClose}>
          &times;
        </button>
      </header>
      <div className="dialog-body about-body">
        <section className="about-hero">
          <div className="about-mark-wrap" aria-hidden="true">
            <img className="about-mark" src={loadBalancerIcon} alt="" />
          </div>
          <div className="about-copy">
            <span>{t('dialogs.about_description')}</span>
          </div>
        </section>
        <div className="about-meta-grid">
          <div>
            <span>{t('dialogs.about_platform')}</span>
            <strong>{platformLabel(platform, t)}</strong>
          </div>
          <div>
            <span>{t('dialogs.about_runtime')}</span>
            <strong>{t('dialogs.about_runtime_desktop')}</strong>
          </div>
          <div>
            <span>{t('dialogs.about_copyright', { year })}</span>
          </div>
          <div>
            <span>{t('dialogs.about_build')}</span>
            <strong>{buildLabel}</strong>
          </div>
        </div>
      </div>
    </dialog>
  );
}
