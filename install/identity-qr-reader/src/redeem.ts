import type { ReaderConfig } from "./config.js";

export type RedeemResponse = {
  ok?: boolean;
  redeemed?: boolean;
  validationId?: string;
  detectionId?: string | null;
  memberId?: string;
  correlationOutcome?: string | null;
  error?: string;
  code?: string;
};

export type RedeemResult =
  | { ok: true; data: RedeemResponse }
  | { ok: false; status: number; data: RedeemResponse };

export async function redeemQrUrl(
  config: Pick<ReaderConfig, "identityUrl" | "deviceKey" | "emitDetection">,
  qrUrl: string,
  fetchImpl: typeof fetch = fetch,
): Promise<RedeemResult> {
  const res = await fetchImpl(`${config.identityUrl}/api/bridge/intent/redeem`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${config.deviceKey}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      qrUrl: qrUrl.trim(),
      emitDetection: config.emitDetection,
    }),
  });

  const data = (await res.json().catch(() => ({}))) as RedeemResponse;
  if (!res.ok) {
    return { ok: false, status: res.status, data };
  }
  return { ok: true, data };
}

export function formatRedeemLog(result: RedeemResult): string {
  if (result.ok) {
    const parts = [
      "OK",
      result.data.validationId ? `validation=${result.data.validationId}` : null,
      result.data.memberId ? `member=${result.data.memberId}` : null,
      result.data.correlationOutcome ? `correlation=${result.data.correlationOutcome}` : null,
      result.data.redeemed ? "(idempotente)" : null,
    ].filter(Boolean);
    return parts.join(" ");
  }
  const code = result.data.code ? ` [${result.data.code}]` : "";
  return `ERRO ${result.status}: ${result.data.error ?? "Falha no resgate."}${code}`;
}
