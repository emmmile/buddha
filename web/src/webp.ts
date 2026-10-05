// WebP export with metadata. libwebp, compiled to WebAssembly, encodes the pixels: the browsers'
// own encoders take no effort setting (Chrome's is at the lowest), and Safari's encodes no WebP.
// The file is then rewritten in the extended format (a VP8X chunk first) with an XMP chunk at the
// end, as the WebP container specification lays out. Tools such as exiftool and ImageMagick's
// identify -verbose list it.

import encode from "@jsquash/webp/encode";

// Lossless with every pixel kept, as cwebp -z 3 -exact (method 3, quality 30). On exports from
// the explorer it is 11-23% smaller than Chrome's WebP, and higher levels gain at most 3% more
// at up to ten times the time.
const OPTIONS = { lossless: 1, method: 3, quality: 30, exact: 1, near_lossless: 100 };
const XMP_FLAG = 0x04;
const ALPHA_FLAG = 0x10;
const NAMESPACE = "https://github.com/emmmile/buddha/ns/explorer/1.0/";

const encoder = new TextEncoder();
const decoder = new TextDecoder();

function escapeXml(text: string) {
  return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// The PNG keywords map to their XMP equivalents; other keys, which must be XML names, go in the
// explorer's own namespace.
function xmp(text: Record<string, string>) {
  const properties = Object.entries(text).map(([key, value]) => {
    const v = escapeXml(value);
    switch (key) {
      case "Software":
        return `<xmp:CreatorTool>${v}</xmp:CreatorTool>`;
      case "Creation Time":
        return `<xmp:CreateDate>${v}</xmp:CreateDate>`;
      case "Description":
        return `<dc:description><rdf:Alt><rdf:li xml:lang="x-default">${v}</rdf:li></rdf:Alt></dc:description>`;
      default:
        return `<buddha:${key}>${v}</buddha:${key}>`;
    }
  });
  return (
    `<?xpacket begin="\uFEFF" id="W5M0MpCehiHzreSzNTczkc9d"?>` +
    `<x:xmpmeta xmlns:x="adobe:ns:meta/">` +
    `<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">` +
    `<rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/" ` +
    `xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:buddha="${NAMESPACE}">` +
    properties.join("") +
    `</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end="r"?>`
  );
}

// One RIFF chunk: FourCC, little-endian size, data, padded to an even length.
function chunk(fourcc: string, data: Uint8Array) {
  const bytes = new Uint8Array(8 + data.length + (data.length & 1));
  bytes.set(encoder.encode(fourcc), 0);
  new DataView(bytes.buffer).setUint32(4, data.length, true);
  bytes.set(data, 8);
  return bytes;
}

function vp8x(flags: number, width: number, height: number) {
  const data = new Uint8Array(10);
  data[0] = flags;
  data.set([(width - 1) & 0xff, ((width - 1) >> 8) & 0xff, (width - 1) >> 16], 4);
  data.set([(height - 1) & 0xff, ((height - 1) >> 8) & 0xff, (height - 1) >> 16], 7);
  return chunk("VP8X", data);
}

// Whether the WebP decodes to exactly the pixels of the image, checked with the browser's own
// decoder in case the encoder were ever set up to change any.
async function decodesTo(webp: Blob, image: ImageData) {
  const canvas = new OffscreenCanvas(image.width, image.height);
  const bitmap = await createImageBitmap(webp, {
    colorSpaceConversion: "none",
    premultiplyAlpha: "none",
  });
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (!context) throw new Error("Cannot create a 2D canvas to check the image");
  context.drawImage(bitmap, 0, 0);
  bitmap.close();
  const decoded = new Uint32Array(
    context.getImageData(0, 0, image.width, image.height).data.buffer
  );
  const expected = new Uint32Array(image.data.buffer, image.data.byteOffset, decoded.length);
  return decoded.every((pixel, i) => pixel === expected[i]);
}

// Encodes an image as lossless WebP with the entries of text in an XMP chunk, or returns
// undefined if the result does not decode to the same pixels.
export async function encodeWebp(
  image: ImageData,
  text: Record<string, string>
): Promise<Blob | undefined> {
  const webp = new Uint8Array(await encode(image, OPTIONS));
  if (
    decoder.decode(webp.subarray(0, 4)) !== "RIFF" ||
    decoder.decode(webp.subarray(8, 12)) !== "WEBP"
  ) {
    throw new Error("Unexpected WebP layout from the encoder");
  }

  // The chunks after the header, with VP8X's flags taken out to be rewritten.
  const view = new DataView(webp.buffer, webp.byteOffset, webp.byteLength);
  let flags = 0;
  const chunks: Uint8Array<ArrayBuffer>[] = [];
  for (let at = 12; at + 8 <= webp.length;) {
    const fourcc = decoder.decode(webp.subarray(at, at + 4));
    const size = view.getUint32(at + 4, true);
    const end = at + 8 + size + (size & 1);
    if (fourcc === "VP8X") flags = webp[at + 8];
    // VP8L keeps its alpha_is_used bit in the bitstream header, after the signature byte.
    else if (fourcc === "VP8L" && view.getUint32(at + 9, true) & (1 << 28)) flags |= ALPHA_FLAG;
    else if (fourcc === "ALPH") flags |= ALPHA_FLAG;
    if (fourcc !== "VP8X" && fourcc !== "XMP ") chunks.push(webp.subarray(at, end));
    at = end;
  }
  if (!(await decodesTo(new Blob([webp]), image))) return undefined;
  chunks.unshift(vp8x(flags | XMP_FLAG, image.width, image.height));
  chunks.push(chunk("XMP ", encoder.encode(xmp(text))));

  const header = encoder.encode("RIFF....WEBP");
  const size = 4 + chunks.reduce((sum, c) => sum + c.length, 0);
  new DataView(header.buffer).setUint32(4, size, true);
  return new Blob([header, ...chunks], { type: "image/webp" });
}
