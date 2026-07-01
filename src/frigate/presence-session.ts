export type PresenceSessionDecision =
  | { emit: true; sessionId: string }
  | { emit: false };

type SessionState = {
  sessionId: string;
  lastActivityFrameTime: number;
  trackId: string;
};

const sessions = new Map<string, SessionState>();

function sessionKey(camera: string, zone: string | null, accessPoint: string): string {
  return `${camera}\0${zone ?? ""}\0${accessPoint}`;
}

/** Clears in-memory session state (for tests). */
export function resetPresenceSessions(): void {
  sessions.clear();
}

/**
 * Debounces Frigate track motion into presence sessions.
 * Emits only when a new session starts (gap exceeded or first sighting in zone).
 * Within the gap, suppresses motion even when Frigate assigns a new track id (webcam churn).
 */
export function decidePresenceSession(input: {
  camera: string;
  zone: string | null;
  accessPoint: string;
  trackId: string;
  frameTime: number;
  gapSeconds: number;
}): PresenceSessionDecision {
  const gapMs = input.gapSeconds * 1000;
  const frameMs = input.frameTime * 1000;
  const key = sessionKey(input.camera, input.zone, input.accessPoint);
  const existing = sessions.get(key);

  if (!existing) {
    const sessionId = crypto.randomUUID();
    sessions.set(key, {
      sessionId,
      lastActivityFrameTime: input.frameTime,
      trackId: input.trackId,
    });
    return { emit: true, sessionId };
  }

  const elapsedMs = frameMs - existing.lastActivityFrameTime * 1000;

  if (elapsedMs <= gapMs) {
    existing.lastActivityFrameTime = input.frameTime;
    existing.trackId = input.trackId;
    return { emit: false };
  }

  const sessionId = crypto.randomUUID();
  sessions.set(key, {
    sessionId,
    lastActivityFrameTime: input.frameTime,
    trackId: input.trackId,
  });
  return { emit: true, sessionId };
}
