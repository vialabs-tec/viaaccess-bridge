import { buildVadpDetection } from "../vadp.js";
import type { VadpDetectionEvent } from "../vadp-types.js";
import { getFrigateBaseUrl, loadFrigateMappings, matchFrigateMappings } from "./mapping.js";
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
  const before = payload.before;

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
  const zoneEntries = detectZoneEntries(before, after);

  const targets: Array<{ mapping: FrigateAccessPointMapping; zone: string | null }> = [];

  if (zoneEntries.length > 0) {
    for (const zone of zoneEntries) {
      for (const mapping of matchFrigateMappings(mappings, after.camera, zone, after.label)) {
        targets.push({ mapping, zone });
      }
    }
  } else if (payload.type === "new") {
    for (const mapping of matchFrigateMappings(mappings, after.camera, null, after.label)) {
      targets.push({ mapping, zone: null });
    }
  }

  if (!targets.length) {
    return { events: [], ignoredReason: "no_mapping_match" };
  }

  const events = targets.map(({ mapping, zone }) =>
    buildVadpDetection({
      provider: "frigate",
      accessPointId: mapping.accessPoint,
      timestamp,
      confidence: after.score ?? null,
      metadata: {
        detectionKind: zone ? "line_crossed" : "person_detected",
        snapshot: snapshotUrl,
        camera: after.camera,
        providerEventId: after.id,
        personCount: 1,
        frigateEventType: payload.type,
        frigateLabel: after.label,
        frigateZone: zone,
        frigateEnteredZones: after.entered_zones ?? [],
        frigateCurrentZones: after.current_zones ?? [],
      },
    }),
  );

  return { events };
}
