import * as THREE from 'three';
import type { Device, House, Room } from '../types';
import { lampState, opening, value } from './devices';
import { furnish } from './furniture';
import { lampLit, makeLamps } from './lamps';
import { mergeStatic } from './static';
import { buildSpeaker, SPEAKER } from './speaker';
import { buildVacuum } from './vacuum';

/** `update` moves a device towards its values; it returns true while the device moves by itself and needs frames.
 * What moves by itself casts no shadow: the shadows are drawn again only when values change. */
type Binding = { id: string; update: (device: Device, dt: number, seconds: number) => boolean | void };
type Part = THREE.Mesh<THREE.BufferGeometry, THREE.MeshLambertMaterial>;
/** Where the front of a device looks; every model below is built looking along +x. */
const TURN = { '+x': 0, '+z': -Math.PI / 2, '-x': Math.PI, '-z': Math.PI / 2 };
const WALL = '#bdcdcf';
/** The light of the lamps is kept per vertex: faces are cut into squares about this size, in metres. */
const CELL = .2;
const cells = (size: number) => Math.max(1, Math.ceil(size / CELL));

/** Primitives that add a mesh to the scene and remember what to dispose of. */
export function makeKit(world: THREE.Object3D) {
  const geometries: THREE.BufferGeometry[] = [], materials: THREE.Material[] = [], textures: THREE.Texture[] = [];
  const mesh = (geometry: THREE.BufferGeometry, color: string, x: number, y: number, z: number, parent: THREE.Object3D): Part => {
    const material = lampLit(new THREE.MeshLambertMaterial({ color }));
    geometries.push(geometry); materials.push(material);
    const part = new THREE.Mesh(geometry, material);
    part.position.set(x, y, z); part.castShadow = true; part.receiveShadow = true; parent.add(part);
    return part;
  };
  return {
    geometries, materials, textures, lamps: makeLamps(),
    box: (x: number, y: number, z: number, w: number, h: number, d: number, color: string, parent = world) => mesh(new THREE.BoxGeometry(w, h, d, cells(w), cells(h), cells(d)), color, x, y, z, parent),
    cylinder: (x: number, y: number, z: number, radius: number, height: number, color: string, parent = world) => mesh(new THREE.CylinderGeometry(radius, radius, height, 24, cells(height)), color, x, y, z, parent),
    ball: (x: number, y: number, z: number, radius: number, color: string, parent = world) => mesh(new THREE.SphereGeometry(radius, 20, 14), color, x, y, z, parent),
  };
}
export type Kit = ReturnType<typeof makeKit>;

const on = (device: Device) => value(device, 'on_off') === true;
function glow(part: Part, color: string | false, intensity = 1) {
  part.material.emissive.set(color || '#000000'); part.material.emissiveIntensity = intensity;
}

