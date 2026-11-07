// Development server: rebuilds on change, serves dist/, and proxies /api to the
// backend so the browser sees a single origin (same as nginx in production).
//   API_TARGET=http://localhost:8000 PORT=5173 node dev-server.mjs
import { context } from "esbuild";
import { cpSync, mkdirSync, readFileSync, existsSync } from "node:fs";
import http from "node:http";
import path from "node:path";

const API_TARGET = new URL(process.env.API_TARGET || "http://localhost:8000");
const PORT = Number(process.env.PORT || 5173);

mkdirSync("dist", { recursive: true });
cpSync("public", "dist", { recursive: true });
const ctx = await context({
  entryPoints: ["src/main.tsx"],
  bundle: true,
  sourcemap: true,
  outfile: "dist/app.js",
  define: { "process.env.NODE_ENV": '"development"' },
  logLevel: "info",
});
await ctx.watch();

const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".map": "application/json", ".svg": "image/svg+xml" };

http
  .createServer((req, res) => {
    if (req.url.startsWith("/api/") || req.url.startsWith("/swagger") || req.url.startsWith("/api-docs")) {
      const proxied = http.request(
        { host: API_TARGET.hostname, port: API_TARGET.port, path: req.url, method: req.method, headers: req.headers },
        (r) => {
          res.writeHead(r.statusCode, r.headers);
          r.pipe(res);
        },
      );
      proxied.on("error", () => {
        res.writeHead(502, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: { status: 502, code: "BACKEND_UNAVAILABLE", message: "API server is not reachable" } }));
      });
      req.pipe(proxied);
      return;
    }
    const clean = decodeURIComponent(req.url.split("?")[0]);
    let file = path.join("dist", clean === "/" ? "index.html" : clean);
    if (!file.startsWith("dist") || !existsSync(file)) file = "dist/index.html";
    res.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream" });
    res.end(readFileSync(file));
  })
  .listen(PORT, () => console.log(`ClinicFlow dev server: http://localhost:${PORT} (API → ${API_TARGET.origin})`));
