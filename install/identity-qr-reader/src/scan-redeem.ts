#!/usr/bin/env node
import readline from "node:readline";
import { fileURLToPath } from "node:url";
import { loadConfig } from "./config.js";
import { formatRedeemLog, redeemQrUrl } from "./redeem.js";

export type ScanLoopOptions = {
  input?: NodeJS.ReadableStream;
  output?: NodeJS.WritableStream;
  now?: () => number;
};

/** Reads QR URLs from stdin (USB keyboard wedge) and redeems on Identity. */
export async function runScanLoop(
  config: ReturnType<typeof loadConfig>,
  options: ScanLoopOptions = {},
): Promise<void> {
  const input = options.input ?? process.stdin;
  const output = options.output ?? process.stdout;
  const now = options.now ?? Date.now;

  let lastScan = "";
  let lastScanAt = 0;
  let busy = false;

  const log = (line: string) => {
    output.write(`${new Date().toISOString()} ${line}\n`);
  };

  log(`Identity QR reader ativo → ${config.identityUrl}`);
  log(`emitDetection=${config.emitDetection} debounceMs=${config.debounceMs}`);
  log("Aguardando leituras (stdin). Escaneie o QR do celular.");

  const rl = readline.createInterface({ input, terminal: false });

  for await (const line of rl) {
    const qrUrl = line.trim();
    if (!qrUrl) continue;

    const ts = now();
    if (qrUrl === lastScan && ts - lastScanAt < config.debounceMs) {
      log("ignorado (debounce)");
      continue;
    }
    lastScan = qrUrl;
    lastScanAt = ts;

    if (busy) {
      log("ocupado, ignorando scan intermediário");
      continue;
    }

    busy = true;
    try {
      const result = await redeemQrUrl(config, qrUrl);
      log(formatRedeemLog(result));
      if (result.ok) {
        output.write("\x07");
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      log(`ERRO rede: ${message}`);
    } finally {
      busy = false;
    }
  }
}

const isMain = process.argv[1] === fileURLToPath(import.meta.url);
if (isMain) {
  runScanLoop(loadConfig()).catch((error) => {
    console.error(error instanceof Error ? error.message : error);
    process.exit(1);
  });
}
