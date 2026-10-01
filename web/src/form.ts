// Typed access to the controls form.
export type Field = HTMLInputElement | HTMLSelectElement;

export function field(form: HTMLFormElement, name: string): Field {
  const element = form.elements.namedItem(name);
  if (!(element instanceof HTMLInputElement || element instanceof HTMLSelectElement)) {
    throw new Error(`Missing form field ${name}`);
  }
  return element;
}

export function element<T extends HTMLElement>(id: string): T {
  const found = document.getElementById(id);
  if (!found) throw new Error(`Missing element #${id}`);
  return found as T;
}
