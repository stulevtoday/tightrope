import type { DragEvent } from 'react';
import { useTranslation } from 'react-i18next';

interface AccountImportDropzoneProps {
  dragOver: boolean;
  selectedFileName: string;
  disabled?: boolean;
  statusText?: string;
  onSetDragOver: (value: boolean) => void;
  onSelectFile: (file: File | null) => void;
  onBrowseRequested?: () => void;
}

function firstDroppedFile(event: DragEvent<HTMLLabelElement>): File | null {
  const files = event.dataTransfer?.files;
  if (!files || files.length === 0) {
    return null;
  }
  return files[0] ?? null;
}

export function AccountImportDropzone({
  dragOver,
  selectedFileName,
  disabled = false,
  statusText,
  onSetDragOver,
  onSelectFile,
  onBrowseRequested,
}: AccountImportDropzoneProps) {
  const { t } = useTranslation();
  return (
    <label
      className={`account-import-drop file-drop${dragOver ? ' dragover' : ''}${disabled ? ' disabled' : ''}`}
      htmlFor="accountImportFileInput"
      onDragOver={(event) => {
        if (disabled) return;
        event.preventDefault();
        onSetDragOver(true);
      }}
      onDragEnter={(event) => {
        if (disabled) return;
        event.preventDefault();
        onSetDragOver(true);
      }}
      onDragLeave={(event) => {
        if (disabled) return;
        event.preventDefault();
        if (event.currentTarget.contains(event.relatedTarget as Node | null)) return;
        onSetDragOver(false);
      }}
      onDrop={(event) => {
        if (disabled) return;
        event.preventDefault();
        onSetDragOver(false);
        onSelectFile(firstDroppedFile(event));
      }}
    >
      <p>{t('dialogs.account_import_drop_text')}</p>
      <small>
        {t('dialogs.account_import_drop_formats')} <span className="mono">.sqlite</span>, <span className="mono">.sqlite3</span>, <span className="mono">.db</span>
      </small>
      <input
        id="accountImportFileInput"
        type="file"
        accept=".sqlite,.sqlite3,.db,application/vnd.sqlite3"
        disabled={disabled}
        onClick={(event) => {
          if (!onBrowseRequested) {
            return;
          }
          event.preventDefault();
          onBrowseRequested();
        }}
        onChange={(event) => onSelectFile(event.target.files?.[0] ?? null)}
      />
      <div className="file-name">{statusText ?? selectedFileName}</div>
    </label>
  );
}
