import * as THREE from 'three';
import type { Room } from '../types';

const CELL = .1;
type Point = THREE.Vector2;

/** The floor of the rooms as a grid for a round robot of `radius`: where it fits, and the height it drives at there.
 * Furniture lower than `clearance` above the floor blocks it; a rug or a mat it drives over. */
export function floorGrid(rooms: Room[], world: THREE.Object3D, skip: (mesh: THREE.Object3D) => boolean, radius: number, floor: number, clearance: number) {
  const x0 = Math.min(...rooms.map(r => r.x - r.w / 2)), z0 = Math.min(...rooms.map(r => r.z - r.d / 2));
  const nx = Math.ceil((Math.max(...rooms.map(r => r.x + r.w / 2)) - x0) / CELL), nz = Math.ceil((Math.max(...rooms.map(r => r.z + r.d / 2)) - z0) / CELL);
  const free = new Uint8Array(nx * nz), height = new Float32Array(nx * nz).fill(floor);
  const inside = (x: number, z: number) => rooms.some(r => Math.abs(x - r.x) <= r.w / 2 && Math.abs(z - r.z) <= r.d / 2);
  for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
    const x = x0 + (i + .5) * CELL, z = z0 + (j + .5) * CELL;
    free[j * nx + i] = Number(inside(x - radius, z - radius) && inside(x + radius, z - radius) && inside(x - radius, z + radius) && inside(x + radius, z + radius));
  }
  world.updateMatrixWorld(true);
  const whole = new THREE.Box3();
  world.traverse(mesh => {
    if (!(mesh instanceof THREE.Mesh) || skip(mesh)) return;
    const bounds: THREE.Box3[] = mesh.userData.collisionBounds ?? [whole.setFromObject(mesh)];
    for (const box of bounds) {
      if (box.max.y <= floor + .015 || box.min.y >= floor + clearance) continue;
      const flat = box.max.y - box.min.y <= .045, grow = flat ? 0 : radius;
      for (let j = Math.max(0, Math.floor((box.min.z - grow - z0) / CELL)); j < Math.min(nz, Math.ceil((box.max.z + grow - z0) / CELL)); j++)
        for (let i = Math.max(0, Math.floor((box.min.x - grow - x0) / CELL)); i < Math.min(nx, Math.ceil((box.max.x + grow - x0) / CELL)); i++)
          if (flat) height[j * nx + i] = Math.max(height[j * nx + i], box.max.y); else free[j * nx + i] = 0;
    }
  });
  return { x0, z0, nx, nz, free, height };
}
export type FloorGrid = ReturnType<typeof floorGrid>;

const cellOf = (g: FloorGrid, p: Point) => {
  const i = Math.floor((p.x - g.x0) / CELL), j = Math.floor((p.y - g.z0) / CELL);
  return i >= 0 && j >= 0 && i < g.nx && j < g.nz ? j * g.nx + i : -1;
};
const centre = (g: FloorGrid, k: number) => new THREE.Vector2(g.x0 + (k % g.nx + .5) * CELL, g.z0 + (Math.floor(k / g.nx) + .5) * CELL);
export const floorHeight = (g: FloorGrid, p: Point) => { const k = cellOf(g, p); return k < 0 ? g.height[0] : g.height[k]; };

/** Whether the robot drives straight from `a` to `b` over free cells only. */
function clear(g: FloorGrid, a: Point, b: Point) {
  const steps = Math.ceil(a.distanceTo(b) / (CELL / 2)), p = new THREE.Vector2();
  for (let s = 0; s <= steps; s++) { const k = cellOf(g, p.lerpVectors(a, b, s / Math.max(1, steps))); if (k < 0 || !g.free[k]) return false; }
  return true;
}

