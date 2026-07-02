export type ReaderConfig = {
  identityUrl: string;
  deviceKey: string;
  emitDetection: boolean;
  debounceMs: number;
};

export function loadConfig(env: NodeJS.ProcessEnv = process.env): ReaderConfig {
  const identityUrl = env.IDENTITY_URL?.trim().replace(/\/$/, "");
  const deviceKey = env.IDENTITY_DEVICE_KEY?.trim();

  if (!identityUrl) {
    throw new Error("IDENTITY_URL is required (ex. http://localhost:3100).");
  }
  if (!deviceKey?.startsWith("idb_")) {
    throw new Error("IDENTITY_DEVICE_KEY is required (prefix idb_… from Identity admin).");
  }

  const debounceRaw = Number(env.DEBOUNCE_MS ?? 2000);
  const debounceMs = Number.isFinite(debounceRaw) && debounceRaw >= 0 ? debounceRaw : 2000;

  return {
    identityUrl,
    deviceKey,
    emitDetection: env.EMIT_DETECTION !== "false",
    debounceMs,
  };
}
