import { loadConfig, type ReaderConfig } from "./config.js";

export type TurnstileHttpConfig = ReaderConfig & {
  host: string;
  port: number;
  webhookSecret?: string;
  unlockWebhookUrl?: string;
  unlockOnAuthorizedOnly: boolean;
};

export function loadTurnstileHttpConfig(env: NodeJS.ProcessEnv = process.env): TurnstileHttpConfig {
  const reader = loadConfig(env);

  const portRaw = Number(env.HTTP_PORT ?? 3710);
  const port = Number.isFinite(portRaw) && portRaw > 0 ? portRaw : 3710;
  const host = env.HTTP_HOST?.trim() || "0.0.0.0";

  const webhookSecret = env.WEBHOOK_SECRET?.trim() || undefined;
  const unlockWebhookUrl = env.UNLOCK_WEBHOOK_URL?.trim().replace(/\/$/, "") || undefined;
  const unlockOnAuthorizedOnly = env.UNLOCK_ON_AUTHORIZED_ONLY !== "false";

  return {
    ...reader,
    host,
    port,
    webhookSecret,
    unlockWebhookUrl,
    unlockOnAuthorizedOnly,
  };
}
