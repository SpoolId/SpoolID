import { defineConfig } from "vite";

// Wails serves the built frontend from the embedded frontend/dist over its own
// asset protocol, so keep asset paths relative and don't auto-open a browser.
export default defineConfig({
  base: "./",
  clearScreen: false,
  build: {
    target: "es2021",
    outDir: "dist",
    emptyOutDir: true,
  },
});
