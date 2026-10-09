// Run the shipped TypeScript with Node's test runner, using the compiler already required by
// the explorer. Each test gets an isolated context and can replace browser/GPU boundaries.
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath, URL, URLSearchParams } from "node:url";
import { createContext, Script } from "node:vm";
import ts from "typescript";

const sourceRoot = fileURLToPath(new URL("../src/", import.meta.url));

export function loadSource(name, globals = {}, stubs = {}) {
  const context = createContext({ URLSearchParams, Error, ...globals });
  const cache = new Map();
  function load(filename) {
    if (cache.has(filename)) return cache.get(filename);
    const source = readFileSync(filename, "utf8").replaceAll(
      "import.meta.url",
      JSON.stringify(`file://${filename}`)
    );
    const { outputText } = ts.transpileModule(source, {
      compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
      fileName: filename,
    });
    const exports = {};
    cache.set(filename, exports);
    const require = (specifier) => {
      if (Object.hasOwn(stubs, specifier)) return stubs[specifier];
      return load(resolve(dirname(filename), `${specifier}.ts`));
    };
    const run = new Script(`(function (exports, require) { ${outputText}\n})`, { filename });
    run.runInContext(context)(exports, require);
    return exports;
  }
  return load(resolve(sourceRoot, name));
}
