import { describe, expect, it } from "vitest";
import { resetPresenceSessions } from "./presence-session.js";
import { translateFrigateToVadp } from "./to-vadp.js";
import type { FrigateEventsMessage } from "./types.js";

const mappings = [
  {
    accessPoint: "entrada-principal",
    camera: "portao-principal",
    zone: "entrada",
    labels: ["person"],
  },
];

function basePayload(overrides: Partial<FrigateEventsMessage> = {}): FrigateEventsMessage {
  return {
    type: "update",
    before: {
      id: "evt-1",
      camera: "portao-principal",
      frame_time: 1_700_000_000,
      label: "person",
      entered_zones: [],
      current_zones: [],
    },
    after: {
      id: "evt-1",
      camera: "portao-principal",
      frame_time: 1_700_000_001,
      label: "person",
      entered_zones: ["entrada"],
      current_zones: ["entrada"],
      score: 0.92,
    },
    ...overrides,
  };
}

describe("translateFrigateToVadp", () => {
  it("returns empty events for end payloads", () => {
    const result = translateFrigateToVadp(basePayload({ type: "end" }), {
      mappings,
      baseUrl: "http://frigate.local:5000",
    });
    expect(result.events).toEqual([]);
    expect(result.ignoredReason).toBe("end_event");
  });

  it("maps zone entry to VADP passage_detected", () => {
    const result = translateFrigateToVadp(basePayload(), {
      mappings,
      baseUrl: "http://frigate.local:5000",
    });

    expect(result.events).toHaveLength(1);
    expect(result.events[0]).toMatchObject({
      version: "1.0",
      provider: "frigate",
      providerType: "detection",
      event: "passage_detected",
      accessPointId: "entrada-principal",
      confidence: 0.92,
      metadata: {
        detectionKind: "line_crossed",
        snapshot: "http://frigate.local:5000/api/events/evt-1/snapshot.jpg",
        camera: "portao-principal",
        providerEventId: "evt-1",
      },
    });
  });

  it("returns empty when camera/zone does not match mapping", () => {
    const result = translateFrigateToVadp(
      basePayload({
        after: {
          ...basePayload().after,
          camera: "piscina",
          entered_zones: ["deck"],
          current_zones: ["deck"],
        },
        before: {
          ...basePayload().before,
          camera: "piscina",
        },
      }),
      { mappings, baseUrl: null },
    );

    expect(result.events).toEqual([]);
    expect(result.ignoredReason).toBe("no_mapping_match");
  });

  it("debounces interior motion with presenceSessionGapSeconds", () => {
    resetPresenceSessions();

    const interiorMappings = [
      {
        accessPoint: "interior-principal",
        camera: "salao",
        zone: "interior",
        labels: ["person"],
        presenceSessionGapSeconds: 20,
      },
    ];

    const enter = translateFrigateToVadp(
      basePayload({
        before: {
          id: "evt-1",
          camera: "salao",
          frame_time: 1_700_000_000,
          label: "person",
          entered_zones: [],
          current_zones: [],
        },
        after: {
          id: "evt-1",
          camera: "salao",
          frame_time: 1_700_000_001,
          label: "person",
          entered_zones: ["interior"],
          current_zones: ["interior"],
          score: 0.9,
        },
      }),
      { mappings: interiorMappings, baseUrl: null },
    );
    expect(enter.events).toHaveLength(1);
    expect(enter.events[0]?.metadata).toMatchObject({
      presencePhase: "session_started",
      frigateZone: "interior",
    });

    const followUp = translateFrigateToVadp(
      basePayload({
        type: "update",
        before: {
          id: "evt-1",
          camera: "salao",
          frame_time: 1_700_000_001,
          label: "person",
          entered_zones: ["interior"],
          current_zones: ["interior"],
        },
        after: {
          id: "evt-1",
          camera: "salao",
          frame_time: 1_700_000_005,
          label: "person",
          entered_zones: ["interior"],
          current_zones: ["interior"],
          score: 0.9,
        },
      }),
      { mappings: interiorMappings, baseUrl: null },
    );
    expect(followUp.events).toEqual([]);
    expect(followUp.ignoredReason).toBe("presence_session_suppressed");
  });

  it("supports camera-only mapping on new events", () => {
    const result = translateFrigateToVadp(
      basePayload({
        type: "new",
        before: {
          id: "evt-2",
          camera: "hall",
          frame_time: 1_700_000_000,
          label: "person",
          entered_zones: [],
          current_zones: [],
        },
        after: {
          id: "evt-2",
          camera: "hall",
          frame_time: 1_700_000_000,
          label: "person",
          entered_zones: [],
          current_zones: [],
        },
      }),
      {
        mappings: [{ accessPoint: "entrada-principal", camera: "hall", labels: ["person"] }],
        baseUrl: null,
      },
    );

    expect(result.events[0]).toMatchObject({
      event: "passage_detected",
      accessPointId: "entrada-principal",
      metadata: { detectionKind: "person_detected" },
    });
  });
});
