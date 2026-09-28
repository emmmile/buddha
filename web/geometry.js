// Canvas size and the clockwise-rotated complex-plane view.
export function viewportPixelsFor(viewport) {
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

export function attachGeometry(
  {
    form,
    viewport,
    validSettings,
    scheduleRender,
    cancelRender,
    getActiveRenderId,
  },
) {
  const pointers = new Map();
  let gesture;
  function windowViewportPixels() {
    return viewportPixelsFor(viewport);
  }
  function viewState() {
    const { width, height } = windowViewportPixels();
    return {
      width,
      height,
      cre: Number(form.elements.cre.value),
      cim: Number(form.elements.cim.value),
      scale: Number(form.elements.scale.value),
    };
  }

  function imagePoint(clientX, clientY, view) {
    const rect = viewport.getBoundingClientRect();
    return {
      x: (clientX - rect.left) * view.width / rect.width,
      y: (clientY - rect.top) * view.height / rect.height,
    };
  }

  function complexPoint(point, view) {
    // The TIFF orientation maps image y to real and image x to imaginary.
    return {
      re: view.cre + (point.y - view.height / 2) / view.scale,
      im: view.cim + (point.x - view.width / 2) / view.scale,
    };
  }

  function setView(cre, cim, scale) {
    if (![cre, cim, scale].every(Number.isFinite) || scale <= 0) return false;
    form.elements.cre.value = Number(cre.toPrecision(15));
    form.elements.cim.value = Number(cim.toPrecision(15));
    form.elements.scale.value = Number(scale.toPrecision(15));
    return true;
  }

  function pointerMidpoint() {
    const points = [...pointers.values()];
    return {
      x: (points[0].x + points[1].x) / 2,
      y: (points[0].y + points[1].y) / 2,
      distance: Math.hypot(
        points[0].x - points[1].x,
        points[0].y - points[1].y,
      ),
    };
  }

  function beginGesture() {
    if (!validSettings(false)) return;
    const view = viewState();
    if (pointers.size === 1) {
      const pointer = [...pointers.values()][0];
      gesture = { view, x: pointer.x, y: pointer.y, moved: false };
    } else if (pointers.size === 2) {
      const mid = pointerMidpoint();
      gesture = {
        view,
        x: mid.x,
        y: mid.y,
        distance: mid.distance,
        moved: false,
      };
    }
  }

  viewport.addEventListener("pointerdown", (event) => {
    if (event.pointerType === "mouse" && event.button !== 0) return;
    if (pointers.size >= 2) return;
    cancelRender();
    viewport.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    viewport.classList.add("dragging");
    beginGesture();
  });

  viewport.addEventListener("pointermove", (event) => {
    if (!pointers.has(event.pointerId) || !gesture) return;
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (pointers.size === 1 && gesture.distance === undefined) {
      const point = [...pointers.values()][0];
      if (
        Math.hypot(point.x - gesture.x, point.y - gesture.y) <= 3 &&
        !gesture.moved
      ) return;
      const start = imagePoint(gesture.x, gesture.y, gesture.view);
      const now = imagePoint(point.x, point.y, gesture.view);
      if (
        setView(
          gesture.view.cre - (now.y - start.y) / gesture.view.scale,
          gesture.view.cim - (now.x - start.x) / gesture.view.scale,
          gesture.view.scale,
        )
      ) {
        gesture.moved = true;
      }
    } else if (pointers.size === 2 && gesture.distance > 0) {
      const mid = pointerMidpoint();
      const scale = gesture.view.scale * mid.distance / gesture.distance;
      const anchor = complexPoint(
        imagePoint(gesture.x, gesture.y, gesture.view),
        gesture.view,
      );
      const now = imagePoint(mid.x, mid.y, gesture.view);
      if (
        setView(
          anchor.re - (now.y - gesture.view.height / 2) / scale,
          anchor.im - (now.x - gesture.view.width / 2) / scale,
          scale,
        )
      ) {
        gesture.moved = true;
      }
    }
  });

  function endPointer(event) {
    if (!pointers.delete(event.pointerId)) return;
    if (gesture?.moved) scheduleRender();
    gesture = undefined;
    if (pointers.size) beginGesture();
    else viewport.classList.remove("dragging");
  }
  viewport.addEventListener("pointerup", endPointer);
  viewport.addEventListener("pointercancel", endPointer);

  viewport.addEventListener("wheel", (event) => {
    event.preventDefault();
    if (!validSettings(false)) return;
    const view = viewState();
    const point = imagePoint(event.clientX, event.clientY, view);
    const anchor = complexPoint(point, view);
    const delta = event.deltaY *
      (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? 800 : 1);
    const scale = view.scale *
      Math.exp(Math.max(-2, Math.min(2, -delta * 0.001)));
    if (
      setView(
        anchor.re - (point.y - view.height / 2) / scale,
        anchor.im - (point.x - view.width / 2) / scale,
        scale,
      )
    ) {
      scheduleRender();
    }
  }, { passive: false });

  let lastViewport = windowViewportPixels();
  new ResizeObserver(() => {
    const next = windowViewportPixels();
    if (
      next.width === lastViewport.width && next.height === lastViewport.height
    ) return;
    const previous = lastViewport;
    lastViewport = next;
    if (!validSettings(false)) return;
    form.elements.scale.value = Number((Number(form.elements.scale.value) *
      Math.min(next.width, next.height) /
      Math.min(previous.width, previous.height)).toPrecision(15));
    if (getActiveRenderId()) scheduleRender(250);
  }).observe(viewport);
}
