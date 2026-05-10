import { useCallback, useState } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type { FirewallIpEntry, FirewallMode } from '../../shared/types';
import { reportWarn } from '../errors';
import type { StatusNoticeLevel } from '../statusNotices';

export interface UseFirewallOptions {
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  listFirewallIpsRequest: TightropeService['listFirewallIpsRequest'];
  addFirewallIpRequest: TightropeService['addFirewallIpRequest'];
  removeFirewallIpRequest: TightropeService['removeFirewallIpRequest'];
}

export interface UseFirewallResult {
  firewallMode: FirewallMode;
  firewallEntries: FirewallIpEntry[];
  firewallDraftIpAddress: string;
  refreshFirewallIps: () => Promise<void>;
  setFirewallDraft: (value: string) => void;
  addFirewallIpAddress: () => Promise<void>;
  removeFirewallIpAddress: (ipAddress: string) => Promise<void>;
}

export function useFirewall(options: UseFirewallOptions): UseFirewallResult {
  const { listFirewallIpsRequest, addFirewallIpRequest, removeFirewallIpRequest } = options;
  const [firewallMode, setFirewallMode] = useState<FirewallMode>('allow_all');
  const [firewallEntries, setFirewallEntries] = useState<FirewallIpEntry[]>([]);
  const [firewallDraftIpAddress, setFirewallDraftIpAddress] = useState('');

  const refreshFirewallIps = useCallback(async (): Promise<void> => {
    const response = await listFirewallIpsRequest();
    if (!response) {
      return;
    }
    setFirewallMode(response.mode);
    setFirewallEntries(response.entries);
  }, [listFirewallIpsRequest]);

  const setFirewallDraft = useCallback((value: string): void => {
    setFirewallDraftIpAddress(value);
  }, []);

  const addFirewallIpAddress = useCallback(async (): Promise<void> => {
    const candidate = firewallDraftIpAddress.trim();
    if (!candidate) {
      return;
    }
    try {
      const added = await addFirewallIpRequest(candidate);
      if (!added) {
        return;
      }
      setFirewallDraftIpAddress('');
      await refreshFirewallIps();
      options.pushRuntimeEvent(i18next.t('status.firewall_ip_added', { ip: candidate }), 'success');
    } catch (error) {
      reportWarn(options.pushRuntimeEvent, error, 'Failed to add firewall IP');
    }
  }, [addFirewallIpRequest, firewallDraftIpAddress, options, refreshFirewallIps]);

  const removeFirewallIpAddress = useCallback(async (ipAddress: string): Promise<void> => {
    try {
      const removed = await removeFirewallIpRequest(ipAddress);
      if (!removed) {
        return;
      }
      await refreshFirewallIps();
      options.pushRuntimeEvent(i18next.t('status.firewall_ip_removed', { ip: ipAddress }), 'success');
    } catch (error) {
      reportWarn(options.pushRuntimeEvent, error, 'Failed to remove firewall IP');
    }
  }, [options, refreshFirewallIps, removeFirewallIpRequest]);

  return {
    firewallMode,
    firewallEntries,
    firewallDraftIpAddress,
    refreshFirewallIps,
    setFirewallDraft,
    addFirewallIpAddress,
    removeFirewallIpAddress,
  };
}
