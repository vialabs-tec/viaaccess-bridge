export type FrigateEventType = "new" | "update" | "end";

export type FrigateTrackedObject = {
  id: string;
  camera: string;
  frame_time: number;
  label: string;
  false_positive?: boolean;
  start_time?: number;
  end_time?: number | null;
  score?: number;
  current_zones?: string[];
  entered_zones?: string[];
  has_snapshot?: boolean;
};

export type FrigateEventsMessage = {
  type: FrigateEventType;
  before: FrigateTrackedObject;
  after: FrigateTrackedObject;
};

export type FrigateAccessPointMapping = {
  /** ViaAccess access point slug */
  accessPoint: string;
  /** Frigate camera name */
  camera: string;
  /** Frigate zone that represents a line crossing / entrance */
  zone?: string;
  /** Object labels to accept (default: person) */
  labels?: string[];
  /**
   * Debounce continuous motion into presence sessions (seconds).
   * When set, only the first detection per session is forwarded to ViaAccess.
   */
  presenceSessionGapSeconds?: number;
};
