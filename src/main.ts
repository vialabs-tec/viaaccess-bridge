#!/usr/bin/env node
/**
 * ViaAccess Bridge: Frigate MQTT → VADP → ViaAccess Cloud.
 * Runs on the client network next to Frigate (Docker or bare metal).
 */
import mqtt from "mqtt";
import { translateFrigateToVadp } from "./frigate/to-vadp.js";
import type { FrigateEventsMessage } from "./frigate/types.js";
import type { VadpDetectionEvent } from "./vadp-types.js";
import { Outbox } from "./outbox.js";
import { writeBridgeStatus } from "./status.js";

const apiUrl = process.env.VIAACCESS_API_URL?.trim()?.replace(/\/$/, "");
const apiKey = process.env.VIAACCESS_API_KEY?.trim();
const mqttUrl = process.env.FRIGATE_MQTT_URL?.trim() ?? "mqtt://127.0.0.1:1883";
const topicPrefix = process.env.FRIGATE_MQTT_TOPIC_PREFIX?.trim() || "frigate";
const eventsTopic = `${topicPrefix}/events`;
const outboxPath = process.env.OUTBOX_PATH?.trim() || "./data/outbox.jsonl";
const flushIntervalMs = Number(process.env.OUTBOX_FLUSH_INTERVAL_MS ?? "30000");

if (!apiUrl || !apiKey) {
  console.error("[viaaccess-bridge] VIAACCESS_API_URL and VIAACCESS_API_KEY are required");
  process.exit(1);
}

const outbox = new Outbox(outboxPath);

function status(partial: Record<string, unknown>) {
  writeBridgeStatus({ running: true, mqttUrl, apiUrl, ...partial });
}

status({ connected: false });

async function forwardDetection(
  event: VadpDetectionEvent,
  idempotencyKey?: string,
): Promise<boolean> {
  try {
    const response = await fetch(`${apiUrl}/api/v1/detections`, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${apiKey}`,
        "Content-Type": "application/json",
        Accept: "application/json",
        ...(idempotencyKey ? { "Idempotency-Key": idempotencyKey } : {}),
      },
      body: JSON.stringify(event),
    });

    const text = await response.text();
    if (!response.ok) {
      console.error(`[viaaccess-bridge] ViaAccess ${response.status}: ${text}`);
      status({ connected: true, lastError: text, lastMqttAt: new Date().toISOString() });
      return false;
    }

    console.info(
      `[viaaccess-bridge] VADP ${event.event} → ${event.accessPointId} (${response.status})`,
    );
    status({
      connected: true,
      lastDetectionEvent: event.event,
      lastAccessPointId: event.accessPointId,
      lastForwardStatus: response.status,
      lastMqttAt: new Date().toISOString(),
      lastForwardAt: new Date().toISOString(),
    });
    return true;
  } catch (error) {
    console.error("[viaaccess-bridge] forward failed", error);
    status({ connected: true, lastError: String(error) });
    return false;
  }
}

async function deliver(entry: { idempotencyKey: string; body: unknown }): Promise<boolean> {
  return forwardDetection(entry.body as VadpDetectionEvent, entry.idempotencyKey);
}

setInterval(() => {
  void outbox.flush(deliver);
}, flushIntervalMs);

const client = mqtt.connect(mqttUrl);

client.on("connect", () => {
  console.info(`[viaaccess-bridge] connected to ${mqttUrl}, subscribing ${eventsTopic}`);
  client.subscribe(eventsTopic);
  status({ connected: true });
  void outbox.flush(deliver);
});

client.on("message", (_topic, buffer) => {
  let payload: FrigateEventsMessage;
  try {
    payload = JSON.parse(buffer.toString("utf8")) as FrigateEventsMessage;
  } catch {
    console.warn("[viaaccess-bridge] invalid JSON, skipping");
    return;
  }

  const { events, ignoredReason } = translateFrigateToVadp(payload);
  if (!events.length) {
    if (ignoredReason && ignoredReason !== "end_event") {
      status({
        connected: true,
        lastIgnoredReason: ignoredReason,
        lastMqttAt: new Date().toISOString(),
      });
    }
    return;
  }

  void (async () => {
    for (const event of events) {
      const providerEventId = event.metadata?.providerEventId;
      const idempotencyKey =
        typeof providerEventId === "string" && providerEventId
          ? `frigate:${providerEventId}:${event.event}:${event.accessPointId}`
          : crypto.randomUUID();

      const ok = await forwardDetection(event, idempotencyKey);
      if (!ok) {
        await outbox.enqueue({ idempotencyKey, body: event });
      }
    }
  })();
});

client.on("error", (error) => {
  console.error("[viaaccess-bridge] mqtt error", error);
  status({ connected: false, lastError: String(error) });
});

process.on("SIGINT", () => {
  writeBridgeStatus({ running: false, connected: false });
  process.exit(0);
});
