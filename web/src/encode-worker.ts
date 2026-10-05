// Encodes exported images off the main thread, so the page keeps drawing meanwhile: lossless WebP,
// or PNG if the WebP encoder fails to load or run.

import { encodePng } from "./png";
import { encodeWebp } from "./webp";

export interface EncodeRequest {
  image: ImageData;
  text: Record<string, string>;
}

self.onmessage = async ({ data: { image, text } }: MessageEvent<EncodeRequest>) => {
  const webp = await encodeWebp(image, text).catch((error: unknown) => {
    console.warn("WebP export failed, saving PNG instead:", error);
    return undefined;
  });
  self.postMessage(webp ?? (await encodePng(image, text)));
};
