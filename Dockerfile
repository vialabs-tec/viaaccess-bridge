# syntax=docker/dockerfile:1

# Compile on the builder host. Running npm/tsc under qemu-user for linux/arm64
# hits SIGILL (exit 132) on current node:22-alpine images. Runtime deps are
# pure JS (mqtt), so the pruned node_modules copy is safe across architectures.
FROM --platform=$BUILDPLATFORM node:22-alpine AS build
WORKDIR /app
COPY package.json package-lock.json tsconfig.json ./
COPY src ./src
RUN npm ci \
  && npm run build \
  && npm prune --omit=dev

FROM node:22-alpine
WORKDIR /app
ENV NODE_ENV=production
COPY --from=build /app/dist ./dist
COPY --from=build /app/node_modules ./node_modules
COPY package.json ./
RUN mkdir -p /data /runtime
VOLUME ["/data", "/runtime"]
CMD ["node", "dist/main.js"]
