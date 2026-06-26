# Build from parent folder that contains viaaccess-bridge/ and viaaccess/:
#   docker build -f viaaccess-bridge/Dockerfile -t vialabs/viaaccess-bridge:local .
FROM node:22-alpine AS build
WORKDIR /app
COPY viaaccess-bridge/package.json viaaccess-bridge/tsconfig.json ./
COPY viaaccess-bridge/src ./src
COPY viaaccess/packages/client /client
RUN npm pkg set dependencies.@viaaccess/client="file:/client" \
  && npm install \
  && npm run build

FROM node:22-alpine
WORKDIR /app
ENV NODE_ENV=production
COPY --from=build /app/dist ./dist
COPY --from=build /app/node_modules ./node_modules
COPY viaaccess-bridge/package.json ./
RUN mkdir -p /data
VOLUME ["/data"]
CMD ["node", "dist/main.js"]
