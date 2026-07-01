import { buildVadpDetection } from "../vadp.js";
import type { VadpDetectionEvent } from "../vadp-types.js";
import { getFrigateBaseUrl, loadFrigateMappings, matchFrigateMappings } from "./mapping.js";
import { decidePresenceSession } from "./presence-session.js";
import {
  buildFrigateSnapshotUrl,
  detectZoneEntries,
  frigateFrameTimeToIso,
} from "./zone-entry.js";
import type {
  FrigateAccessPointMapping,
  FrigateEventsMessage,
} from "./types.js";

export type TranslateFrigateToVadpOptions = {
  mappings?: FrigateAccessPointMapping[];
  baseUrl?: string | null;
};

export type TranslateFrigateToVadpResult = {
  events: VadpDetectionEvent[];
  ignoredReason?: string;
};

type MotionTarget = { mapping: FrigateAccessPointMapping; zone: string | null };

function isInMappedZone(mapping: FrigateAccessPointMapping, currentZones: string[]): boolean {
  if (mapping.zone === undefined) return currentZones.length > 0;
  return currentZones.includes(mapping.zone);
}

function collectMotionTargets(
  payload: FrigateEventsMessage,
  mappings: FrigateAccessPointMapping[],
  after: NonNullable<FrigateEventsMessage["after"]>,
): MotionTarget[] {
  const targets: MotionTarget[] = [];
  const zoneEntries = detectZoneEntries(payload.before, after);
  const currentZones = after.current_zones ?? [];

  if (zoneEntries.length > 0) {
    for (const zone of zoneEntries) {
      for (const mapping of matchFrigateMappings(mappings, after.camera, zone, after.label)) {
        targets.push({ mapping, zone });
      }
    }
  }

  if (payload.type === "new") {
    for (const mapping of matchFrigateMappings(mappings, after.camera, null, after.label)) {
      if (mapping.zone !== undefined) continue;
      targets.push({ mapping, zone: null });
    }
  }

  if (payload.type === "update" && currentZones.length > 0) {
    for (const mapping of mappings) {
      if (mapping.camera !== after.camera) continue;
      const labels = mapping.labels?.length ? mapping.labels : ["person"];
      if (!labels.includes(after.label)) continue;
      if (!mapping.presenceSessionGapSeconds || mapping.zone === undefined) continue;
      if (!isInMappedZone(mapping, currentZones)) continue;
      if (targets.some((t) => t.mapping === mapping && t.zone === mapping.zone)) continue;
      targets.push({ mapping, zone: mapping.zone });
    }
  }

  return targets;
}

function dedupeMotionTargets(targets: MotionTarget[]): MotionTarget[] {
  const seen = new Set<string>();
  const unique: MotionTarget[] = [];
  for (const target of targets) {
    const key = `${target.mapping.accessPoint}\0${target.mapping.camera}\0${target.zone ?? ""}`;
    if (seen.has(key)) continue;
    seen.add(key);
    unique.push(target);
  }
  return unique;
}

function shouldForwardPresenceSession(
  mapping: FrigateAccessPointMapping,
  zone: string | null,
  trackId: string,
  frameTime: number,
): { forward: true; sessionId: string } | { forward: false } {
  const gapSeconds = mapping.presenceSessionGapSeconds;
  if (!gapSeconds || gapSeconds < 1) {
    return { forward: true, sessionId: trackId };
  }

  const decision = decidePresenceSession({
    camera: mapping.camera,
    zone,
    accessPoint: mapping.accessPoint,
    trackId,
    frameTime,
    gapSeconds,
  });

  if (!decision.emit) {
    return { forward: false };
  }

  return { forward: true, sessionId: decision.sessionId };
}

/**
 * Frigate Detection Provider adapter: MQTT `frigate/events` → VADP.
 * See https://docs.frigate.video/integrations/mqtt/
 */
export function translateFrigateToVadp(
  payload: FrigateEventsMessage,
  options?: TranslateFrigateToVadpOptions,
): TranslateFrigateToVadpResult {
  if (payload.type === "end") {
    return { events: [], ignoredReason: "end_event" };
  }

  const after = payload.after;

  if (!after?.camera || !after.id) {
    return { events: [], ignoredReason: "missing_after_payload" };
  }

  if (after.false_positive) {
    return { events: [], ignoredReason: "false_positive" };
  }

  const mappings = options?.mappings ?? loadFrigateMappings();
  if (!mappings.length) {
    return { events: [], ignoredReason: "no_mappings_configured" };
  }

  const baseUrl = options?.baseUrl === undefined ? getFrigateBaseUrl() : options.baseUrl;
  const snapshotUrl = buildFrigateSnapshotUrl(baseUrl, after.id);
  const timestamp = frigateFrameTimeToIso(after.frame_time);

  const targets = dedupeMotionTargets(collectMotionTargets(payload, mappings, after));
  if (!targets.length) {
    return { events: [], ignoredReason: "no_mapping_match" };
  }

  const events: VadpDetectionEvent[] = [];

  for (const { mapping, zone } of targets) {
    const session = shouldForwardPresenceSession(mapping, zone, after.id, after.frame_time);
    if (!session.forward) continue;

    const debounced = Boolean(mapping.presenceSessionGapSeconds && mapping.presenceSessionGapSeconds >= 1);

    events.push(
      buildVadpDetection({
        provider: "frigate",
        accessPointId: mapping.accessPoint,
        timestamp,
        confidence: after.score ?? null,
        metadata: {
          detectionKind: zone ? "line_crossed" : "person_detected",
          snapshot: snapshotUrl,
          camera: after.camera,
          providerEventId: debounced ? session.sessionId : after.id,
          personCount: 1,
          frigateEventType: payload.type,
          frigateLabel: after.label,
          frigateZone: zone,
          frigateEnteredZones: after.entered_zones ?? [],
          frigateCurrentZones: after.current_zones ?? [],
          ...(debounced
            ? {
                presencePhase: "session_started",
                presenceSessionId: session.sessionId,
                presenceSessionGapSeconds: mapping.presenceSessionGapSeconds,
                frigateTrackId: after.id,
              }
            : {}),
        },
      }),
    );
  }

  if (!events.length) {
    return { events: [], ignoredReason: "presence_session_suppressed" };
  }

  return { events };
}
