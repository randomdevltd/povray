import type { Model } from '../src/index.ts';
import sphere from './sphere.ts';
import cylinder from './cylinder.ts';
import cone from './cone.ts';
import capsule from './capsule.ts';
import frustum from './frustum.ts';
import superellipsoid from './superellipsoid.ts';
import torus from './torus.ts';
import squaretorus from './squaretorus.ts';
import donut from './donut.ts';
import spindle from './spindle.ts';
import vase from './vase.ts';
import bowl from './bowl.ts';
import wineglass from './wineglass.ts';
import pumpkin from './pumpkin.ts';
import column from './column.ts';
import starcolumn from './starcolumn.ts';
import cbeam from './cbeam.ts';
import banded from './banded.ts';
import morph from './morph.ts';
import log from './log.ts';
import wobble from './wobble.ts';
import knot from './knot.ts';
import starknot from './starknot.ts';
import figure8 from './figure8.ts';
import chain from './chain.ts';
import spring from './spring.ts';
import rope from './rope.ts';
import dna from './dna.ts';
import tentacle from './tentacle.ts';
import horn from './horn.ts';
import croissant from './croissant.ts';
import elbow from './elbow.ts';
import curl from './curl.ts';
import scroll from './scroll.ts';
import shell from './shell.ts';
import ramp from './ramp.ts';
import ribbon from './ribbon.ts';
import box from './box.ts';
import mushroom from './mushroom.ts';
import trumpet from './trumpet.ts';
import basket from './basket.ts';
import frame from './frame.ts';
import boulder from './boulder.ts';
import splat from './splat.ts';
import leaf from './leaf.ts';
import pageturn from './pageturn.ts';
import pagecurl from './pagecurl.ts';
import pagecurlthin from './pagecurlthin.ts';
import crumpledpage from './crumpledpage.ts';
import paperball from './paperball.ts';
import paperplane from './paperplane.ts';
import aeroplane from './aeroplane.ts';
import balloondog from './balloondog.ts';
import sock from './sock.ts';
import finger from './finger.ts';
import arm from './arm.ts';
import head from './head.ts';
import mobius from './mobius.ts';
import wizardhat from './wizardhat.ts';
import staff from './staff.ts';
import ramshorn from './ramshorn.ts';
import trunk from './trunk.ts';
import klein from './klein.ts';
import fatknot from './fatknot.ts';

export const examples: Record<string, Model> = {
  sphere, cylinder, cone, capsule, frustum, superellipsoid, torus, squaretorus,
  donut, spindle, vase, bowl, wineglass, pumpkin, column, starcolumn,
  cbeam, banded, morph, log, wobble, knot, starknot, figure8,
  chain, spring, rope, dna, tentacle, horn, croissant, elbow,
  curl, scroll, shell, ramp, ribbon, box, mushroom, trumpet,
  basket, frame, boulder, splat, leaf, pageturn, pagecurl,
  crumpledpage, paperball, paperplane, aeroplane, balloondog, sock, finger, arm,
  head, ramshorn, trunk, wizardhat, staff, mobius, klein, fatknot, pagecurlthin,
};

export const stress: Record<string, [number, number]> = { ramshorn: [480, 1200], trunk: [480, 1200] };

export const failures = ['spindle', 'klein', 'fatknot', 'pagecurlthin'];

export const notes: Record<string, string> = {
  box: 'closed thin (closed lathe profile)',
  basket: 'composite; body is closed thin (closed lathe profile)',
  leaf: 'closed thin (flattened lathe)',
  pagecurlthin: 'closed thin (slab instead of sheet)',
};