/** The cells reachable from `from`, by their distance in steps (8 neighbours, no corner cutting), and where each came from. */
function flood(g: FloorGrid, from: number) {
  const parent = new Int32Array(g.free.length).fill(-1), queue = [from];
  parent[from] = from;
  for (let head = 0; head < queue.length; head++) {
    const k = queue[head], i = k % g.nx;
    for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]]) {
      const n = k + dj * g.nx + di;
      if (i + di < 0 || i + di >= g.nx || n < 0 || n >= g.free.length || !g.free[n] || parent[n] >= 0) continue;
      if (di && dj && (!g.free[k + di] || !g.free[k + dj * g.nx])) continue;
      parent[n] = k; queue.push(n);
    }
  }
  return parent;
}

/** The cell nearest to `p` of those `ok` takes; -1 when there is none. */
function nearest(g: FloorGrid, p: Point, ok: (cell: number) => boolean) {
  let best = -1, distance = Infinity;
  for (let k = 0; k < g.free.length; k++) {
    if (!ok(k)) continue;
    const d = Math.hypot(g.x0 + (k % g.nx + .5) * CELL - p.x, g.z0 + (Math.floor(k / g.nx) + .5) * CELL - p.y);
    if (d < distance) { distance = d; best = k; }
  }
  return best;
}

/** A short way from `a` to `b` around what blocks it, as the points where it turns; empty when the robot cannot move.
 * An end too close to something, as a dock by the furniture, is left or reached from the nearest free cell. */
export function way(g: FloorGrid, a: Point, b: Point): Point[] {
  if (clear(g, a, b)) return [b.clone()];
  const from = nearest(g, a, k => g.free[k] === 1);
  if (from < 0) return [];
  const parent = flood(g, from), to = nearest(g, b, k => parent[k] >= 0);
  const cells: Point[] = [];
  for (let k = to; k !== from; k = parent[k]) cells.unshift(centre(g, k));
  if (cellOf(g, a) !== from) cells.unshift(centre(g, from));
  if (cellOf(g, b) === to && to !== from) cells[cells.length - 1] = b.clone(); else cells.push(b.clone());
  // Only the turns are kept: from each kept point, the farthest one it sees straight.
  const turns: Point[] = [];
  for (let at = a, n = 0; n < cells.length;) {
    let far = n;
    while (far + 1 < cells.length && clear(g, at, cells[far + 1])) far++;
    turns.push(cells[far]); at = cells[far]; n = far + 1;
  }
  return turns;
}

/** How a robot cleans the house from its dock: room after room, the nearest first, in lanes across each room. */
export function sweep(g: FloorGrid, rooms: Room[], dock: Point, lane: number): Point[] {
  const start = nearest(g, dock, k => g.free[k] === 1), reach = start < 0 ? null : flood(g, start);
  const stops: Point[] = [];
  let at = dock, left = [...rooms];
  while (left.length) {
    const room = left.reduce((best, r) => at.distanceTo(new THREE.Vector2(r.x, r.z)) < at.distanceTo(new THREE.Vector2(best.x, best.z)) ? r : best);
    left = left.filter(r => r !== room);
    const i0 = Math.max(0, Math.ceil((room.x - room.w / 2 - g.x0) / CELL)), i1 = Math.min(g.nx, Math.floor((room.x + room.w / 2 - g.x0) / CELL));
    let forward = true;
    for (let z = room.z - room.d / 2 + lane / 2; z < room.z + room.d / 2; z += lane, forward = !forward) {
      const j = Math.floor((z - g.z0) / CELL), runs: [number, number][] = [];
      for (let i = i0, run = -1; i <= i1; i++) {
        const ok = i < i1 && j >= 0 && j < g.nz && reach !== null && reach[j * g.nx + i] >= 0;
        if (ok && run < 0) run = i;
        if (!ok && run >= 0) { if (i - run >= 3) runs.push([run, i - 1]); run = -1; }
      }
      for (const [a, b] of forward ? runs : runs.reverse().map(([a, b]) => [b, a] as [number, number]))
        stops.push(centre(g, j * g.nx + a), centre(g, j * g.nx + b));
    }
    if (stops.length) at = stops[stops.length - 1];
  }
  return stops;
}
