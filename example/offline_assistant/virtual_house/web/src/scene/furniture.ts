import type { Kit } from './geometry';

/** The furniture of a house of houses/, by its file name: decoration only, in the coordinates of its rooms.
 * A house without an entry here is just empty. */
export function furnish(kit: Kit, house: string) {
  ({ lake_house: lakeHouse, apartment } as Record<string, (kit: Kit) => void>)[house]?.(kit);
}

function pieces({ box, cylinder, ball }: Kit) {
  return {
    legs(xs: number[], zs: number[], height: number, color: string, size = .09) {
      for (const x of xs) for (const z of zs) box(x, height / 2 + .06, z, size, height, size, color);
    },
    plant(x: number, z: number) {
      cylinder(x, .28, z, .3, .45, '#967b5d');
      for (const dx of [-.2, 0, .2]) ball(x + dx, .75 + Math.abs(dx), z, .3, '#59784e');
    },
    windowPane: (x: number, y: number, z: number, w: number, h: number, d: number) => box(x, y, z, w, h, d, '#65879f'),
    /** Seat, back and armrests; the armrests stick out a little so that no two faces lie in one plane. */
    sofa(x: number, z: number, length: number) {
      box(x, .4, z, 1.1, .6, length, '#c4cdd2'); box(x - .45, .75, z, .25, .8, length - .02, '#b1bcc5');
      for (const side of [-1, 1]) box(x, .65, z + side * (length / 2 - .1), 1.12, .4, .22, '#b1bcc5');
    },
    chair(x: number, z: number, back: number) {
      for (const dx of [-.2, .2]) for (const dz of [-.2, .2]) box(x + dx, .21, z + dz, .06, .3, .06, '#35434a');
      box(x, .42, z, .5, .1, .5, '#47696c'); box(x + back * .22, .72, z, .07, .55, .5, '#47696c');
    },
  };
}

function lakeHouse(kit: Kit) {
  const { box, cylinder, ball } = kit, { legs, plant, windowPane, sofa, chair } = pieces(kit);

  // Kitchen: fridge, counter with a hob and a sink, dining table with two chairs.
  box(-7.6, 1, -3.6, .7, 1.9, .7, '#dfe5e6'); box(-7.24, 1.2, -3.35, .03, .7, .05, '#8c9aa0');
  box(-5.65, .5, -3.58, 3.1, .88, .8, '#d3d8ce'); box(-5.65, .98, -3.58, 3.2, .12, .9, '#e3e0d1');
  for (let x = -6.8; x < -4.2; x += .65) box(x, .5, -3.17, .015, .7, .02, '#87958f');
  box(-5.2, 1.05, -3.58, .85, .025, .55, '#263542'); box(-6.5, 1.05, -3.58, .6, .025, .5, '#889fa6');
  box(-5.4, .75, -1.5, 1.7, .1, .95, '#bc926a'); legs([-6.1, -4.7], [-1.85, -1.15], .66, '#35434a');
  chair(-6.65, -1.5, -1); chair(-4.15, -1.5, 1);

  // Bathroom: bathtub, washing machine, washbasin.
  box(-2.3, .37, -3.5, 2, .62, .85, '#e8eef0'); box(-2.3, .66, -3.5, 1.75, .05, .6, '#9fc3d2');
  box(-.95, .53, -3.4, .7, .9, .7, '#dce3e2'); cylinder(-.95, .5, -3.04, .24, .04, '#34566a').rotation.x = Math.PI / 2;
  box(-3.12, .46, -2.2, .55, .8, .9, '#cfd8d6'); box(-3.12, .89, -2.2, .45, .06, .6, '#f2f5f5');

  // Bedroom: bed with its head at the partition, nightstand, window behind the curtains.
  box(.9, .3, -2.2, 2.6, .45, 2, '#5e5049'); box(.9, .57, -2.2, 2.55, .24, 1.9, '#d8dde0');
  box(1.31, .73, -2.2, 1.77, .12, 1.95, '#487889');
  for (const z of [-2.7, -1.7]) box(-.05, .77, z, .5, .2, .8, '#f2eadc');
  box(-.15, .41, -3.6, .6, .7, .6, '#a58768');
  windowPane(1.5, 1.5, -3.98, 2.3, 1.3, .05);

  // Garage: car, workbench with a toolbox.
  box(6.1, .55, -1.5, 3, .5, 1.5, '#b5483f'); box(5.95, 1, -1.5, 1.6, .42, 1.3, '#2c4658');
  for (const x of [5.1, 7.1]) for (const z of [-2.28, -.72]) cylinder(x, .34, z, .28, .2, '#22282b').rotation.x = Math.PI / 2;
  box(4.9, .85, -3.6, 1.5, .1, .7, '#bd965f'); legs([4.25, 5.55], [-3.85, -3.35], .74, '#394749');
  box(4.6, 1.02, -3.6, .5, .25, .35, '#b87a45');

  // Living room: rug, sofa under the window, coffee table, TV console, plant.
  box(-5.3, .09, 2.2, 3.2, .04, 2.4, '#384f62');
  sofa(-7.05, 2.3, 2.4);
  box(-5.5, .4, 2.3, .85, .13, 1.4, '#bb9166'); box(-5.5, .22, 2.3, .12, .28, .9, '#24343d');
  box(-3, .33, 1.6, .6, .5, 2.2, '#293d4b'); box(-3, .6, 1.6, .3, .04, .7, '#24343d');
  windowPane(-7.98, 1.5, 2.45, .05, 1.3, 2.2);
  plant(-4.6, .55);

  // Hall: shoe cabinet, door mat, coat stand, plant.
  box(-.4, .33, .4, 1.4, .54, .5, '#b69470');
  box(-1, .075, 3.2, 1.3, .03, .8, '#425360');
  cylinder(.1, .96, 2.2, .04, 1.8, '#5d4a3c'); cylinder(.1, .09, 2.2, .22, .06, '#5d4a3c');
  for (const [dx, dz] of [[.16, 0], [-.16, 0], [0, .16], [0, -.16]]) box(.1 + dx, 1.75, 2.2 + dz, .1, .04, .1, '#5d4a3c');
  plant(-2.05, 1.3);

  // Study: desk with a monitor, chair, rug.
  box(2.3, .09, 2.4, 2.2, .03, 1.6, '#5b6f6a');
  box(2.8, .78, .55, 2, .08, .8, '#c6ab86'); legs([1.9, 3.7], [.25, .85], .7, '#344954');
  box(2.8, 1.2, .3, 1, .6, .06, '#283e4e'); box(2.8, .88, .3, .12, .12, .12, '#344954'); box(2.8, .835, .3, .45, .03, .3, '#344954');
  legs([2.55, 3.05], [1.35, 1.85], .32, '#344954', .06);
  box(2.8, .45, 1.6, .62, .1, .62, '#436471'); box(2.8, .78, 1.89, .62, .6, .08, '#436471');

  // Children's room: bed, rug, shelf, toys.
  box(5.6, .09, 2.3, 2, .03, 1.8, '#9294a4');
  box(7.35, .32, 2.6, 1, .45, 2.1, '#c5ac85'); box(7.35, .6, 2.6, .95, .2, 2, '#dbccb5');
  box(7.35, .74, 2.91, .97, .1, 1.42, '#c98a7c'); box(7.35, .78, 1.85, .7, .16, .45, '#f2eadc');
  box(6.6, .4, .4, 1.6, .68, .5, '#bba788');
  box(5.3, .26, 2.6, .3, .3, .3, '#d9884f'); box(5.75, .23, 2, .24, .24, .24, '#5a8fb8'); ball(5.1, .29, 1.9, .18, '#d9c24f');
}

