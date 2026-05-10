import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { AccountImportDialogActions } from './accountImport/AccountImportDialogActions';
import { AccountImportDropzone } from './accountImport/AccountImportDropzone';
import { AccountImportHero } from './accountImport/AccountImportHero';
import { AccountImportPreviewTable } from './accountImport/AccountImportPreviewTable';
import { AccountImportResult } from './accountImport/AccountImportResult';
import { AccountImportSourceDatabasePassphrasePrompt } from './accountImport/AccountImportSourceDatabasePassphrasePrompt';
import { AccountImportSourceKeyPrompt } from './accountImport/AccountImportSourceKeyPrompt';
import { AccountImportSummary } from './accountImport/AccountImportSummary';
import { useAccountImport } from './accountImport/useAccountImport';

interface AccountImportDialogProps {
  open: boolean;
  importWithoutOverwrite: boolean;
  onClose: () => void;
}

export function AccountImportDialog({ open, importWithoutOverwrite, onClose }: AccountImportDialogProps) {
  const { t } = useTranslation();
  const [dragOver, setDragOver] = useState(false);
  const supportsNativeBrowse = typeof window.tightrope?.pickSqlImportSourcePath === 'function';
  const {
    stage,
    selectedFileName,
    selectedPath,
    sourceEncryptionKey,
    sourceDatabasePassphrase,
    requiresSourceEncryptionKey,
    requiresSourceDatabasePassphrase,
    preview,
    applyResult,
    error,
    overrides,
    importableRows,
    handleFileSelected,
    handleBrowseRequested,
    setSourceEncryptionKey,
    setSourceDatabasePassphrase,
    setRowOverride,
    rescan,
    applyImport,
    reset,
  } = useAccountImport(importWithoutOverwrite);

  const scanning = stage === 'scanning';
  const importing = stage === 'importing';
  const missingRequiredSourceKey = requiresSourceEncryptionKey && sourceEncryptionKey.trim().length === 0;
  const missingRequiredSourceDbPassphrase =
    requiresSourceDatabasePassphrase && sourceDatabasePassphrase.trim().length === 0;
  const importEnabled = Boolean(preview)
    && importableRows > 0
    && !scanning
    && !importing
    && !missingRequiredSourceKey
    && !missingRequiredSourceDbPassphrase;
  const rescanEnabled = selectedPath.length > 0 && !scanning && !importing;
  const dropzoneStatusText = useMemo(() => {
    if (scanning) return t('dialogs.account_import_scanning');
    if (importing) return t('dialogs.account_import_importing');
    return selectedFileName;
  }, [importing, scanning, selectedFileName]);

  const handleClose = () => {
    setDragOver(false);
    reset();
    onClose();
  };

  if (!open) {
    return null;
  }

  return (
    <dialog open id="accountImportDialog" onClick={(event) => event.currentTarget === event.target && handleClose()}>
      <header className="dialog-header">
        <h3>{t('dialogs.account_import_title')}</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={handleClose}>
          &times;
        </button>
      </header>
      <div className="dialog-body account-import-body">
        <AccountImportHero />

        <AccountImportDropzone
          dragOver={dragOver}
          selectedFileName={selectedFileName}
          statusText={dropzoneStatusText}
          disabled={scanning || importing}
          onSetDragOver={setDragOver}
          onSelectFile={handleFileSelected}
          onBrowseRequested={
            supportsNativeBrowse
              ? () => {
                  void handleBrowseRequested();
                }
              : undefined
          }
        />

        {error && <div className="account-import-error">{error}</div>}

        {(requiresSourceDatabasePassphrase || sourceDatabasePassphrase.trim().length > 0) && (
          <AccountImportSourceDatabasePassphrasePrompt
            value={sourceDatabasePassphrase}
            required={requiresSourceDatabasePassphrase}
            disabled={scanning || importing}
            onChange={setSourceDatabasePassphrase}
          />
        )}

        {preview && (
          <>
            <AccountImportSummary preview={preview} />
            {(requiresSourceEncryptionKey || sourceEncryptionKey.trim().length > 0) && (
              <AccountImportSourceKeyPrompt
                value={sourceEncryptionKey}
                required={requiresSourceEncryptionKey}
                disabled={scanning || importing}
                onChange={setSourceEncryptionKey}
              />
            )}
            {Array.isArray(preview.warnings) && preview.warnings.length > 0 && (
              <ul className="account-import-warning-list account-import-preview-warning-list">
                {preview.warnings.map((warning, index) => (
                  <li key={`${warning}-${index}`}>{warning}</li>
                ))}
              </ul>
            )}
            <AccountImportPreviewTable
              rows={preview.rows}
              overrides={overrides}
              disableActions={importing || stage === 'done'}
              importWithoutOverwrite={importWithoutOverwrite}
              onOverrideChange={setRowOverride}
            />
          </>
        )}

        {applyResult && <AccountImportResult result={applyResult} />}

        <AccountImportDialogActions
          stage={stage}
          importEnabled={importEnabled}
          rescanEnabled={rescanEnabled}
          onCancel={handleClose}
          onRescan={() => {
            void rescan();
          }}
          onImport={() => {
            void applyImport();
          }}
        />
      </div>
    </dialog>
  );
}
