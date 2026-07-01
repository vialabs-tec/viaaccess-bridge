import { describe, expect, it, beforeEach } from "vitest";
import { decidePresenceSession, resetPresenceSessions } from "./presence-session.js";

describe("decidePresenceSession", () => {
  beforeEach(() => {
    resetPresenceSessions();
  });

  it("emits on first sighting in zone", () => {
    const first = decidePresenceSession({
      camera: "salao",
      zone: "interior",
      accessPoint: "interior-principal",
      trackId: "t1",
      frameTime: 100,
      gapSeconds: 20,
    });
    expect(first.emit).toBe(true);

    const second = decidePresenceSession({
      camera: "salao",
      zone: "interior",
      accessPoint: "interior-principal",
      trackId: "t1",
      frameTime: 110,
      gapSeconds: 20,
    });
    expect(second.emit).toBe(false);
  });

  it("starts a new session after gap seconds", () => {
    decidePresenceSession({
      camera: "salao",
      zone: "interior",
      accessPoint: "interior-principal",
      trackId: "t1",
      frameTime: 100,
      gapSeconds: 20,
    });

    const afterGap = decidePresenceSession({
      camera: "salao",
      zone: "interior",
      accessPoint: "interior-principal",
      trackId: "t1",
      frameTime: 125,
      gapSeconds: 20,
    });
    expect(afterGap.emit).toBe(true);
  });

  it("suppresses a new track within the gap (webcam track churn)", () => {
    decidePresenceSession({
      camera: "salao",
      zone: "interior",
      accessPoint: "interior-principal",
      trackId: "t1",
      frameTime: 100,
      gapSeconds: 20,
    });

    const otherTrack = decidePresenceSession({
      camera: "salao",
      zone: "interior",
      accessPoint: "interior-principal",
      trackId: "t2",
      frameTime: 105,
      gapSeconds: 20,
    });
    expect(otherTrack.emit).toBe(false);
  });
});
