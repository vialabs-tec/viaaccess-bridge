import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { fileURLToPath } from "node:url";
import { redeemQrUrl, type RedeemResult } from "./redeem.js";
import { loadTurnstileHttpConfig, type TurnstileHttpConfig } from "./turnstile-http-config.js";
import { postUnlockWebhook } from "./unlock-webhook.js";

export type DebounceState = {
  lastScan: string;
  lastScanAt: number;
};

export function createDebounceState(): DebounceState {
  return { lastScan: "", lastScanAt: 0 };
};

type ScanBody = {
  qrUrl?: string;
  qr?: string;
  payload?: string;
};

export function extractQrUrlFromBody(body: unknown): string | null {
  if (typeof body === "string") {
    const trimmed = body.trim();
    return trimmed || null;
  }
  if (!body || typeof body !== "object") return null;
  const record = body as ScanBody;
  for (const value of [record.qrUrl, record.qr, record.payload]) {
    if (typeof value === "string" && value.trim()) return value.trim();
  }
  return null;
}

function isAuthorizedRedeem(result: RedeemResult): boolean {
  return result.ok && result.data.correlationOutcome === "AUTHORIZED";
}

function shouldUnlock(config: TurnstileHttpConfig, result: RedeemResult): boolean {
  if (!config.unlockWebhookUrl || !result.ok) return false;
  if (!config.unlockOnAuthorizedOnly) return true;
  return isAuthorizedRedeem(result);
}

export async function handleScanRequest(input: {
  config: TurnstileHttpConfig;
  body: unknown;
  webhookSecretHeader?: string | null;
  debounce: DebounceState;
  now?: () => number;
  fetchImpl?: typeof fetch;
}): Promise<{ status: number; body: Record<string, unknown> }> {
  const now = input.now ?? Date.now;
  const fetchImpl = input.fetchImpl ?? fetch;
  const { config, debounce } = input;

  if (config.webhookSecret) {
    const provided = input.webhookSecretHeader?.trim();
    if (provided !== config.webhookSecret) {
      return { status: 401, body: { ok: false, error: "Webhook não autorizado." } };
    }
  }

  const qrUrl = extractQrUrlFromBody(input.body);
  if (!qrUrl) {
    return {
      status: 400,
      body: { ok: false, error: "Informe qrUrl, qr ou payload com a URL do QR." },
    };
  }

  if (qrUrl === debounce.lastScan && now() - debounce.lastScanAt < config.debounceMs) {
    return { status: 200, body: { ok: true, ignored: true, reason: "debounce" } };
  }
  debounce.lastScan = qrUrl;
  debounce.lastScanAt = now();

  const redeem = await redeemQrUrl(config, qrUrl, fetchImpl);
  const response: Record<string, unknown> = {
    ok: redeem.ok,
    redeem: redeem.ok ? redeem.data : { error: redeem.data.error, code: redeem.data.code },
  };

  if (shouldUnlock(config, redeem)) {
    const unlock = await postUnlockWebhook(
      config.unlockWebhookUrl!,
      {
        memberId: redeem.data.memberId,
        validationId: redeem.data.validationId,
        detectionId: redeem.data.detectionId ?? null,
        correlationOutcome: redeem.data.correlationOutcome ?? null,
        accessPointSlug: redeem.data.accessPointSlug,
      },
      fetchImpl,
    );
    response.unlock = unlock;
  }

  const status = redeem.ok ? 200 : redeem.status >= 400 ? redeem.status : 502;
  return { status, body: response };
}

async function readJsonBody(req: IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) {
    chunks.push(typeof chunk === "string" ? Buffer.from(chunk) : chunk);
  }
  const raw = Buffer.concat(chunks).toString("utf8").trim();
  if (!raw) return {};
  try {
    return JSON.parse(raw) as unknown;
  } catch {
    return raw;
  }
}

export function startTurnstileHttpServer(
  config: TurnstileHttpConfig,
  options: { fetchImpl?: typeof fetch } = {},
): ReturnType<typeof createServer> {
  const debounce = createDebounceState();
  const fetchImpl = options.fetchImpl ?? fetch;

  const server = createServer(async (req, res) => {
    await routeRequest(req, res, config, debounce, fetchImpl);
  });

  server.listen(config.port, config.host, () => {
    console.log(
      `${new Date().toISOString()} Turnstile HTTP adapter em http://${config.host}:${config.port}`,
    );
    console.log(`Identity → ${config.identityUrl} emitDetection=${config.emitDetection}`);
    if (config.unlockWebhookUrl) {
      console.log(`Unlock webhook → ${config.unlockWebhookUrl}`);
    }
  });

  return server;
}

async function routeRequest(
  req: IncomingMessage,
  res: ServerResponse,
  config: TurnstileHttpConfig,
  debounce: DebounceState,
  fetchImpl: typeof fetch,
): Promise<void> {
  const url = req.url ?? "/";

  if (req.method === "GET" && url === "/health") {
    sendJson(res, 200, { ok: true });
    return;
  }

  if (req.method === "POST" && (url === "/scan" || url === "/")) {
    const body = await readJsonBody(req);
    const result = await handleScanRequest({
      config,
      body,
      webhookSecretHeader: req.headers["x-webhook-secret"],
      debounce,
      fetchImpl,
    });
    sendJson(res, result.status, result.body);
    return;
  }

  sendJson(res, 404, { ok: false, error: "Não encontrado." });
}

function sendJson(res: ServerResponse, status: number, body: Record<string, unknown>): void {
  res.writeHead(status, { "Content-Type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(body));
}

const isMain = process.argv[1] === fileURLToPath(import.meta.url);
if (isMain) {
  startTurnstileHttpServer(loadTurnstileHttpConfig());
}
