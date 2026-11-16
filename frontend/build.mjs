// Production build: bundles src/main.tsx with esbuild into dist/.
import { build } from "esbuild";
import { cpSync, mkdirSync, rmSync } from "node:fs";

rmSync("dist", { recursive: true, force: true });
mkdirSync("dist", { recursive: true });
cpSync("public", "dist", { recursive: true });

await build({
  entryPoints: ["src/main.tsx"],
  bundle: true,
  minify: true,
  sourcemap: true,
  target: ["es2020"],
  outfile: "dist/app.js",
  loader: { ".css": "css" },
  define: { "process.env.NODE_ENV": '"production"' },
  logLevel: "info",
});

// On Vercel, refuse to build while vercel.json still points /api at the
// placeholder host: the app would load but every sign-in would fail.
if (process.env.VERCEL) {
  const { readFileSync } = await import("node:fs");
  if (readFileSync("vercel.json", "utf8").includes("clinicflow-api.example.invalid")) {
    console.error(
      "\nvercel.json still sends /api to the placeholder clinicflow-api.example.invalid.\n" +
        "Replace it with the HTTPS address of your ClinicFlow backend (see docs/DEPLOY_VERCEL.md).\n",
    );
    process.exit(1);
  }
}
