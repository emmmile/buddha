// Canvas size and the clockwise-rotated complex-plane view. Navigation updates the window fields
// and calls viewChanged after every move, so the sampler follows the gesture.
import { field } from "./form";

export interface Size {
  width: number;
  height: number;
}

interface View extends Size {
  cre: number;
  cim: number;
  scale: number;
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

export function viewportPixelsFor(viewport: HTMLElement): Size {
  const ratio = globalThis.devicePixelRatio || 1;
  let width = Math.max(1, Math.round(viewport.clientWidth * ratio));
  let height = Math.max(1, Math.round(viewport.clientHeight * ratio));
  const limit = 2000000;
  if (width * height > limit) {
    const factor = Math.sqrt(limit / (width * height));
    width = Math.max(1, Math.floor(width * factor));
    height = Math.max(1, Math.floor(height * factor));
  }
  return { width, height };
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
    return {
      ...viewportPixelsFor(viewport),
      cre: Number(field(form, "cre").value),
      cim: Number(field(form, "cim").value),
      scale: Number(field(form, "scale").value),
    };
  }

  function imagePoint(clientX: number, clientY: number, view: View): Point {
    const rect = viewport.getBoundingClientRect();
    return {
      x: ((clientX - rect.left) * view.width) / rect.width,
      y: ((clientY - rect.top) * view.height) / rect.height,
    };
  }

  function complexPoint(point: Point, view: View) {
    // The TIFF orientation maps image y to real and image x to imaginary.
    return {
      re: view.cre + (point.y - view.height / 2) / view.scale,
      im: view.cim + (point.x - view.width / 2) / view.scale,
    };
  }

  function setView(cre: number, cim: number, scale: number) {
    if (![cre, cim, scale].every(Number.isFinite) || scale <= 0) return false;
    field(form, "cre").value = String(Number(cre.toPrecision(15)));
    field(form, "cim").value = String(Number(cim.toPrecision(15)));
    field(form, "scale").value = String(Number(scale.toPrecision(15)));
    return true;
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
      const start = imagePoint(gesture.x, gesture.y, view);
      const now = imagePoint(point.x, point.y, view);
      if (
        setView(
          view.cre - (now.y - start.y) / view.scale,
          view.cim - (now.x - start.x) / view.scale,
          view.scale
        )
      ) {
        gesture.moved = true;
        viewChanged();
      }
    } else if (pointers.size === 2 && gesture.distance) {
      const mid = pointerMidpoint();
      const scale = (view.scale * mid.distance) / gesture.distance;
      const anchor = complexPoint(imagePoint(gesture.x, gesture.y, view), view);
      const now = imagePoint(mid.x, mid.y, view);
      if (
        setView(
          anchor.re - (now.y - view.height / 2) / scale,
          anchor.im - (now.x - view.width / 2) / scale,
          scale
        )
      ) {
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
      const point = imagePoint(event.clientX, event.clientY, view);
      const anchor = complexPoint(point, view);
      const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? 800 : 1);
      const scale = view.scale * Math.exp(Math.max(-2, Math.min(2, -delta * 0.001)));
      if (
        setView(
          anchor.re - (point.y - view.height / 2) / scale,
          anchor.im - (point.x - view.width / 2) / scale,
          scale
        )
      ) {
        viewChanged();
      }
    },
    { passive: false }
  );

  let lastViewport = viewportPixelsFor(viewport);
  new ResizeObserver(() => {
    const next = viewportPixelsFor(viewport);
    if (next.width === lastViewport.width && next.height === lastViewport.height) return;
    const previous = lastViewport;
    lastViewport = next;
    if (!validSettings(false)) return;
    const scale =
      (Number(field(form, "scale").value) * Math.min(next.width, next.height)) /
      Math.min(previous.width, previous.height);
    field(form, "scale").value = String(Number(scale.toPrecision(15)));
    viewChanged();
  }).observe(viewport);
}
