import { describe, expect, it, vi } from "vitest";
import { formatRedeemLog, redeemQrUrl } from "./redeem.js";

describe("redeemQrUrl", () => {
  it("posts qrUrl to Identity bridge redeem", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        ok: true,
        validationId: "val_1",
        memberId: "mem_1",
        correlationOutcome: "AUTHORIZED",
      }),
    });

    const result = await redeemQrUrl(
      {
        identityUrl: "http://localhost:3100",
        deviceKey: "idb_test_key",
        emitDetection: true,
      },
      "http://localhost:3100/r/intent1?t=secret",
      fetchMock as typeof fetch,
    );

    expect(result.ok).toBe(true);
    expect(fetchMock).toHaveBeenCalledWith(
      "http://localhost:3100/api/bridge/intent/redeem",
      expect.objectContaining({
        method: "POST",
        headers: expect.objectContaining({
          Authorization: "Bearer idb_test_key",
        }),
      }),
    );
    const body = JSON.parse(String(fetchMock.mock.calls[0]?.[1]?.body));
    expect(body.qrUrl).toContain("/r/intent1");
    expect(body.emitDetection).toBe(true);
  });

  it("formats error responses in Portuguese", () => {
    const line = formatRedeemLog({
      ok: false,
      status: 403,
      data: { error: "QR expirado.", code: "INTENT_EXPIRED" },
    });
    expect(line).toContain("ERRO 403");
    expect(line).toContain("INTENT_EXPIRED");
  });
});
