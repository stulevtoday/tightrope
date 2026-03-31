import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { app, dialog } from 'electron';

const TIGHTROPE_PROVIDER_NAME = 'tightrope';
const TIGHTROPE_PROVIDER_SECTION = 'model_providers.tightrope';
const TIGHTROPE_PROVIDER_SETTINGS: Record<string, string> = {
  name: '"OpenAI"',
  base_url: '"http://127.0.0.1:2455/backend-api/codex"',
  wire_api: '"responses"',
  supports_websockets: 'true',
  requires_openai_auth: 'true',
};

interface TightropeSetupState {
  configured: boolean;
  hasCommentedProviderEntry: boolean;
  hasCommentedSettingsEntry: boolean;
}

function codexConfigPath(): string {
  return path.join(app.getPath('home'), '.codex', 'config.toml');
}

function stripInlineComment(value: string): string {
  return value.split('#', 1)[0]?.trim() ?? '';
}

function parseTomlStringValue(rawValue: string): string | null {
  const value = stripInlineComment(rawValue);
  const doubleQuoted = value.match(/^"(.*)"$/);
  if (doubleQuoted) {
    return doubleQuoted[1];
  }
  const singleQuoted = value.match(/^'(.*)'$/);
  if (singleQuoted) {
    return singleQuoted[1];
  }
  return null;
}

function parseTomlKeyValueExpression(value: string): { key: string; rawValue: string } | null {
  const keyValueMatch = value.match(/^([A-Za-z0-9_.-]+)\s*=\s*(.+)$/);
  if (!keyValueMatch) {
    return null;
  }
  const [, key, rawValue] = keyValueMatch;
  return { key, rawValue: rawValue.trim() };
}

function parseActiveTomlExpression(line: string): string | null {
  const trimmed = line.trimStart();
  if (trimmed.length === 0 || trimmed.startsWith('#')) {
    return null;
  }
  const expression = stripInlineComment(trimmed);
  return expression.length > 0 ? expression : null;
}

function parseCommentedTomlExpression(line: string): string | null {
  const trimmed = line.trimStart();
  if (!trimmed.startsWith('#')) {
    return null;
  }
  const expression = stripInlineComment(trimmed.slice(1).trimStart());
  return expression.length > 0 ? expression : null;
}

function parseSectionName(expression: string | null): string | null {
  if (!expression) {
    return null;
  }
  const match = expression.match(/^\[([^\]]+)\]$/);
  return match ? match[1] : null;
}

