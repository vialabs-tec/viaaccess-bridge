import { writeFileSync } from "node:fs";
import { resolve } from "node:path";

const statusPath = process.env.BRIDGE_STATUS_PATH?.trim()
  ? resolve(process.env.BRIDGE_STATUS_PATH)
  : null;

export function writeBridgeStatus(partial: Record<string, unknown>): void {
  if (!statusPath) return;

  try {
    writeFileSync(
      statusPath,
      JSON.stringify({ updatedAt: new Date().toISOString(), ...partial }, null, 2),
      "utf8",
    );
  } catch {
    // ignore status write errors
  }
}
