FROM node:22-bookworm AS web
WORKDIR /src
RUN corepack enable && corepack prepare pnpm@11.22.0 --activate
COPY package.json pnpm-workspace.yaml pnpm-lock.yaml tsconfig.base.json ./
COPY packages ./packages
RUN pnpm install --frozen-lockfile && pnpm build:web

FROM debian:bookworm-slim AS cpp
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ pkg-config libsqlite3-dev libsodium-dev nlohmann-json3-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY backend ./backend
RUN cmake -S backend -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 4

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libsqlite3-0 libsodium23 ca-certificates && rm -rf /var/lib/apt/lists/* && useradd --uid 10001 --create-home app
WORKDIR /app
COPY --from=cpp /src/build/mistakebook /app/mistakebook
COPY --from=web /src/packages/web/dist /app/web
RUN mkdir /app/data && chown app:app /app/data
USER app
ENV HOST=0.0.0.0 PORT=8080 WEB_ROOT=/app/web DATABASE_PATH=/app/data/mistakebook.db
EXPOSE 8080
VOLUME ["/app/data"]
CMD ["/app/mistakebook"]