function inspectTightropeSetup(configText: string): TightropeSetupState {
  const requiredSettingKeys = new Set(Object.keys(TIGHTROPE_PROVIDER_SETTINGS));
  const activeSettingKeys = new Set<string>();
  const commentedSettingKeys = new Set<string>();
  let activeSectionName: string | null = null;
  let commentedSectionName: string | null = null;
  let activeModelProvider: string | null = null;
  let hasActiveProviderSection = false;
  let hasCommentedModelProvider = false;
  let hasCommentedProviderSection = false;

  for (const line of configText.split(/\r?\n/)) {
    const activeExpression = parseActiveTomlExpression(line);
    const activeSection = parseSectionName(activeExpression);
    if (activeSection) {
      activeSectionName = activeSection;
      commentedSectionName = null;
      if (activeSectionName === TIGHTROPE_PROVIDER_SECTION) {
        hasActiveProviderSection = true;
      }
      continue;
    }

    const commentedExpression = parseCommentedTomlExpression(line);
    const commentedSection = parseSectionName(commentedExpression);
    if (commentedSection) {
      commentedSectionName = commentedSection;
      if (commentedSectionName === TIGHTROPE_PROVIDER_SECTION) {
        hasCommentedProviderSection = true;
      }
      continue;
    }

    const activeEntry = parseTomlKeyValueExpression(activeExpression ?? '');
    if (activeEntry) {
      if (activeSectionName === null && activeEntry.key === 'model_provider') {
        activeModelProvider = parseTomlStringValue(activeEntry.rawValue);
      }
      if (activeSectionName === TIGHTROPE_PROVIDER_SECTION && requiredSettingKeys.has(activeEntry.key)) {
        activeSettingKeys.add(activeEntry.key);
      }
      continue;
    }

    const commentedEntry = parseTomlKeyValueExpression(commentedExpression ?? '');
    if (!commentedEntry) {
      continue;
    }
    if (activeSectionName === null && commentedSectionName === null && commentedEntry.key === 'model_provider') {
      const modelProvider = parseTomlStringValue(commentedEntry.rawValue);
      hasCommentedModelProvider = modelProvider === TIGHTROPE_PROVIDER_NAME;
    }
    if (
      (activeSectionName === TIGHTROPE_PROVIDER_SECTION || commentedSectionName === TIGHTROPE_PROVIDER_SECTION) &&
      requiredSettingKeys.has(commentedEntry.key)
    ) {
      commentedSettingKeys.add(commentedEntry.key);
    }
  }

  const configured =
    activeModelProvider === TIGHTROPE_PROVIDER_NAME &&
    hasActiveProviderSection &&
    Object.keys(TIGHTROPE_PROVIDER_SETTINGS).every((key) => activeSettingKeys.has(key));

  return {
    configured,
    hasCommentedProviderEntry: hasCommentedModelProvider || hasCommentedProviderSection,
    hasCommentedSettingsEntry: commentedSettingKeys.size > 0,
  };
}

function isActiveSectionHeader(line: string): boolean {
  const trimmed = line.trim();
  return trimmed.length > 0 && !trimmed.startsWith('#') && /^\[[^\]]+\]$/.test(trimmed);
}

function sectionNameForLine(line: string): string | null {
  const trimmed = line.trim();
  if (trimmed.length === 0 || trimmed.startsWith('#')) {
    return null;
  }
  const match = trimmed.match(/^\[([^\]]+)\]$/);
  return match ? match[1] : null;
}

function isActiveKeyLine(line: string, key: string): boolean {
  const trimmed = line.trim();
  return trimmed.length > 0 && !trimmed.startsWith('#') && new RegExp(`^${key}\\s*=`).test(trimmed);
}

function findSectionBounds(lines: string[], sectionName: string): { start: number; end: number } | null {
  let sectionStart = -1;
  for (let index = 0; index < lines.length; index += 1) {
    const currentSection = sectionNameForLine(lines[index]);
    if (!currentSection) {
      continue;
    }

    if (sectionStart === -1) {
      if (currentSection === sectionName) {
        sectionStart = index;
      }
      continue;
    }

    return { start: sectionStart, end: index };
  }

  if (sectionStart === -1) {
    return null;
  }
  return { start: sectionStart, end: lines.length };
}

function upsertModelProvider(lines: string[]): string[] {
  const updated: string[] = [];
  let seenModelProvider = false;
  for (const line of lines) {
    if (!isActiveKeyLine(line, 'model_provider')) {
      updated.push(line);
      continue;
    }

    if (!seenModelProvider) {
      updated.push(`model_provider = "${TIGHTROPE_PROVIDER_NAME}"`);
      seenModelProvider = true;
    }
  }

  if (seenModelProvider) {
    return updated;
  }

  const firstSectionIndex = updated.findIndex((line) => isActiveSectionHeader(line));
  const insertAt = firstSectionIndex >= 0 ? firstSectionIndex : updated.length;
  updated.splice(insertAt, 0, `model_provider = "${TIGHTROPE_PROVIDER_NAME}"`);
  return updated;
}

