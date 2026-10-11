// Defaults, numeric limits and conversions shared by the form, links and render snapshots.
import type { DisplaySettings } from "./display";
import { field } from "./form";
import type { SamplerSettings } from "./sampler";
import { decodeHash, encodeHash } from "./share";

interface NumberField {
  value: number;
  min?: number;
  max?: number;
  step: number | "any";
}

const numbers = {
  cre: { value: 0, step: "any" },
  cim: { value: 0, step: "any" },
  span: { value: 4, min: 1e-12, step: "any" },
  lowr: { value: 512, min: 0, max: 99999, step: 1 },
  highr: { value: 8192, min: 1, max: 100000, step: 1 },
  lowg: { value: 128, min: 0, max: 99999, step: 1 },
  highg: { value: 2048, min: 1, max: 100000, step: 1 },
  lowb: { value: 32, min: 0, max: 99999, step: 1 },
  highb: { value: 512, min: 1, max: 100000, step: 1 },
  radius: { value: 1, min: 0.001, max: 100, step: "any" },
  exponent_l: { value: 1, min: 0, max: 8, step: "any" },
  exponent_c: { value: 1, min: 0, max: 8, step: "any" },
  chain_scale: { value: 1, min: 0.001, max: 100, step: "any" },
  exposure: { value: 0.2, min: -2, max: 2, step: 0.05 },
  gamma: { value: 0.3, min: 0.1, max: 1, step: 0.01 },
  saturation: { value: 100, min: -100, max: 100, step: 1 },
  texture: { value: 25, min: -100, max: 100, step: 1 },
  sharpness: { value: 75, min: -100, max: 100, step: 1 },
} satisfies Record<string, NumberField>;

// In the order of the shader's p.seeding and p.sampler.
const seedings = ["walk", "uniform", "newton"] as const;
const samplers = ["metropolis", "naive"] as const;
export const displayNames = ["exposure", "gamma", "saturation", "texture", "sharpness"] as const;
const metropolisNames = ["radius", "exponent_l", "exponent_c", "chain_scale", "seeding"];
type NumberName = keyof typeof numbers;
export type Settings = Record<NumberName, number> & {
  sampler: (typeof samplers)[number];
  seeding: (typeof seedings)[number];
};

export const defaults: Readonly<Settings> = {
  ...(Object.fromEntries(
    Object.entries(numbers).map(([name, rule]) => [name, rule.value])
  ) as Record<NumberName, number>),
  sampler: "metropolis",
  seeding: "newton",
};

export function isDisplayName(name: string): name is (typeof displayNames)[number] {
  return (displayNames as readonly string[]).includes(name);
}

function validNumber(value: number, rule: NumberField) {
  if (!Number.isFinite(value)) return false;
  if (rule.min !== undefined && value < rule.min) return false;
  if (rule.max !== undefined && value > rule.max) return false;
  if (rule.step === "any") return true;
  const steps = (value - (rule.min ?? 0)) / rule.step;
  return Math.abs(steps - Math.round(steps)) < 1e-8;
}

export function configureControls(form: HTMLFormElement) {
  for (const [name, rule] of Object.entries(numbers) as [NumberName, NumberField][]) {
    const input = field(form, name) as HTMLInputElement;
    input.min = rule.min === undefined ? "" : String(rule.min);
    input.max = rule.max === undefined ? "" : String(rule.max);
    input.step = String(rule.step);
    input.required = true;
  }
  writeSettings(form, defaults);
}

export function writeSettings(form: HTMLFormElement, settings: Readonly<Settings>) {
  for (const [name, value] of Object.entries(settings)) {
    const input = field(form, name);
    input.setCustomValidity("");
    input.value = String(value);
  }
}

// Disabled Metropolis fields can contain unfinished edits. Preserve the last committed values
// for those fields; active fields always have to pass validation before becoming render settings.
export function readSettings(form: HTMLFormElement, previous: Settings, report = false) {
  const settings = { ...previous };
  const sampler = field(form, "sampler").value;
  let valid = true;
  for (const [name, rule] of Object.entries(numbers) as [NumberName, NumberField][]) {
    const input = field(form, name) as HTMLInputElement;
    input.setCustomValidity("");
    if (sampler === "naive" && metropolisNames.includes(name)) continue;
    const value = input.valueAsNumber;
    if (!validNumber(value, rule)) {
      input.setCustomValidity("Enter a number within the allowed range and step");
      valid = false;
    } else settings[name] = value;
  }
  for (const [name, options] of [
    ["sampler", samplers],
    ["seeding", seedings],
  ] as const) {
    const input = field(form, name);
    input.setCustomValidity("");
    if (sampler === "naive" && name === "seeding") continue;
    if (!(options as readonly string[]).includes(input.value)) {
      input.setCustomValidity("Choose a supported option");
      valid = false;
    } else Object.assign(settings, { [name]: input.value });
  }
  for (const color of ["r", "g", "b"] as const) {
    if (settings[`low${color}`] >= settings[`high${color}`]) {
      field(form, `high${color}`).setCustomValidity("Maximum must exceed minimum");
      valid = false;
    }
  }
  if (!valid && report) form.reportValidity();
  return valid ? settings : undefined;
}

export function displaySettings(settings: Readonly<Settings>): DisplaySettings {
  const { exposure, gamma, saturation, texture, sharpness } = settings;
  return { exposure, gamma, saturation, texture, sharpness };
}

export function samplerValues(settings: Readonly<Settings>) {
  const {
    exposure: _e,
    gamma: _g,
    saturation: _s,
    texture: _t,
    sharpness: _h,
    ...values
  } = settings;
  return values;
}

export function samplerSettings(
  settings: Readonly<Settings>,
  width: number,
  height: number
): SamplerSettings {
  const { span, sampler, seeding, ...values } = samplerValues(settings);
  return {
    ...values,
    scale: Math.min(width, height) / span,
    width,
    height,
    sampler: samplers.indexOf(sampler),
    seeding: seedings.indexOf(seeding),
  };
}

const { cre, cim, ...rest } = defaults;
const hashDefaults = { re: cre, im: cim, ...rest };

export function settingsHash(settings: Readonly<Settings>) {
  const { cre, cim, ...values } = settings;
  return encodeHash({ re: cre, im: cim, ...values }, hashDefaults, ["re", "im", "span"]);
}

// Validate URL values independently of the form, including dormant sampler settings. Invalid
// values retain their defaults, and existing link names and export metadata stay compatible.
export function settingsFromHash(hash: string): Settings {
  const { re, im, ...values } = decodeHash(hash, hashDefaults);
  const candidate: Record<string, number | string | undefined> = { cre: re, cim: im, ...values };
  const settings = { ...defaults };
  for (const [name, rule] of Object.entries(numbers) as [NumberName, NumberField][]) {
    const value = candidate[name];
    if (typeof value === "number" && validNumber(value, rule)) settings[name] = value;
  }
  if (samplers.includes(candidate.sampler as Settings["sampler"])) {
    settings.sampler = candidate.sampler as Settings["sampler"];
  }
  if (seedings.includes(candidate.seeding as Settings["seeding"])) {
    settings.seeding = candidate.seeding as Settings["seeding"];
  }
  for (const color of ["r", "g", "b"] as const) {
    if (settings[`low${color}`] >= settings[`high${color}`]) {
      settings[`low${color}`] = defaults[`low${color}`];
      settings[`high${color}`] = defaults[`high${color}`];
    }
  }
  return settings;
}
