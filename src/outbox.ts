import { appendFile, mkdir, readFile, rename, writeFile } from "node:fs/promises";
import path from "node:path";

export type OutboxEntry = {
  idempotencyKey: string;
  body: unknown;
  attempts: number;
  createdAt: string;
};

const MAX_ATTEMPTS = 8;

export class Outbox {
  private readonly filePath: string;
  private flushing = false;

  constructor(filePath: string) {
    this.filePath = filePath;
  }

  async enqueue(entry: Omit<OutboxEntry, "attempts" | "createdAt">): Promise<void> {
    const line = JSON.stringify({
      ...entry,
      attempts: 0,
      createdAt: new Date().toISOString(),
    } satisfies OutboxEntry);
    await this.ensureDir();
    await appendFile(this.filePath, `${line}\n`, "utf8");
  }

  async flush(send: (entry: OutboxEntry) => Promise<boolean>): Promise<void> {
    if (this.flushing) return;
    this.flushing = true;

    try {
      const entries = await this.readAll();
      if (!entries.length) return;

      const remaining: OutboxEntry[] = [];

      for (const entry of entries) {
        if (entry.attempts >= MAX_ATTEMPTS) {
          console.error(
            `[viaaccess-bridge] outbox drop id=${entry.idempotencyKey} after ${entry.attempts} attempts`,
          );
          continue;
        }

        const ok = await send(entry);
        if (!ok) {
          remaining.push({ ...entry, attempts: entry.attempts + 1 });
        }
      }

      await this.writeAll(remaining);
    } catch (error) {
      console.error("[viaaccess-bridge] outbox flush failed", error);
    } finally {
      this.flushing = false;
    }
  }

  private async ensureDir(): Promise<void> {
    await mkdir(path.dirname(this.filePath), { recursive: true });
  }

  private async readAll(): Promise<OutboxEntry[]> {
    try {
      const raw = await readFile(this.filePath, "utf8");
      return raw
        .split("\n")
        .map((line) => line.trim())
        .filter(Boolean)
        .map((line) => JSON.parse(line) as OutboxEntry);
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === "ENOENT") return [];
      throw error;
    }
  }

  private async writeAll(entries: OutboxEntry[]): Promise<void> {
    await this.ensureDir();
    const tmp = `${this.filePath}.tmp`;
    const content = entries.length ? `${entries.map((e) => JSON.stringify(e)).join("\n")}\n` : "";
    await writeFile(tmp, content, "utf8");
    await rename(tmp, this.filePath);
  }
}
