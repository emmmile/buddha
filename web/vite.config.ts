import { defineConfig } from "vite";

// Relative asset paths, so the built site works from any subpath (e.g. GitHub Pages).
export default defineConfig({
  base: "./",
  // The exclusion map lives in the repository's data/ directory.
  server: { fs: { allow: [".."] } },
  // The WebP encoder loads its WebAssembly with dynamic imports, which workers can only follow as
  // ES modules, and pre-bundling would lose the .wasm files it finds next to itself.
  worker: { format: "es" },
  optimizeDeps: { exclude: ["@jsquash/webp"] },
});
