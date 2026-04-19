import jsd from "@eslint/js";

export default [
  {
    ...js.configs.recommended,
    ignores: ["assets/libs/"],
    rules: {
      "no-console": "warn",
      "no-unused-vars": "warn",
    },
  },
];