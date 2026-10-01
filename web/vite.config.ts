import { defineConfig } from "vite";

// Relative asset paths, so the built site works from any subpath (e.g. GitHub Pages).
export default defineConfig({
  base: "./",
  // The exclusion map lives in the repository's data/ directory.
  server: { fs: { allow: [".."] } },
});
