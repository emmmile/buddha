// Loads data/exclusion.map, the map buddha++ and buddha-metal use, onto the GPU. The file is
// zstd-compressed and little-endian (see core/mandelbrot.h):
//   "BUDDHAEX"         magic
//   uint32 version     1
//   uint32 iterations  iteration limit the map was computed with
//   uint64 size        resolution: the map has size x size/2 cells
//   cells              one bit per cell, cell i in bit i % 8 of byte i / 8
// Read as little-endian 32-bit words, cell i is bit i % 32 of word i / 32. The map covers
// [-2, 2] x [-2, 0] in square cells; the other half of the plane is its mirror image.
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
  const bits = bytes.slice(HEADER, HEADER + length);
  device.queue.writeBuffer(cells, 0, bits);
  const list = boundaryCells(bits, size);
  const boundary = device.createBuffer({
    size: Math.max(4, list.byteLength),
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(boundary, 0, list);
  return { size, iterations, cells, boundary, boundaryCount: list.length };
}

// Cells that are not excluded but have an excluded neighbour: they lie along the boundary of the
// set, where Newton seeding starts. Row size / 2 - 1 touches the real axis, whose mirror image is
// itself.
function boundaryCells(bits: Uint8Array, size: number) {
  const rows = size / 2;
  const excluded = (x: number, y: number) => {
    if (x < 0 || x >= size || y < 0) return false;
    const cell = Math.min(y, rows - 1) * size + x;
    return (bits[cell >> 3] & (1 << (cell & 7))) !== 0;
  };
  const list: number[] = [];
  for (let y = 0; y < rows; ++y) {
    for (let x = 0; x < size; ++x) {
      if (excluded(x, y)) continue;
      if (excluded(x - 1, y) || excluded(x + 1, y) || excluded(x, y - 1) || excluded(x, y + 1)) {
        list.push(y * size + x);
      }
    }
  }
  return new Uint32Array(list);
}
