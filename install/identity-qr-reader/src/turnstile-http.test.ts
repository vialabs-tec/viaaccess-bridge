import { describe, expect, it, vi } from "vitest";
import {
  createDebounceState,
  extractQrUrlFromBody,
  handleScanRequest,
} from "./turnstile-http.js";
import type { TurnstileHttpConfig } from "./turnstile-http-config.js";

const baseConfig: TurnstileHttpConfig = {
  identityUrl: "http://localhost:3100",
  deviceKey: "idb_test",
  emitDetection: true,
  debounceMs: 2000,
  host: "127.0.0.1",
  port: 3710,
  unlockOnAuthorizedOnly: true,
  unlockWebhookUrl: "http://turnstile.local/unlock",
};

describe("extractQrUrlFromBody", () => {
  it("accepts qrUrl, qr, or raw string", () => {
    expect(extractQrUrlFromBody({ qrUrl: "http://x/r/1?t=a" })).toBe("http://x/r/1?t=a");
    expect(extractQrUrlFromBody({ qr: "http://x/r/2?t=b" })).toBe("http://x/r/2?t=b");
    expect(extractQrUrlFromBody("http://x/r/3?t=c")).toBe("http://x/r/3?t=c");
  });
});

describe("handleScanRequest", () => {
  it("redeems and calls unlock webhook when authorized", async () => {
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce({
        ok: true,
        json: async () => ({
          ok: true,
          validationId: "val_1",
          memberId: "mem_1",
          correlationOutcome: "AUTHORIZED",
          accessPointSlug: "entrada",
        }),
      })
      .mockResolvedValueOnce({ ok: true, status: 200 });

    const result = await handleScanRequest({
      config: baseConfig,
      body: { qrUrl: "http://localhost:3100/r/i1?t=tok" },
      debounce: createDebounceState(),
      fetchImpl: fetchMock as typeof fetch,
    });

    expect(result.status).toBe(200);
    expect(result.body.ok).toBe(true);
    expect(fetchMock).toHaveBeenCalledTimes(2);
    expect(fetchMock.mock.calls[1]?.[0]).toBe("http://turnstile.local/unlock");
  });

  it("skips unlock when correlation is not authorized", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        ok: true,
        validationId: "val_1",
        correlationOutcome: "UNAUTHORIZED",
      }),
    });

    const result = await handleScanRequest({
      config: baseConfig,
      body: { qr: "http://localhost:3100/r/i1?t=tok" },
      debounce: createDebounceState(),
      fetchImpl: fetchMock as typeof fetch,
    });

    expect(result.status).toBe(200);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(result.body.unlock).toBeUndefined();
  });

  it("rejects missing webhook secret when configured", async () => {
    const result = await handleScanRequest({
      config: { ...baseConfig, webhookSecret: "secret-1" },
      body: { qrUrl: "http://x" },
      webhookSecretHeader: "wrong",
      debounce: createDebounceState(),
    });

    expect(result.status).toBe(401);
  });
});
