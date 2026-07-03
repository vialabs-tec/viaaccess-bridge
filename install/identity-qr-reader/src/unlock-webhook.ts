export type UnlockWebhookPayload = {
  memberId?: string;
  validationId?: string;
  detectionId?: string | null;
  correlationOutcome?: string | null;
  accessPointSlug?: string;
};

export type UnlockWebhookResult =
  | { ok: true; status: number }
  | { ok: false; status: number; error: string };

/** Optional POST to a local turnstile controller after a successful redeem. */
export async function postUnlockWebhook(
  unlockWebhookUrl: string,
  payload: UnlockWebhookPayload,
  fetchImpl: typeof fetch = fetch,
): Promise<UnlockWebhookResult> {
  try {
    const res = await fetchImpl(unlockWebhookUrl, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!res.ok) {
      return { ok: false, status: res.status, error: `Unlock webhook HTTP ${res.status}` };
    }
    return { ok: true, status: res.status };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return { ok: false, status: 0, error: message };
  }
}
