// PNG export with text metadata. The browser encodes the pixels; iTXt chunks (UTF-8 text, PNG
// specification section 11.3.4.5) are then inserted after IHDR. Tools such as exiftool and
// ImageMagick's identify -verbose list them.

const SIGNATURE_AND_IHDR = 8 + 4 + 4 + 13 + 4; // signature, then IHDR's length, type, data, CRC

const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

function crc32(bytes: Uint8Array) {
  let c = 0xffffffff;
  for (const byte of bytes) c = crcTable[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

// One uncompressed iTXt chunk: keyword, then text in UTF-8 with no language tag.
function itxt(keyword: string, text: string) {
  const encoder = new TextEncoder();
  const data = new Uint8Array([
    ...encoder.encode(keyword),
    0, // keyword terminator
    0, // not compressed
    0, // compression method
    0, // empty language tag
    0, // empty translated keyword
    ...encoder.encode(text),
  ]);
  const chunk = new Uint8Array(12 + data.length);
  const view = new DataView(chunk.buffer);
  view.setUint32(0, data.length);
  chunk.set(encoder.encode("iTXt"), 4);
  chunk.set(data, 8);
  view.setUint32(8 + data.length, crc32(chunk.subarray(4, 8 + data.length)));
  return chunk;
}

// Encodes an image as PNG with one iTXt chunk per entry of text, keyed by its PNG keyword
// (1-79 Latin-1 characters; "Software", "Description" and "Creation Time" are registered ones).
export async function encodePng(image: ImageData, text: Record<string, string>): Promise<Blob> {
  const canvas = new OffscreenCanvas(image.width, image.height);
  const context = canvas.getContext("2d");
  if (!context) throw new Error("Cannot create a 2D canvas to encode the image");
  context.putImageData(image, 0, 0);
  const png = new Uint8Array(
    await (await canvas.convertToBlob({ type: "image/png" })).arrayBuffer()
  );
  if (new TextDecoder().decode(png.subarray(12, 16)) !== "IHDR") {
    throw new Error("Unexpected PNG layout from the browser");
  }
  const chunks = Object.entries(text).map(([keyword, value]) => itxt(keyword, value));
  return new Blob(
    [png.subarray(0, SIGNATURE_AND_IHDR), ...chunks, png.subarray(SIGNATURE_AND_IHDR)],
    {
      type: "image/png",
    }
  );
}
