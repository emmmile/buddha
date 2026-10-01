// Loads data/exclusion.map, the map buddha++ and buddha-metal use, onto the GPU. The file is
// zstd-compressed and little-endian (see core/mandelbrot.h):
//   "BUDDHAEX"         magic
//   uint32 version     1
//   uint32 iterations  iteration limit the map was computed with
//   uint64 size        resolution: the map has size x size/2 cells
//   cells              one bit per cell, cell i in bit i % 8 of byte i / 8
// Read as little-endian 32-bit words, cell i is bit i % 32 of word i / 32.
import { decompress } from "fzstd";
import type { ExclusionMap } from "./sampler";

const HEADER = 24;

// Not a "?url" import: the dev server takes .map files for source maps and serves them raw.
const mapUrl = new URL("../../data/exclusion.map", import.meta.url);

export async function loadExclusionMap(
  device: GPUDevice
): Promise<ExclusionMap & { iterations: number }> {
  const response = await fetch(mapUrl);
  if (!response.ok) throw new Error(`Cannot load the exclusion map (${response.status})`);
  const bytes = decompress(new Uint8Array(await response.arrayBuffer()));
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const magic = new TextDecoder().decode(bytes.subarray(0, 8));
  if (magic !== "BUDDHAEX" || view.getUint32(8, true) !== 1) {
    throw new Error("Invalid exclusion map");
  }
  const iterations = view.getUint32(12, true);
  const size = Number(view.getBigUint64(16, true));
  const length = (size * size) / 2 / 8;
  if (size < 2 || size % 8 !== 0 || bytes.byteLength < HEADER + length) {
    throw new Error("Invalid exclusion map size");
  }
  const cells = device.createBuffer({
    size: length,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(cells, 0, bytes.slice(HEADER, HEADER + length));
  return { size, iterations, cells };
}
