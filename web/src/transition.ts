// The old preview is only a visual placeholder: its pixels never enter the new histogram.
import type { Size } from "./geometry";

export interface PreviewView extends Size {
  cre: number;
  cim: number;
  span: number;
}

export interface PreviewTransform {
  scale: [number, number];
  offset: [number, number];
  opacity: number;
}

// Map a pixel's normalized coordinates in the new view back into the old image. The explorer
// is rotated: imaginary coordinates run across the image, real coordinates run down it.
export function previewTransform(
  previous: PreviewView,
  current: PreviewView,
  opacity: number
): PreviewTransform {
  const oldUnit = previous.span / Math.min(previous.width, previous.height);
  const newUnit = current.span / Math.min(current.width, current.height);
  const scaleX = (newUnit * current.width) / (oldUnit * previous.width);
  const scaleY = (newUnit * current.height) / (oldUnit * previous.height);
  return {
    scale: [scaleX, scaleY],
    offset: [
      0.5 - scaleX / 2 + (current.cim - previous.cim) / (oldUnit * previous.width),
      0.5 - scaleY / 2 + (current.cre - previous.cre) / (oldUnit * previous.height),
    ],
    opacity,
  };
}

const HOLD_POINTS_PER_PIXEL = 1;
const FADE_POINTS_PER_PIXEL = 8;

// Hold the old frame while the new render finds points, then smoothly replace it between one
// and eight histogram increments per pixel. Density makes this independent of display size;
// stalled or paused renders retain their placeholder instead of fading to black on a timer.
export function previousOpacity(points: number, pixels: number) {
  const progress = Math.max(
    0,
    Math.min(
      1,
      (points / pixels - HOLD_POINTS_PER_PIXEL) / (FADE_POINTS_PER_PIXEL - HOLD_POINTS_PER_PIXEL)
    )
  );
  return 1 - progress * progress * (3 - 2 * progress);
}
