# ClinicFlow web client: build with esbuild, serve with nginx (which also
# reverse-proxies /api to the backend so the browser sees a single origin).
FROM node:22-alpine AS build
WORKDIR /app
COPY package.json ./
RUN npm install --no-audit --no-fund
COPY tsconfig.json build.mjs ./
COPY public ./public
COPY src ./src
RUN npx tsc --noEmit -p . && node build.mjs

FROM nginx:1.27-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 8080
