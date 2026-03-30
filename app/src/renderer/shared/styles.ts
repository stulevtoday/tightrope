import type { CSSProperties } from 'react';

export function sliderTrackStyle(value: number): CSSProperties {
  const pct = Math.max(0, Math.min(100, value * 100));
  return {
    background: `linear-gradient(to right, var(--accent) 0%, var(--accent) ${pct}%, var(--surface-1) ${pct}%, var(--surface-1) 100%)`,
  };
}
