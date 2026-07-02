import { defineConfig } from "vite";

// The device serves fixed gzipped filenames from LittleFS, so emit predictable,
// unhashed names: index page -> app.js, config page -> config.js, shared css ->
// style.css. The two entries are self-contained (no shared import) so there's no
// extra chunk for the device to route.
export default defineConfig({
  base: "/",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    minify: "esbuild",
    cssCodeSplit: false,
    // No benefit on a local device app; avoids an extra polyfill chunk the
    // firmware would otherwise have to serve.
    modulePreload: false,
    rollupOptions: {
      input: {
        index: "index.html",
        config: "config.html",
      },
      output: {
        entryFileNames: "[name].js",
        chunkFileNames: "[name].js",
        assetFileNames: "style.css",
      },
    },
  },
});
