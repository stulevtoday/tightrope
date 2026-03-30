export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export function normalize(value: number, min: number, max: number, epsilon = 1e-9): number {
  return (value - min) / (max - min + epsilon);
}

export function hashToUnit(key: string): number {
  let hash = 0;
  for (let i = 0; i < key.length; i += 1) {
    hash = ((hash << 5) - hash + key.charCodeAt(i)) | 0;
  }
  return (hash >>> 0) / 0xffffffff;
}
