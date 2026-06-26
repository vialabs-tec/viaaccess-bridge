import type { FrigateAccessPointMapping } from "./types.js";

const DEFAULT_LABELS = ["person"];

export function loadFrigateMappings(): FrigateAccessPointMapping[] {
  const raw = process.env.FRIGATE_ACCESS_POINT_MAP?.trim();
  if (!raw) return [];

  try {
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return [];
    return parsed.filter(isValidMapping);
  } catch {
    console.warn("[viaaccess-bridge] FRIGATE_ACCESS_POINT_MAP is not valid JSON");
    return [];
  }
}

function isValidMapping(value: unknown): value is FrigateAccessPointMapping {
  if (!value || typeof value !== "object") return false;
  const mapping = value as FrigateAccessPointMapping;
  return typeof mapping.accessPoint === "string" && typeof mapping.camera === "string";
}

export function getFrigateBaseUrl(): string | null {
  const url = process.env.FRIGATE_BASE_URL?.trim();
  return url || null;
}

export function matchFrigateMappings(
  mappings: FrigateAccessPointMapping[],
  camera: string,
  zone: string | null,
  label: string,
): FrigateAccessPointMapping[] {
  return mappings.filter((mapping) => {
    if (mapping.camera !== camera) return false;

    const labels = mapping.labels?.length ? mapping.labels : DEFAULT_LABELS;
    if (!labels.includes(label)) return false;

    if (mapping.zone !== undefined) {
      return zone === mapping.zone;
    }

    return zone === null;
  });
}
