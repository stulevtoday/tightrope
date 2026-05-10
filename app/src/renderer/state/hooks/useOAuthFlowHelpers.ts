import type { AppRuntimeState } from '../../shared/types';

export const DEFAULT_DEVICE_EXPIRES_SECONDS = 900;
export const DEFAULT_OAUTH_CALLBACK_PORT = 1455;
export const BROWSER_OAUTH_POLL_MS = 1000;
export const OAUTH_DEEP_LINK_RETRY_MS = 250;
export const OAUTH_DEEP_LINK_MAX_ATTEMPTS = 12;

export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function sanitizeEmailLocalPart(input: string): string {
  const lowered = input.toLowerCase();
  const sanitized = lowered.replace(/[^a-z0-9._-]/g, '-').replace(/-+/g, '-').replace(/^-+|-+$/g, '');
  if (sanitized.length > 0) {
    return sanitized.slice(0, 48);
  }
  return `account-${Date.now().toString(36)}`;
}

export function oauthEmailFromHints(callbackUrl: string | null, fallbackHint: string): string {
  if (callbackUrl) {
    try {
      const parsed = new URL(callbackUrl);
      const maybeEmail = parsed.searchParams.get('email') ?? parsed.searchParams.get('user_email');
      if (maybeEmail && maybeEmail.includes('@')) {
        return maybeEmail;
      }
      const maybeCode = parsed.searchParams.get('code') ?? parsed.searchParams.get('state');
      if (maybeCode) {
        return `${sanitizeEmailLocalPart(maybeCode)}@openai.local`;
      }
    } catch {
      // ignore malformed callback URL and fallback below
    }
  }
  return `${sanitizeEmailLocalPart(fallbackHint)}@openai.local`;
}

function stringField(value: unknown): string | null {
  return typeof value === 'string' && value.trim() !== '' ? value.trim() : null;
}

export interface ImportAccountPayload {
  email: string;
  provider: string;
  access_token?: string;
  refresh_token?: string;
}

export function extractImportAccountPayload(raw: unknown): ImportAccountPayload | null {
  if (!raw || typeof raw !== 'object') {
    return null;
  }
  const object = raw as Record<string, unknown>;
  const nestedAccount =
    object.account && typeof object.account === 'object' ? (object.account as Record<string, unknown>) : undefined;
  const nestedUser =
    object.user && typeof object.user === 'object' ? (object.user as Record<string, unknown>) : undefined;
  const nestedAccounts =
    Array.isArray(object.accounts) && object.accounts.length > 0 && typeof object.accounts[0] === 'object'
      ? (object.accounts[0] as Record<string, unknown>)
      : undefined;

  const email =
    stringField(object.email) ??
    stringField(object.accountEmail) ??
    stringField(nestedAccount?.email) ??
    stringField(nestedUser?.email) ??
    stringField(nestedAccounts?.email);
  const refreshToken =
    stringField(object.refresh_token) ??
    stringField(nestedAccount?.refresh_token);
  const accessToken =
    stringField(object.access_token) ??
    stringField(nestedAccount?.access_token);
  const fallbackTokenSeed =
    refreshToken ??
    accessToken ??
    stringField(object.token) ??
    stringField(nestedAccount?.access_token);
  const resolvedEmail =
    email ??
    (fallbackTokenSeed ? `${sanitizeEmailLocalPart(fallbackTokenSeed)}@openai.local` : null);
  if (!resolvedEmail) {
    return null;
  }
  const provider =
    stringField(object.provider) ??
    stringField(nestedAccount?.provider) ??
    stringField(nestedUser?.provider) ??
    stringField(nestedAccounts?.provider) ??
    'openai';
  const result: ImportAccountPayload = { email: resolvedEmail, provider };
  if (accessToken) result.access_token = accessToken;
  if (refreshToken) result.refresh_token = refreshToken;
  return result;
}

export function readFileText(file: File): Promise<string> {
  const textMethod = (file as File & { text?: () => Promise<string> }).text;
  if (typeof textMethod === 'function') {
    return textMethod.call(file);
  }
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(new Error('Failed to read import file'));
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : '');
    reader.readAsText(file);
  });
}

export function isValidOAuthAuthorizationUrl(urlValue: string | null | undefined): urlValue is string {
  if (typeof urlValue !== 'string') {
    return false;
  }
  const trimmed = urlValue.trim();
  if (!trimmed || trimmed.includes('...')) {
    return false;
  }
  try {
    const parsed = new URL(trimmed);
    return (
      parsed.pathname === '/oauth/authorize' &&
      parsed.searchParams.get('response_type') === 'code' &&
      !!parsed.searchParams.get('state')
    );
  } catch {
    return false;
  }
}

export function oauthStateToken(authorizationUrl: string): string | null {
  if (!isValidOAuthAuthorizationUrl(authorizationUrl)) {
    return null;
  }
  try {
    const url = new URL(authorizationUrl);
    return url.searchParams.get('state');
  } catch {
    return null;
  }
}

export async function writeClipboardText(value: string): Promise<void> {
  if (typeof navigator !== 'undefined' && navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(value);
    return;
  }

  if (typeof document === 'undefined' || typeof document.execCommand !== 'function') {
    throw new Error('Clipboard API unavailable');
  }

  const element = document.createElement('textarea');
  element.value = value;
  element.setAttribute('readonly', 'true');
  element.style.position = 'fixed';
  element.style.opacity = '0';
  element.style.left = '-9999px';
  document.body.appendChild(element);
  element.select();
  const copied = document.execCommand('copy');
  document.body.removeChild(element);
  if (!copied) {
    throw new Error('Clipboard write failed');
  }
}

export function callbackParts(callbackUrl: string): { callbackPath: string; listenerPort: number; listenerUrl: string } {
  try {
    const parsed = new URL(callbackUrl);
    const callbackPath = parsed.pathname || '/auth/callback';
    const listenerPort = parsed.port ? Number(parsed.port) : parsed.protocol === 'https:' ? 443 : 80;
    return {
      callbackPath,
      listenerPort: Number.isFinite(listenerPort) && listenerPort > 0 ? listenerPort : DEFAULT_OAUTH_CALLBACK_PORT,
      listenerUrl: parsed.toString(),
    };
  } catch {
    return {
      callbackPath: '/auth/callback',
      listenerPort: DEFAULT_OAUTH_CALLBACK_PORT,
      listenerUrl: callbackUrl,
    };
  }
}

export function oauthCallbackHasCode(callbackUrl: string | null | undefined): boolean {
  if (typeof callbackUrl !== 'string' || callbackUrl.trim() === '') {
    return false;
  }
  try {
    const parsed = new URL(callbackUrl);
    const code = parsed.searchParams.get('code');
    return typeof code === 'string' && code.trim() !== '';
  } catch {
    return false;
  }
}

export function resetAddAccountTransientState(state: AppRuntimeState): AppRuntimeState {
  return {
    ...state,
    addAccountStep: 'stepMethod',
    selectedFileName: '',
    manualCallback: '',
    deviceCountdownSeconds: DEFAULT_DEVICE_EXPIRES_SECONDS,
    copyAuthLabel: 'Copy',
    copyDeviceLabel: 'Copy',
    addAccountError: 'Something went wrong.',
  };
}
