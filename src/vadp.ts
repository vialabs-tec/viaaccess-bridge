import type { VadpDetectionEvent } from "./vadp-types.js";

export function buildVadpDetection(
  input: Omit<VadpDetectionEvent, "version" | "providerType" | "event"> & {
    event?: VadpDetectionEvent["event"];
  },
): VadpDetectionEvent {
  return {
    version: "1.0",
    providerType: "detection",
    event: input.event ?? "passage_detected",
    provider: input.provider.trim().toLowerCase(),
    accessPointId: input.accessPointId,
    timestamp: input.timestamp,
    confidence: input.confidence,
    metadata: input.metadata,
  };
}
