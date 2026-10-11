// Canvas size and the clockwise-rotated complex-plane view. Navigation updates the window fields
// and calls viewChanged after every move, so the sampler follows the gesture. The view is its
// centre and span, the complex units across the shorter side, so it does not depend on the size
// of the window or on how many pixels render it.
import { field } from "./form";

export interface Size {
  width: number;
  height: number;
}

// Sizes here are of the viewport on screen, in CSS pixels: the image fills it.
interface View extends Size {
  cre: number;
  cim: number;
  span: number;
}

interface Point {
  x: number;
  y: number;
}

interface Gesture extends Point {
  view: View;
  distance?: number; // between two pointers, for a pinch
  moved: boolean;
}

export function sameSize(a: Size, b: Size) {
  return a.width === b.width && a.height === b.height;
}

// The largest size of the same shape within limits.pixels pixels and limits.side on either side,
// for screens larger than the GPU buffers or textures can hold.
export function fitPixels(size: Size, limits: { pixels: number; side: number }): Size {
  const factor = Math.min(
    Math.sqrt(limits.pixels / (size.width * size.height)),
    limits.side / size.width,
    limits.side / size.height
  );
  if (factor >= 1) return size;
  return {
    width: Math.max(1, Math.floor(size.width * factor)),
    height: Math.max(1, Math.floor(size.height * factor)),
  };
}

// Calls changed with the size of element on screen in device pixels, now and whenever it changes,
// including when only the pixel ratio does (browser zoom, another monitor). Browsers without
// device-pixel-content-box get the CSS size times the ratio, rounded, and a media query for the
// ratio. The canvas is sized by CSS and its backing store never takes part in layout, so setting
// the backing store to this size cannot change it.
export function observeDevicePixels(element: HTMLElement, changed: (size: Size) => void) {
  let last: Size = { width: 0, height: 0 };
  let css: Size | undefined;
  const report = (size: Size) => {
    if (!size.width || !size.height || sameSize(size, last)) return; // hidden, or no change
    last = size;
    changed(size);
  };
  const fromCss = () => {
    if (!css) return;
    const ratio = globalThis.devicePixelRatio || 1;
    report({ width: Math.round(css.width * ratio), height: Math.round(css.height * ratio) });
  };
  const observer = new ResizeObserver(([entry]) => {
    const device = entry.devicePixelContentBoxSize?.[0];
    if (device) return report({ width: device.inlineSize, height: device.blockSize });
    const box = entry.contentBoxSize[0];
    css = { width: box.inlineSize, height: box.blockSize };
    fromCss();
  });
  try {
    observer.observe(element, { box: "device-pixel-content-box" });
  } catch {
    observer.observe(element);
  }
  // Only acts once the observer has fallen back to the CSS size.
  const watchRatio = () => {
    matchMedia(`(resolution: ${globalThis.devicePixelRatio || 1}dppx)`).addEventListener(
      "change",
      () => {
        fromCss();
        watchRatio();
      },
      { once: true }
    );
  };
  watchRatio();
}

export function attachGeometry({
  form,
  viewport,
  validSettings,
  viewChanged,
}: {
  form: HTMLFormElement;
  viewport: HTMLElement;
  validSettings: (report: boolean) => boolean;
  viewChanged: () => void;
}) {
  const pointers = new Map<number, Point>();
  let gesture: Gesture | undefined;

  function viewState(): View {
    const rect = viewport.getBoundingClientRect();
    return {
      width: rect.width,
      height: rect.height,
      cre: Number(field(form, "cre").value),
      cim: Number(field(form, "cim").value),
      span: Number(field(form, "span").value),
    };
  }

  // Complex units per CSS pixel at the given span.
  const unit = (view: View, span = view.span) => span / Math.min(view.width, view.height);

  function viewPoint(clientX: number, clientY: number): Point {
    const rect = viewport.getBoundingClientRect();
    return { x: clientX - rect.left, y: clientY - rect.top };
  }

  function complexPoint(point: Point, view: View) {
    // The TIFF orientation maps image y to real and image x to imaginary.
    return {
      re: view.cre + (point.y - view.height / 2) * unit(view),
      im: view.cim + (point.x - view.width / 2) * unit(view),
    };
  }

  function setView(cre: number, cim: number, span: number) {
    if (![cre, cim, span].every(Number.isFinite) || span <= 0) return false;
    field(form, "cre").value = String(Number(cre.toPrecision(15)));
    field(form, "cim").value = String(Number(cim.toPrecision(15)));
    field(form, "span").value = String(Number(span.toPrecision(15)));
    return true;
  }

  // Moves the view so that anchor, a complex point, lies under point at the new span.
  function anchorView(anchor: { re: number; im: number }, point: Point, view: View, span: number) {
    return setView(
      anchor.re - (point.y - view.height / 2) * unit(view, span),
      anchor.im - (point.x - view.width / 2) * unit(view, span),
      span
    );
  }

  function pointerMidpoint() {
    const [a, b] = [...pointers.values()];
    return {
      x: (a.x + b.x) / 2,
      y: (a.y + b.y) / 2,
      distance: Math.hypot(a.x - b.x, a.y - b.y),
    };
  }

  function beginGesture() {
    if (!validSettings(false)) return;
    const view = viewState();
    if (pointers.size === 1) {
      const pointer = [...pointers.values()][0];
      gesture = { view, x: pointer.x, y: pointer.y, moved: false };
    } else if (pointers.size === 2) {
      gesture = { view, ...pointerMidpoint(), moved: false };
    }
  }

  viewport.addEventListener("pointerdown", (event) => {
    if (event.pointerType === "mouse" && event.button !== 0) return;
    if (pointers.size >= 2) return;
    viewport.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    viewport.classList.add("dragging");
    beginGesture();
  });

  viewport.addEventListener("pointermove", (event) => {
    if (!pointers.has(event.pointerId) || !gesture) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    const view = gesture.view;
    if (pointers.size === 1 && gesture.distance === undefined) {
      const point = [...pointers.values()][0];
      if (Math.hypot(point.x - gesture.x, point.y - gesture.y) <= 3 && !gesture.moved) return;
      if (
        setView(
          view.cre - (point.y - gesture.y) * unit(view),
          view.cim - (point.x - gesture.x) * unit(view),
          view.span
        )
      ) {
        gesture.moved = true;
        viewChanged();
      }
    } else if (pointers.size === 2 && gesture.distance) {
      const mid = pointerMidpoint();
      const span = (view.span * gesture.distance) / mid.distance;
      const anchor = complexPoint(viewPoint(gesture.x, gesture.y), view);
      if (anchorView(anchor, viewPoint(mid.x, mid.y), view, span)) {
        gesture.moved = true;
        viewChanged();
      }
    }
  });

  function endPointer(event: PointerEvent) {
    if (!pointers.delete(event.pointerId)) return;
    gesture = undefined;
    if (pointers.size) beginGesture();
    else viewport.classList.remove("dragging");
  }
  viewport.addEventListener("pointerup", endPointer);
  viewport.addEventListener("pointercancel", endPointer);

  viewport.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      if (!validSettings(false)) return;
      const view = viewState();
      const point = viewPoint(event.clientX, event.clientY);
      const anchor = complexPoint(point, view);
      const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? 800 : 1);
      const span = view.span * Math.exp(Math.max(-2, Math.min(2, delta * 0.002)));
      if (anchorView(anchor, point, view, span)) viewChanged();
    },
    { passive: false }
  );
}