/** Builds the model of a device inside `g`, a group on the floor under the device, and returns its per-frame update. */
function buildDevice(kit: Kit, d: Device, g: THREE.Group, rooms: Room[]): Binding['update'] {
  const y = d.pos[1], kind = d.kind;
  const b = (x: number, h: number, z: number, w: number, height: number, depth: number, color: string) => kit.box(x, h, z, w, height, depth, color, g);
  const c = (x: number, h: number, z: number, radius: number, height: number, color: string) => kit.cylinder(x, h, z, radius, height, color, g);
  const lerp = THREE.MathUtils.lerp;
  if (kind === 'light' || kind === 'led_strip') {
    const strip = kind === 'led_strip', ceiling = d.mount === 'ceiling';
    const head = strip ? b(0, y, 0, .06, .06, d.length ?? 2.2, '#fff4d5') : b(0, y, 0, .4, .22, .24, '#fff4d5');
    if (ceiling) {
      // The ceiling is cut away: a pendant hangs on its wire from a small canopy.
      b(0, (y + .11 + 2.9) / 2, 0, .02, 2.9 - y - .11, .02, '#344755'); c(0, 2.9, 0, .12, .03, '#344755');
    } else if (!strip) {
      const base = d.base ?? .085;
      b(0, (y + base) / 2, 0, .045, y - base, .045, '#344755'); c(0, base, 0, .18, .05, '#344755');
    }
    const lamp = kit.lamps.add(g, new THREE.Vector3(strip ? .15 : 0, y + (ceiling ? -.2 : .15), 0), 5, '#ffcc99');
    const dark = new THREE.Color('#627078'), black = new THREE.Color('#000000'), color = new THREE.Color();
    return (device, dt) => {
      const state = lampState(device); color.set(state.color);
      head.material.color.lerp(state.on ? color : dark, dt);
      head.material.emissive.lerp(state.on ? color : black, dt);
      head.material.emissiveIntensity = state.on ? .7 + state.brightness : 0;
      lamp.color.lerp(color, dt); lamp.intensity = lerp(lamp.intensity, state.on ? state.brightness * 14 : 0, dt);
      kit.lamps.shine(lamp);
    };
  }
  if (kind === 'gate') {
    // The panel runs on a rail outside the posts; `slide` is the direction it opens to.
    const slide = d.slide ?? -1;
    for (const dz of [-1.2, 1.2]) b(0, 1, dz, .16, 2, .16, '#afb9b5');
    b(.18, .035, slide * 1.3, .14, .07, 5.2, '#596e76'); b(.18, .08, slide * 1.3, .13, .04, 5.18, '#8a9fa6');
    const panel = new THREE.Group(); panel.position.set(.18, .05, 0); g.add(panel);
    for (const dz of [-.9, .9]) kit.cylinder(0, .13, dz, .08, .1, '#334a54', panel).rotation.z = Math.PI / 2;
    for (let dz = -1.1; dz <= 1.1; dz += .22) kit.box(0, .9, dz, .09, 1.7, .08, '#56696d', panel);
    for (const h of [.15, 1.6]) kit.box(0, h, 0, .12, .12, 2.3, '#687e81', panel);
    return (device, dt) => { panel.position.z = lerp(panel.position.z, slide * 2.5 * opening(device), dt); };
  }
  if (kind === 'window_blind') {
    const length = d.length ?? 2.6;
    b(-.14, 1.34, 0, .22, 1.88, length, '#7497a5'); b(.02, 2.25, 0, .24, .1, length + .1, '#ded4b8');
    const slats = Array.from({ length: 11 }, (_, i) => b(.02, .48 + i * .16, 0, .19, .07, length - .1, '#ded4b8'));
    return (device, dt) => slats.forEach((slat, i) => { slat.position.y = lerp(slat.position.y, lerp(.48 + i * .16, 2.04 + i * .018, opening(device)), dt); });
  }
  if (kind === 'curtain') {
    const length = d.length ?? 2.8, half = length / 2;
    b(0, y + .98, 0, .06, .06, length + .08, '#7e929d');
    for (const dz of [-half - .02, half + .02]) b(-.11, y + .98, dz, .28, .06, .06, '#7e929d');
    const panels = [-1, 1].map(side => b(0, y, side * length / 4, .13, 1.9, half, '#bdc3cb'));
    return (device, dt) => panels.forEach((panel, i) => {
      panel.scale.z = lerp(panel.scale.z, 1 - .86 * opening(device), dt);
      panel.position.z = (i ? 1 : -1) * (half - half / 2 * panel.scale.z);
    });
  }
  if (kind === 'sensor_door') {
    for (const dz of [-.72, .72]) b(0, 1.08, dz, .2, 2.04, .12, '#7b8d91');
    b(0, 2.14, 0, .2, .08, 1.56, '#7b8d91');
    const leaf = new THREE.Group(); leaf.position.set(0, .06, -.65); g.add(leaf);
    kit.box(0, 1, .65, .12, 2, 1.3, '#a67b51', leaf);
    for (const dx of [-.12, .12]) kit.box(dx, 1, 1.12, .18, .08, .08, '#e4c899', leaf);
    return (device, dt) => { leaf.rotation.y = lerp(leaf.rotation.y, value(device, 'doorcontact_state') ? -Math.PI / 2.3 : 0, dt); };
  }
  if (kind === 'hvac_fan') {
    c(0, .1, 0, .3, .08, '#485e69'); b(0, .65, 0, .07, 1.1, .07, '#9bafb6');
    const head = new THREE.Group(); head.position.y = 1.3; g.add(head);
    kit.cylinder(0, 0, 0, .4, .09, '#4c6974', head).rotation.x = Math.PI / 2;
    const rotor = new THREE.Group(); head.add(rotor);
    for (let i = 0; i < 3; i++) {
      const angle = i * Math.PI * 2 / 3;
      const blade = kit.box(Math.sin(angle) * .18, Math.cos(angle) * .18, .07, .14, .4, .04, '#b5c9ca', rotor);
      blade.rotation.z = -angle; blade.castShadow = false;
    }
    kit.cylinder(0, 0, 0, .08, .15, '#dee4df', head).rotation.x = Math.PI / 2;
    return (device, _dt, seconds) => { if (on(device)) rotor.rotation.z -= seconds * 9; return on(device) && seconds > 0; };
  }
  if (kind === 'hvac_underfloor_heating') {
    const [w, depth] = d.size ?? [2.15, 1.45];
    b(0, .07, 0, w, .02, depth, '#b8aa91');
    const coils: Part[] = [], count = Math.floor((w - .1) / .32) + 1, start = -(count - 1) * .16;
    for (let i = 0; i < count; i++) {
      coils.push(b(start + i * .32, y, 0, .06, .025, depth - .1, '#768887'));
      if (i < count - 1) coils.push(b(start + .16 + i * .32, y, (i % 2 ? 1 : -1) * (depth / 2 - .08), .32, .025, .06, '#768887'));
    }
    // The thermostat stands on a post at the corner of the heated area.
    b(w / 2 + .15, .5, -depth / 2 + .1, .04, .9, .04, '#8d9ca3'); b(w / 2 + .15, 1, -depth / 2 + .1, .1, .26, .22, '#dbe2dc');
    return device => coils.forEach(coil => { coil.material.color.set(on(device) ? '#cf895d' : '#768887'); glow(coil, on(device) && '#e87835', .5); });
  }
  if (kind === 'hvac_humidifier') {
    b(0, .45, 0, .55, .8, .55, '#dce6e4');
    for (let i = 0; i < 6; i++) b(0, .85, -.2 + i * .08, .38, .025, .035, '#677f86');
    const level = b(.285, .35, 0, .015, .4, .16, '#62b9ce'), led = b(.29, .7, 0, .02, .045, .2, '#314f5c');
    return device => {
      glow(led, on(device) && '#55d7dc');
      // There is no water sensor: the gauge shows the target humidity.
      level.scale.y = Math.max(.02, Number(value(device, 'hvac_humidity_set') ?? 0) / 100); level.position.y = .15 + .2 * level.scale.y;
    };
  }
  if (kind === 'vacuum_cleaner') return buildVacuum(kit, d, g, rooms);
  if (kind === 'tv') {
    const screen = b(0, y, 0, .12, 1.05, 1.85, '#10202b');
    return device => glow(screen, on(device) && '#387ba9', .8);
  }
  // The rest shows its state with an indicator; what heats also glows warm, so that it reads from afar.
  let indicator: Part, handle: Part | undefined;
  const warm: Part[] = [], cool: Part[] = [];
  if (kind === 'socket') {
    b(0, y, 0, .34, .28, .3, '#d9e2df'); indicator = b(0, y + .055, .16, .15, .045, .07, '#3b5761');
    for (const dx of [-.07, .07]) c(dx, y + .15, 0, .025, .012, '#2a3e48');
  } else if (kind === 'hvac_boiler') {
    warm.push(b(0, y, 0, .5, 1.15, .8, '#d9e2df'));
    for (const dz of [-.25, .25]) c(0, (y - .575) / 2, dz, .04, y - .575, '#91a5a9');
    b(.26, y, 0, .02, .3, .35, '#476973'); indicator = b(.26, y + .4, .25, .02, .045, .07, '#3b5761');
  } else if (kind === 'relay' || kind === 'sensor_smoke' || kind === 'sensor_gas') {
    b(0, y, 0, .16, .28, .3, '#d9e2df'); indicator = b(.09, y + .055, 0, .02, .045, .07, '#3b5761');
    if (kind !== 'relay') for (let i = 0; i < 4; i++) b(.095, y - .07 + i * .025, 0, .015, .012, .2, '#597079');
  } else if (kind === 'hvac_heater' || kind === 'hvac_radiator') {
    // The manifolds join every fin to the supported frame.
    for (const h of [.24, .76]) b(0, h, 0, .15, .09, .96, '#a8bac1');
    for (let i = 0; i < 7; i++) warm.push(b(0, .51, -.39 + i * .13, .28, .68, .085, '#d6dce0'));
    for (const dz of [-.34, .34]) { b(0, .125, dz, .48, .13, .1, '#3a4b55'); b(0, .25, dz, .12, .18, .07, '#8d9ca3'); }
    b(0, .48, .51, .28, .58, .15, '#e3e8e9'); indicator = b(.145, .64, .51, .015, .055, .075, '#34434b');
  } else if (kind === 'sensor_pir') {
    b(0, y, 0, .13, .31, .23, '#e8ece9'); b(.075, y - .03, 0, .03, .17, .18, '#c4d2d1');
    indicator = b(.08, y + .09, 0, .015, .025, .04, '#638f92');
  } else if (kind === 'sensor_temp') {
    b(0, y, 0, .08, .3, .3, '#e6ecec'); b(.047, y, 0, .015, .17, .21, '#516e72');
    indicator = b(.057, y - .065, 0, .01, .02, .1, '#b0d8d5');
  } else if (kind === 'sensor_water_leak') {
    c(0, y, 0, .17, .07, '#e5eaec'); indicator = b(0, y + .04, 0, .055, .015, .025, '#42666f');
  } else if (kind === 'valve') {
    c(0, .365, 0, .055, .6, '#81999f'); b(0, y, 0, .2, .19, .2, '#a3b7b9');
    handle = b(0, y + .12, 0, .43, .05, .07, '#2b7995'); indicator = b(0, y + .155, 0, .08, .02, .045, '#324c58');
  } else if (kind === 'kettle') {
    c(0, y + .0225, 0, .18, .045, '#253b48'); warm.push(c(0, y + .24, 0, .15, .39, '#c8d6d9')); c(0, y + .4475, 0, .145, .025, '#425d66');
    b(.21, y + .25, 0, .065, .28, .08, '#354f5a');
    for (const dy of [-.11, .11]) b(.16, y + .25 + dy, 0, .14, .05, .08, '#354f5a');
    b(-.17, y + .36, 0, .15, .11, .1, '#b6c8ce').rotation.z = -.4;
    indicator = b(0, y + .07, .16, .07, .025, .02, '#34434b');
  } else {
    // A wall-mounted air conditioner with a visible grille and a status LED.
    b(0, y, 0, .25, .4, .95, '#e2e7e8');
    for (let i = 0; i < 4; i++) cool.push(b(.135, y - .1 + i * .04, 0, .015, .015, .78, '#7f959c'));
    indicator = b(.14, y + .1, .32, .015, .035, .065, '#34525d');
  }
  return (device, dt) => {
    const alarm = ['water_leak_state', 'smoke_state', 'gas_leak_state'].some(name => value(device, name) === true);
    const active = on(device) || value(device, 'pir') === true || (kind === 'valve' && opening(device) > 0);
    glow(indicator, alarm ? '#ff3322' : active && '#22c9d7', alarm ? 1.6 : 1.2);
    warm.forEach(part => glow(part, on(device) && '#e8672a', .35));
    cool.forEach(part => glow(part, on(device) && '#58c8ff', 1.2));
    if (handle) handle.rotation.y = lerp(handle.rotation.y, opening(device) ? 0 : Math.PI / 2, dt);
  };
}