function apartment(kit: Kit) {
  const { box, cylinder } = kit, { legs, plant, windowPane, sofa, chair } = pieces(kit);

  // Kitchen: fridge, counter with a hob and a sink, dining table with two chairs.
  box(-4.8, 1, -3.6, .7, 1.9, .7, '#dfe5e6'); box(-4.44, 1.2, -3.35, .03, .7, .05, '#8c9aa0');
  box(-2.8, .5, -3.58, 3.2, .88, .8, '#d3d8ce'); box(-2.8, .98, -3.58, 3.3, .12, .9, '#e3e0d1');
  for (let x = -4; x < -1.3; x += .65) box(x, .5, -3.17, .015, .7, .02, '#87958f');
  box(-2.5, 1.05, -3.58, .85, .025, .55, '#263542'); box(-3.7, 1.05, -3.58, .6, .025, .5, '#889fa6');
  box(-2.6, .75, -1.4, 1.7, .1, .95, '#bc926a'); legs([-3.3, -1.9], [-1.75, -1.05], .66, '#35434a');
  chair(-3.85, -1.4, -1); chair(-1.35, -1.4, 1);

  // Bedroom: bed with its head at the wall, nightstand, window behind the curtains.
  box(3.9, .3, -2.6, 2, .45, 2.6, '#5e5049'); box(3.9, .57, -2.6, 1.95, .24, 2.5, '#d8dde0');
  box(3.9, .73, -2.25, 1.97, .12, 1.7, '#487889');
  for (const x of [3.4, 4.4]) box(x, .77, -3.55, .8, .2, .5, '#f2eadc');
  box(2.45, .41, -3.6, .6, .7, .6, '#a58768');
  windowPane(1.2, 1.5, -3.98, 1.9, 1.3, .05);

  // Living room: rug, sofa under the window, coffee table, TV console.
  box(-2.7, .09, 2.1, 3.4, .04, 2.4, '#384f62');
  sofa(-4.2, 2.2, 2.3);
  box(-2.7, .4, 2.2, .85, .13, 1.4, '#bb9166'); box(-2.7, .22, 2.2, .12, .28, .9, '#24343d');
  box(-.55, .33, 1.5, .6, .5, 2.2, '#293d4b'); box(-.55, .6, 1.5, .3, .04, .7, '#24343d');
  windowPane(-5.18, 1.5, 2.45, .05, 1.3, 2.2);

  // Hall: shoe cabinet, door mat, coat stand, plant.
  box(3.2, .33, .4, 1.4, .54, .5, '#b69470');
  box(2.6, .075, 3.2, 1.3, .03, .8, '#425360');
  cylinder(.6, .96, 1.6, .04, 1.8, '#5d4a3c'); cylinder(.6, .09, 1.6, .22, .06, '#5d4a3c');
  for (const [dx, dz] of [[.16, 0], [-.16, 0], [0, .16], [0, -.16]]) box(.6 + dx, 1.75, 1.6 + dz, .1, .04, .1, '#5d4a3c');
  plant(4.6, 3.3);
}
