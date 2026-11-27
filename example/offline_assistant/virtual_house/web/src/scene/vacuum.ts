import * as THREE from 'three';
import type { Device, Room } from '../types';
import { value } from './devices';
import type { Kit } from './geometry';
import { floorGrid, floorHeight, sweep, way, type FloorGrid } from './route';

// SIZE: the robot and its dock are larger than life, so that they read from the whole house; RADIUS is the robot with
// a margin; FLOOR: the top of the floors buildHouse lays.
const SPEED = .45, TURNING = 2.5, LANE = .35, SIZE = 1.35, RADIUS = .17 * SIZE + .03, HEIGHT = .18, FLOOR = .06;
/** The yaw that turns a model built along +x from `from` towards `to`, both on the floor (x, z). */
const yaw = (from: THREE.Vector2, to: THREE.Vector2) => Math.atan2(from.y - to.y, to.x - from.x);

/** A robot vacuum. Its dock stands at the place of the device, and the robot rests in front of it; switched on, it
 * leaves the dock and cleans the house room by room in lanes, over and over; switched off, it drives back and docks. */
export function buildVacuum(kit: Kit, d: Device, g: THREE.Group, rooms: Room[]) {
  const y = FLOOR;
  const base = new THREE.Group(); base.position.y = y; base.scale.setScalar(SIZE); g.add(base);
  kit.box(.06, .07, 0, .12, .14, .34, '#40545e', base); kit.box(.125, .11, 0, .01, .03, .1, '#3b5761', base);
  const robot = new THREE.Group(); robot.scale.setScalar(SIZE); g.parent!.add(robot);
  // A light body with a dark lid and a bright light on it reads on a dark rug as on a light floor.
  kit.cylinder(0, .045, 0, .17, .07, '#e8eef0', robot); kit.cylinder(0, .086, 0, .13, .012, '#2b3a44', robot);
  kit.cylinder(-.04, .104, 0, .045, .025, '#1d2930', robot); kit.box(.15, .045, 0, .045, .05, .2, '#8fa3ab', robot);
  const led = kit.cylinder(.06, .095, 0, .035, .006, '#314f5c', robot);
  robot.traverse(part => { part.userData.deviceId = d.device_id; part.castShadow = false; });
  // The outline and the caption follow the robot, not the dock.
  g.userData.focus = robot;
  g.updateMatrixWorld(true);
  const dock = g.localToWorld(new THREE.Vector3()), rest = g.localToWorld(new THREE.Vector3(.33 * SIZE, 0, 0));
  const home = new THREE.Vector2(rest.x, rest.z), at = home.clone(), docked = yaw(home, new THREE.Vector2(dock.x, dock.z));
  let angle = docked, cleaning = false, legs: THREE.Vector2[] = [], stops: THREE.Vector2[] = [], next = 0, grid: FloorGrid | undefined;
  robot.position.set(at.x, y, at.y); robot.rotation.y = angle;
  return (device: Device, dt: number, seconds: number) => {
    const on = value(device, 'on_off') === true;
    if (on !== cleaning) {
      cleaning = on;
      // The floor is mapped when the robot first leaves: by then everything stands at its place.
      grid ??= floorGrid(rooms, g.parent!, mesh => mesh.userData.deviceId === d.device_id, RADIUS, y, HEIGHT);
      if (on && !stops.length) stops = sweep(grid, rooms, home, LANE);
      legs = on ? [] : way(grid, at, home);
    }
    if (cleaning && !legs.length && stops.length) { legs = way(grid!, at, stops[next]); next = (next + 1) % stops.length; }
    const goal = legs[0];
    const turn = THREE.MathUtils.euclideanModulo((goal ? yaw(at, goal) : docked) - angle + Math.PI, 2 * Math.PI) - Math.PI;
    angle = THREE.MathUtils.euclideanModulo(angle + THREE.MathUtils.clamp(turn, -TURNING * seconds, TURNING * seconds) + Math.PI, 2 * Math.PI) - Math.PI;
    // It turns on the spot and drives once it almost faces where it goes.
    if (goal && Math.abs(turn) < .3) {
      const left = at.distanceTo(goal), step = SPEED * seconds;
      if (step >= left) { at.copy(goal); legs.shift(); } else at.lerp(goal, step / left);
    }
    robot.position.set(at.x, THREE.MathUtils.lerp(robot.position.y, grid ? floorHeight(grid, at) : y, dt), at.y);
    robot.rotation.y = angle;
    // Cyan while it cleans, a dim green while it charges.
    led.material.emissive.set(on ? '#22e0f0' : '#2fbf6a'); led.material.emissiveIntensity = on ? 2.5 : .6;
    return seconds > 0 && (goal !== undefined || Math.abs(turn) > 1e-4);
  };
}
