// @ts-check
import js from "@eslint/js";
import tseslint from "typescript-eslint";

// Frontend lint (ESLint + typescript-eslint, flat config). Generated code
// (wailsjs bindings), build output, and the copied DB are ignored.
export default tseslint.config(
  {
    ignores: ["dist", "wailsjs", "node_modules", "material_database.json"],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    rules: {
      // The serial/HTTP layer deals in dynamic JSON, so `any` at those
      // boundaries (Reply, invoke wrappers) is intentional.
      "@typescript-eslint/no-explicit-any": "off",
    },
  },
);
