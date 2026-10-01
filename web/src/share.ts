// Settings in the URL hash, so a link reproduces a view: #re=-0.1&im=0.75&span=0.004&lowr=1024.
// The window is its centre and span, the length of the viewport's shorter side in complex units,
// so a link shows the same region at any screen size or pixel ratio.
export type Values = Record<string, number | string>;

// Writes the names in always, then every other value that differs from its default. Numbers use
// the shortest text that round-trips, so doubles keep full precision.
export function encodeHash(values: Values, defaults: Values, always: readonly string[]) {
  const params = new URLSearchParams();
  for (const [name, value] of Object.entries(values)) {
    if (always.includes(name) || value !== defaults[name]) params.set(name, String(value));
  }
  return `#${params}`;
}

// Reads the names known from defaults, as numbers where the default is one. Unknown names and
// numbers that do not parse are dropped; ranges and select options are for the caller to check.
export function decodeHash(hash: string, defaults: Values): Partial<Values> {
  const params = new URLSearchParams(hash.replace(/^#/, ""));
  const values: Partial<Values> = {};
  for (const [name, fallback] of Object.entries(defaults)) {
    const text = params.get(name)?.trim();
    if (!text) continue;
    if (typeof fallback === "string") values[name] = text;
    else if (Number.isFinite(Number(text))) values[name] = Number(text);
  }
  return values;
}
