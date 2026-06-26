export type VadpDetectionEvent = {
  version: "1.0";
  provider: string;
  providerType: "detection";
  event: "passage_detected";
  accessPointId: string;
  timestamp: string;
  confidence?: number | null;
  metadata?: Record<string, unknown>;
};