function upsertTightropeProviderSection(lines: string[]): string[] {
  const bounds = findSectionBounds(lines, TIGHTROPE_PROVIDER_SECTION);
  const requiredEntries = Object.entries(TIGHTROPE_PROVIDER_SETTINGS);

  if (!bounds) {
    const appended = [...lines];
    if (appended.length > 0 && appended[appended.length - 1].trim().length > 0) {
      appended.push('');
    }
    appended.push(`[${TIGHTROPE_PROVIDER_SECTION}]`);
    for (const [key, value] of requiredEntries) {
      appended.push(`${key} = ${value}`);
    }
    return appended;
  }

  const presentKeys = new Set<string>();
  for (let index = bounds.start + 1; index < bounds.end; index += 1) {
    const trimmed = lines[index].trim();
    if (trimmed.length === 0 || trimmed.startsWith('#')) {
      continue;
    }
    const match = trimmed.match(/^([A-Za-z0-9_.-]+)\s*=/);
    if (!match) {
      continue;
    }
    presentKeys.add(match[1]);
  }

  const missingLines = requiredEntries
    .filter(([key]) => !presentKeys.has(key))
    .map(([key, value]) => `${key} = ${value}`);

  if (missingLines.length === 0) {
    return lines;
  }

  const updated = [...lines];
  updated.splice(bounds.end, 0, ...missingLines);
  return updated;
}

function buildUpdatedConfig(existingConfig: string): string {
  const normalized = existingConfig.replace(/\r\n/g, '\n');
  const lines = normalized.length === 0 ? [] : normalized.split('\n');
  const withModelProvider = upsertModelProvider(lines);
  const withProviderSection = upsertTightropeProviderSection(withModelProvider);
  const joined = withProviderSection.join('\n');
  if (joined.endsWith('\n')) {
    return joined;
  }
  return `${joined}\n`;
}

function isTightropeConfigured(configText: string): boolean {
  return inspectTightropeSetup(configText).configured;
}

async function readCodexConfig(): Promise<string> {
  const configPath = codexConfigPath();
  try {
    return await fs.readFile(configPath, 'utf8');
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') {
      return '';
    }
    throw error;
  }
}

async function writeCodexConfig(configText: string): Promise<void> {
  const configPath = codexConfigPath();
  await fs.mkdir(path.dirname(configPath), { recursive: true });
  await fs.writeFile(configPath, configText, 'utf8');
}

function formatError(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  return String(error);
}

export async function ensureTightropeCodexSetup(): Promise<void> {
  const existingConfig = await readCodexConfig();
  const setupState = inspectTightropeSetup(existingConfig);
  if (setupState.configured) {
    return;
  }

  const hasCommentedEntries = setupState.hasCommentedProviderEntry || setupState.hasCommentedSettingsEntry;

  const prompt = await dialog.showMessageBox({
    type: 'warning',
    buttons: ['Add Settings', 'Not Now'],
    defaultId: 0,
    cancelId: 1,
    noLink: true,
    title: 'Tightrope Setup',
    message: 'Tightrope has not been setup in Codex.',
    detail: hasCommentedEntries
      ? 'Detected commented Tightrope provider/settings in ~/.codex/config.toml. Do you want Tightrope to add active settings?'
      : 'Do you want Tightrope to add the required settings to ~/.codex/config.toml?',
  });

  if (prompt.response !== 0) {
    return;
  }

  try {
    const updatedConfig = buildUpdatedConfig(existingConfig);
    await writeCodexConfig(updatedConfig);
  } catch (error) {
    await dialog.showMessageBox({
      type: 'error',
      buttons: ['OK'],
      defaultId: 0,
      noLink: true,
      title: 'Tightrope Setup Failed',
      message: 'Tightrope could not update ~/.codex/config.toml.',
      detail: formatError(error),
    });
    return;
  }

  await dialog.showMessageBox({
    type: 'info',
    buttons: ['OK'],
    defaultId: 0,
    noLink: true,
    title: 'Restart Codex',
    message: 'Tightrope settings were added.',
    detail: 'Restart Codex to load the new Tightrope provider settings.',
  });
}