export function buildHouse(world: THREE.Scene, house: House) {
  const kit = makeKit(world), { box } = kit, { rooms } = house;
  const minX = Math.min(...rooms.map(r => r.x - r.w / 2)), maxX = Math.max(...rooms.map(r => r.x + r.w / 2));
  const minZ = Math.min(...rooms.map(r => r.z - r.d / 2)), maxZ = Math.max(...rooms.map(r => r.z + r.d / 2));
  const plan = { x: (minX + maxX) / 2, z: (minZ + maxZ) / 2, w: maxX - minX, d: maxZ - minZ };
  function label(text: string, x: number, z: number) {
    const canvas = document.createElement('canvas'); canvas.width = 512; canvas.height = 96;
    const ctx = canvas.getContext('2d')!;
    ctx.font = '500 34px sans-serif'; ctx.textAlign = 'center';
    // A plate under the text keeps it readable on a light floor; it is translucent so as not to hide what is behind.
    const width = Math.min(504, ctx.measureText(text).width + 32);
    ctx.fillStyle = 'rgba(16, 36, 50, .62)'; ctx.beginPath(); ctx.roundRect(256 - width / 2, 22, width, 52, 12); ctx.fill();
    ctx.fillStyle = '#ffffff'; ctx.fillText(text, 256, 60);
    const texture = new THREE.CanvasTexture(canvas); kit.textures.push(texture);
    const material = new THREE.SpriteMaterial({ map: texture, depthTest: false, transparent: true, opacity: .9 }); kit.materials.push(material);
    const sprite = new THREE.Sprite(material); sprite.position.set(x, .2, z); sprite.scale.set(3.2, .6, 1); world.add(sprite);
  }
  box(plan.x, -.28, plan.z, plan.w + .7, .5, plan.d + .7, '#273d4a');
  box(plan.x, -.025, plan.z, plan.w + .5, .09, plan.d + .5, '#273d4a');
  for (const room of rooms) {
    box(room.x, 0, room.z, room.w - .2, .12, room.d - .2, room.floor);
    for (let z = room.z - room.d / 2 + .25; z < room.z + room.d / 2 - .1; z += .32) box(room.x, .065, z, room.w - .2, .008, .012, '#514d48');
    label(room.name, room.x, room.z + room.d / 2 - .45);
  }
  // A cutaway: tall walls only at the back and on the left, so every room is visible from the initial camera.
  box(plan.x, 1.15, minZ - .08, plan.w + .32, 2.3, .16, '#d9e1e2');
  box(minX - .08, 1.15, plan.z, .16, 2.3, plan.d, WALL);
  // Low partitions where two rooms meet, with a doorway at one end of a long one.
  for (const a of rooms) for (const b of rooms) {
    const z0 = Math.max(a.z - a.d / 2, b.z - b.d / 2), z1 = Math.min(a.z + a.d / 2, b.z + b.d / 2);
    const x0 = Math.max(a.x - a.w / 2, b.x - b.w / 2), x1 = Math.min(a.x + a.w / 2, b.x + b.w / 2);
    if (Math.abs(a.x + a.w / 2 - (b.x - b.w / 2)) < .01 && z1 - z0 > .5) {
      const door = z1 - z0 > 1.6 ? 1.1 : 0;
      box(a.x + a.w / 2, .4, (z0 + z1 - door) / 2, .15, .8, z1 - z0 - door, WALL);
    }
    if (Math.abs(a.z + a.d / 2 - (b.z - b.d / 2)) < .01 && x1 - x0 > .5) {
      const door = x1 - x0 > 1.6 ? 1.1 : 0;
      box((x0 + door + x1) / 2, .4, a.z + a.d / 2, x1 - x0 - door, .8, .15, WALL);
    }
  }
  furnish(kit, house.id);
  mergeStatic(world, kit, rooms);
  const bindings: Binding[] = [];
  const deviceObjects = new Map<string, THREE.Object3D>();
  for (const device of house.devices) {
    const object = new THREE.Group();
    object.position.set(device.pos[0], 0, device.pos[2]); object.rotation.y = TURN[device.facing ?? '+x']; world.add(object);
    const update = buildDevice(kit, device, object, rooms);
    object.traverse(child => { child.userData.deviceId = device.device_id; });
    deviceObjects.set(device.device_id, object.userData.focus ?? object);
    bindings.push({ id: device.device_id, update });
  }
  const speaker = house.assistant.pos && buildSpeaker(kit, world, house.assistant.pos);
  if (speaker) deviceObjects.set(SPEAKER, speaker.object);
  kit.lamps.light(world);
  return { bindings, deviceObjects, plan, speaker: speaker ? speaker.update : undefined, followLamps: kit.lamps.follow, geometries: kit.geometries, materials: kit.materials, textures: kit.textures };
}
