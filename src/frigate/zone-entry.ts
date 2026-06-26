import type { FrigateTrackedObject } from "./types.js";

/** Zones newly entered between before/after states (Frigate MQTT frigate/events). */
export function detectZoneEntries(
  before: FrigateTrackedObject,
  after: FrigateTrackedObject,
): string[] {
  const previous = new Set(before.entered_zones ?? []);
  return (after.entered_zones ?? []).filter((zone) => !previous.has(zone));
}

export function buildFrigateSnapshotUrl(baseUrl: string | null, eventId: string): string | null {
  if (!baseUrl) return null;
  return `${baseUrl}/api/events/${encodeURIComponent(eventId)}/snapshot.jpg`;
}

export function frigateFrameTimeToIso(frameTime: number): string {
  return new Date(frameTime * 1000).toISOString();
}
