# Disk-backed render caches: design

Status: design for review. Nothing here is implemented.

## Goal

1. Every large in-memory render cache can live on disk. One maximum-memory setting caps what is resident; beyond it,
   caches spill to disk.
2. The existing memory option, which caps photons only, moves to a photon-specific name.
3. When caches do not fit the budget, progressive rendering works region by region instead of pass by pass over the
   whole image. Regions are ordered so that as few cache blocks as possible must be loaded.
4. All caches can be saved, so an interrupted render restarts without rebuilding them. Where everything fits in
   memory, as on large rented machines, this is purely a recovery mechanism.
5. A host that renders this way keeps enough free disk for the spill.

## What the evidence says

The figures below come from recent production renders on a shared 8-core, 16 GB workstation and on rented spot
machines with 64 to 192 vCPUs. The machines and scenes are described generically.

### A restart that rendered nothing

A spot machine was reclaimed 11.5 minutes after it started. Its two jobs were bake renders of one procedurally built
figure scene, and neither had produced a pixel. The relaunched attempt on another machine shows what the job needs
before its first pixel:

| Phase | Seconds | Threads |
|---|---|---|
| Copy inputs to the machine (1489 files) | 206 (223 on the lost machine) | I/O |
| Parse: 53M tokens of 4.0 scene language, with parse-time `trace()`/`inside()` loops that place geometry | 534 | 1 |
| Meshes | 1.5 | 1 |
| Media | 0 | - |
| Lights (one interior light grid) | 1.2 | parallel |
| **First pixel** | about 745 s after start | 16 |

The machine died about 70 s before the first pixel. Everything was lost: the copied inputs, which live on the
machine's disk, and about 87% of the parse. There was no render state file yet, so continuing had nothing to start
from. During those 11.5 minutes the 96-vCPU machine kept two cores busy, one single-threaded parse per job.

**Engine caches would not have saved this run.** The caches built after the parse took under 3 s. The loss was
input copying plus a single-threaded parse, and both are repeated in full on every attempt. Other spot machines the
same day lived 3.8, 5.5, 11.3, 11.5 and 12.8 minutes. Any job that needs more than about 10 minutes before its first
pixel is exposed in the same way.

### Pre-pixel time on other heavy renders

| Render | Parse s (1 thread) | Other pre-pixel work, s | Notes |
|---|---|---|---|
| Garment bake, 18 cameras, spot, 4 attempts | 74 per attempt | lights 19.5 per attempt; input copy 101 to 215 per attempt | the pixels resumed from the state file; parse, lights and copying were repeated 4 times |
| Large garment panel 4925x5320, spot, 4 attempts | 41 per attempt | lights 12 per attempt | 98% of trace time is in the last three progressive passes (steps 4, 2, 1) |
| Subsurface figure 384x384, spot | 62 | bounce 493, 85% of it in the sequential "splits" tail; subsurface cells built lazily in the first pass, 58.6 s | trace 3343 s on 8 threads |
| Forest with volume lights, local | 174 | meshes 199, media 216, lights 62, bounce 4403 (older build; 373 to 421 on current builds) | preparation was 79% of the run |
| Figure with a volume light, local | 100 | lights 1991: one volume light stalled for 32 minutes at 7495/7497 units | never rendered |
| Figure finals, local | 1344 to 1432 | - | parse alone took 22 to 24 minutes |

Three conclusions follow:

- Parse is the largest single-threaded pre-render cost on procedural scenes, and caches cannot capture it (see "Parse").
- Bounce and light preparation are the largest cache builds: 373 to 1036 s for surface bounce on subsurface figures,
  and up to 4403 s for volume bounce.
- Today only the render state file and the radiosity file beside it survive a restart.

### Memory on the workstation

- The kernel's OOM killer fired 8 times in 9 days. Renders were SIGKILLed while other jobs were swapping.
- One subsurface test render accounted 13.3 GB of method 2 cloud: 65.7M points at 212 B each. Its resident peak was
  7.5 GB, so the rest sat in swap. It had declared 1.4 GB to the scheduler.
- Of 1356 renders in 14 days, 182 peaked above 2 GB and 15 above 4 GB.
- Production caches observed:
  - subsurface clouds of 0.2 to 0.7 GB;
  - media density grids of up to 30M cells (about 360 MB) and interior light grids of up to 25M cells, both in a
    forest scene;
  - ray-map camera images of 1.9 GB decoded per scene (45 16-bit PNGs of 3387x6786).
- The scheduler admits work on a declared peak, and no engine cap applies by default except the photon one.
- At the time of writing the workstation had 2.1 GB of RAM available, 4.6 GB of swap in use, and 11 GB of disk free
  out of 301 GB.

## Inventory of large caches

All paths are under `source/`. "Bounded per tile" asks whether the cache blocks touched by one screen tile are a
small, predictable set.

| Cache | Build, threading | Saved today | Memory cap today | Access | Bounded per tile |
|---|---|---|---|---|---|
| Classic photon maps | Shooting is parallel (`PhotonShootingTask`, backend/scene/view.cpp:1373). Sort and kd-tree are single-threaded (`PhotonSortingTask::sortPhotonMap`, backend/lighting/photonsortingtask.cpp:211; `PhotonMap::buildTree`, core/lighting/photons.cpp:1837) | Yes: `save_file`/`load_file`, keyed (`POVPHM2`) | None | World-space kd-tree gather (photons.cpp:2433), about 20 B a photon | No: secondary rays gather anywhere |
| Method 2 photons | Shooting is parallel in batches of 16 shards (view.cpp:2060). The index is single-threaded per set (`BuildProgressivePhotonMaps`, view.cpp:2205) | Yes: `SaveProgressivePhotons`/`LoadProgressivePhotons` (photonsortingtask.cpp:613/741), validated | `Photon_Memory` (see below). Over the cap the render fails | World-space k-NN per receiver partition; 64 B a deposit, 168 B a media segment | Partitions yes, for camera rays only |
| Radiosity octree | Parallel pretrace and final trace (backend/render/radiositytask.cpp:121; `AddBlock`, core/lighting/radiosity.cpp:1240). Grows during the final trace | Yes, as a `.rca` text journal, but with no key or validation | None | World-space octree (`FindReusableBlock`, radiosity.cpp:1522) | No |
| Subsurface method 2 clouds | Built lazily during the trace, parallel (`Trace::BuildSubsurfaceCell`, core/render/trace.cpp:8513; `SubsurfaceCache::Acquire`, core/lighting/subsurface.cpp:504) | No | `subsurface { memory MB }`, off by default. Over the cap the render fails (subsurface.cpp:757) | **World-space cells keyed by integer cube** (subsurface.h:239); a gather reads at most 3 cubes per axis | Yes for camera rays; radiosity samples also build cells |
| Surface maps | Lazy, parallel, chunks published by compare-and-swap (core/lighting/surfacemap.h:395) | No | Charged to the subsurface cap | World-space cube octree per object; 36 B a record | Yes, like the subsurface clouds |
| Subsurface layer and boundary tables | Built on demand | No | **LRU eviction at 32 MB** (`kSubsurfaceTablesMB`, subsurface.h:143); the only evicting cache today | Keyed by parameters | Not spatial |
| Media method 4 density grids | Parallel `ParallelFor` over z slabs (core/material/media.cpp:467), eager during the parse | No | `domain_cells`, coarsened with a warning when over it | One dense box per medium; 12 B a cell | Yes, by the media a tile's rays cross |
| Optical-depth grids | Parallel, **built lazily at the first ray** (`BuildOptical`, media.cpp:648) | No | 2x `domain_cells` | Light-aligned grid | Partly |
| Refraction-field grids | Parallel (media.cpp:1604) | No | `domain_cells`/`detail` | World-space scalar grid; 8 B a cell | Partly |
| Volume light source cells | Inner loops are parallel (media.cpp:2595), but **one light at a time** inside a single `FieldPreparationTask` (backend/bounding/boundingtask.cpp:856) | No | `domain_cells` | World-space lattice | No: shadow and emission draws come from anywhere |
| Bounce fields: volume grids and surface irradiance nodes | `bounce_prepare::Workers`/`ParallelFor` (core/lighting/bounceprepare.h:103). The surface "splits" loop commits in a fixed sequence and only looks ahead in parallel (core/lighting/surfacerefine.h:505-560) | No | `domain_cells`; 4096 surface nodes per light by default | World-space lattice or nearest nodes | No |
| Mesh triangles and per-mesh BVH | Each mesh is built single-threaded; meshes are built concurrently (boundingtask.cpp:458/490) | `.povt` tree cache beside the `.povm` (parser/parser_povm.cpp:306/410), keyed by content hash. **It cannot be written when the input folder is read-only**, so the tree is rebuilt on every render there | None | Object-space BVH; about 54 B a triangle | Per object |
| Generated meshes | Deferred mesh tasks | Yes, with `global_settings { mesh_cache "dir" }`, keyed | Never pruned | As meshes | Per object |
| Scene BVH | Single-threaded (`Build_Bounding_Slabs`/`Build_Flat_BBox_Tree`, core/bounding/boundingbox.cpp:252/686) | No | - | World-space | No (small) |
| Image maps, including ray-map camera images | The parser decodes the whole image (`Parser::Read_Image`, parser/parser.cpp:12398) | No | - | Texture space, no mip pyramid | **Ray-map images are screen-space: exactly bounded per tile** |

The memory peak is not printed on Linux builds: `POV_MEM_STATS` is 0 (base/configbase.h:510). It has to be
measured from outside the process today.

## The memory parameters today

| Parameter | What it controls |
|---|---|
| `photons { memory GB }` in `global_settings` (parser/parser.cpp:8423; pov4.cpp:1074) | Ceiling on method 2 photon storage, in GB of 10^9 bytes. It is the same word as `subsurface { memory MB }`, which takes binary megabytes. The reference documents the two as one item "in two places with different units" (tools/language/reference/overlay.json:317) |
| `Photon_Memory=GB`, then `POV_PHOTON_MEMORY` | Override the scene value, in that order (frontend/processrenderoptions.cpp:625; photonsortingtask.cpp:696-739) |
| Default photon ceiling | 40% of MemAvailable, read after the first batch, at most 6 GiB. The docs say "6 GB" of 10^9 bytes, but the code uses 6x1024^3, which errors report as "6.44 GB" |
| `subsurface { memory MB }` | Opt-in cap on subsurface clouds and surface maps |
| `Max_Image_Buffer_Memory` | Output image buffer before it pages to a file (`FileBackedPixelContainer`, base/image/image.cpp:3024). This is the only engine data that already pages |
| Scheduler-side declared peak (outside the engine) | Admission only. It sets no container limit and is never passed to the engine |

The misleading one is the bare `memory` inside `photons { }`. It reads like a general budget, it shares its word
with the subsurface cap but not its units, and its default depends on what else the machine is doing. Once a
general budget exists, that word belongs to the budget.

## Pixel scheduling today

- **Blocks.** The image is cut into `Render_Block_Size` blocks, 32 pixels by default (backend/scene/view.cpp:1074).
  `Render_Pattern` orders them (default row-major; view.cpp:146-254) and `Render_Block_Step` strides through them.
  Threads claim blocks from a shared counter under a mutex (`GetNextRectangle`, view.cpp:256).
- **Progressive passes (`+PR`).** `QueueProgressiveRender` (view.cpp:2242) builds log2(max side)+1 lattice levels,
  each halving the step. A level traces only the lattice points that are new at its step, one sample each (a mosaic
  refinement). AA refine passes or method 4 rounds follow. Every pass covers the whole image. Idle threads steal
  items from the busiest block (`TraceTask::RunPass`, backend/render/tracetask.cpp:945).
- **Cost by pass.** Each level costs about 4x the one before. On a 26-megapixel render the last three levels took
  98% of trace time and every level up to step 8 took under 2%.
- **Continue (`+C`).** The `.pov-state` file keeps the highest level reached and the blocks finished in it
  (frontend/renderfrontend.cpp:686). The radiosity cache is autosaved beside it. Parse, photons and every
  preparation phase start over.
- **Threading.** Parse is single-threaded. Eager mesh and grid builds use `+WT`-1 helper threads during the parse.
  Preparation runs as tasks on a process-wide `WorkerPool` (core/support/workerpool.h:20) with `ParallelFor` inside.
- **Footprints.** No per-tile record of cache use exists. `BlockInfo` is empty (backend/scene/view.h:195).

## Design

### 1. One memory budget

**Option.** `Max_Cache_Memory=GB` on the command line or in an INI file, and `POV_CACHE_MEMORY` in the environment.
Like the photon ceiling, it is a property of the machine, not of the scene. A scene may state a default with
`global_settings { cache_memory GB }`, and the command line wins. Without any of them there is no budget, and
behaviour is exactly as today.

**Registry.** A process-wide `CacheBudget` tracks every large cache as a set of blocks. Each block records its
bytes, its state (building, resident, spilled, pinned), a use clock and a pin count. A reader pins a block for the
duration of one lookup, the way subsurface cells are acquired today. Eviction is least-recently-used with a second
chance (CLOCK) over unpinned, complete blocks. An evicted block's memory is released; the next pin loads it again.

**What a block is, per cache.** A block is a section of the cache's file (section 2): it has its own world-space
bounds, offset, length and checksum, so it loads, maps or verifies alone. A large cache that covers the whole image,
such as a fine media grid or a 13 GB subsurface cloud, is never held whole: a region loads only the sections it
touches.

| Cache | Block |
|---|---|
| Subsurface clouds | One cell (the existing integer-cube key) |
| Surface maps | One cube chunk |
| Media density, optical and refraction grids | A 3D brick of up to 32 x 32 x 32 cells; the brick size is in the index |
| Light source lattices and bounce grids | A brick of up to 32 cells a side of the lattice's own nodes or cells |
| Bounce surface nodes | A spatial bucket of the node index |
| Method 2 photons | One receiver partition |
| Meshes | One mesh; a mesh above a size limit splits into subtrees of its BVH, each with its box |
| Image maps | Rows in bands of 64 |
| Ray-map images | Bands, so a screen tile maps to known bands |
| Classic photon map | A subtree of the kd-tree below a fixed depth, each with its box |

Bricks rather than z slabs: a slab spans the whole grid in x and y, so a region at one side of the image would load
the width of the volume; a brick bounds what it holds on every axis.

**Two ways to page.**

- *Immutable after build* covers the grids, lattices, bounce fields, photons, meshes and images. On completion the
  block is written once to the spill file and the heap copy is freed. Readers then use a read-only `mmap` of the file.
  Clean file-backed pages cost nothing to evict: the kernel drops them under pressure instead of swapping, and the
  budget evicts them explicitly with `madvise(MADV_DONTNEED)`. This one change removes most of the swap storms seen
  on the workstation, because large caches stop being anonymous memory.
- *Built lazily during the trace* covers subsurface cells, surface map chunks and optical grids. A block becomes
  spillable when it is complete. Until then it is pinned and charged.

**Caches that stay whole.** Every cache with a spatial layout is sectioned, however large. Only structures that are
not spatial stay whole and pinned, each for its own reason:

- The scene BVH: every ray starts at its root and descends through its top levels, so every region touches it; it is
  small in every scene measured.
- The subsurface layer and boundary tables: they are keyed by material parameters, not by position, and already evict
  under their own 32 MB cap.
- A light's sampling tables, the cumulative distribution over its cells and lobes that chooses where a sample lands:
  a draw anywhere in the image may pick any cell, so the table is needed whole. It is one number or a few per cell,
  far smaller than the light's own colour lattice, which is sectioned.
- The cache index itself: the map from section ids to bounds and offsets that selective loading reads first.

The radiosity octree is spatial but pointer-linked and grows during the final trace. It becomes sectionable once its
nodes are offset-linked in append-only blocks per top-level octant; until then it is pinned. No recent production
render used radiosity.

Pinned caches are charged to the budget. When the pinned total alone exceeds the budget, the render stops before the
first pixel with an error that lists each pinned cache and its size. This follows today's photon and subsurface cap
errors, and the render never thrashes silently.

**Enforcement order.** Each charge checks resident + charge <= budget. If the check fails, the registry evicts
unpinned blocks. If it still fails, it waits briefly for pins to drop. As a last resort it overshoots by up to one
block per thread and counts the overshoot in the statistics. A hard failure happens only for the pinned case above.
The existing subsurface `memory` cap and the photon ceiling become caps on total size, which now means disk, not
RAM. They keep their behaviour of failing early with a clear message.

**Statistics.** At the end of every render the engine prints one line per cache with:

- built, peak resident and spilled bytes;
- loads and evictions;
- footprint misses (see region scheduling);
- the process's peak resident memory, `VmHWM` from `/proc/self/status`.

### 2. Spill format and location

- **Directory.** `<state file>.caches/` beside the `.pov-state`, or `Cache_Path=<dir>` when given. It must be on a
  local disk. A network mount is not acceptable, because page faults would turn into network round trips.
- **File layout.** One file per cache; sections are never split into files of their own. Each file has:
  - a 4 KiB header: magic, format version, byte order, the build ID, the cache key (see section 4), the section
    count, the index's offset and checksum, the cache's name and a checksum of the header;
  - the sections, little-endian, holding offsets, never pointers. A section of a page or more starts on a 4 KiB page
    boundary, so it can be mapped from the file alone; a smaller one is aligned to 16 bytes. (The first draft said
    64 KiB; a page is what mapping needs on the platforms in use, and 64 KiB padding bloated the many small bounce
    sections.);
  - a trailing section index: per section its offset, length, 64-bit checksum, id, first cell and cell counts, its
    world-space bounds and a label. A brick's id is its grid's number in the file (high 32 bits) and its brick
    number; a section that is not part of a grid has an id of all ones and no cells.
- **Partial loading.** The index is read and checked on its own, and then any subset of sections is read and its
  checksums checked, without reading the rest. Increment 1 loads every section; increments 3 and 5 load selectively
  with no format change.
- **Writes.** Files are written through a temporary file and a rename. That is the pattern the `.povt` mesh tree
  cache already uses (parser/parser_povm.cpp:934), so a file is either complete or absent.
- **The spill file is the save file.** No second format exists.
- **Size.** The spill is at most the sum of the caches, because blocks are written once. From the renders above:
  - 1 to 4 GB for typical heavy scenes (subsurface clouds 0.2 to 0.7 GB, decoded ray maps 1.9 GB, forest grids
    about 1 GB);
  - 13 GB for the worst subsurface test seen;
  - up to the photon cap for photon scenes.
- **Free-space policy on a rendering host.**
  - The scheduler admits a spilling job only when free disk >= a fixed reserve + the job's expected spill. The
    proposed reserve is 40 GB; the expected spill is taken from its last run or its declaration.
  - Spill directories are deleted when the job ends.
  - Saved caches kept for a resumable job are deleted when the job finishes or is cancelled.
  - Saved caches kept for reuse across jobs are pruned nightly, least recently used first, down to a cap of 50 GB.
  - A host should keep at least 60 GB free in total. The workstation measured here has 11 GB free and needs clean-up
    first.

### 3. Region scheduling

Region scheduling applies only when the projected resident set exceeds the budget. When everything fits, as on a
large machine, scheduling is unchanged.

**Footprints from the cheap passes.** The progressive levels at steps of 8 and coarser cost under 2% of the trace,
and they already sample every part of the image. While they run, each thread records the cache blocks that its rays
touch per tile. A tile is 64x64 pixels, a multiple of the block size. Rays of every kind count: camera, reflected,
refracted, shadow, gather. The record is a small open-addressing set of 32-bit block ids per thread. It is merged
into the tile's sorted id list when the block completes, through the existing `CompletedRectangle` path. Memory is
T tiles x F ids x 4 B: a 26-megapixel image has 6,400 tiles, so at F = 200 the record takes 5 MB. A lookup of a block
outside the recorded footprint still works through demand loading, and is counted as a footprint miss.

**Ordering.** The goal is the tile order that minimises total bytes loaded under budget B. This is a variant of
minimum-fault page sequencing and is NP-hard, so a heuristic is used:

1. **Baseline: Hilbert order over tiles.** This is O(T log T). Screen proximity gives world proximity for camera
   rays, and it is the fallback when footprints are missing.
2. **Greedy working-set schedule.** An inverted index maps each block to the tiles that need it. Starting from the
   tile with the largest footprint, the scheduler repeatedly takes the unscheduled tile with the smallest marginal
   load: the bytes of its blocks that are not resident. Ties go to the nearer tile in Hilbert order. Each block that
   is loaded lowers the marginal cost of the tiles listed under it in the inverted index. A lazy priority queue
   keeps this at O(T·F·log T) for the whole schedule, a fraction of a second for the sizes above.
3. **Eviction for the fixed order: Belady's MIN.** Once the order is fixed, each block's next use is known, so
   evicting the block used farthest in the future is optimal for that order. This costs O(T·F·log B) with a heap.

**Clusters and passes.** Scheduled tiles are cut into clusters. Each cluster is a maximal run whose union footprint
fits in 0.8xB. The early passes stay pass-major over the whole image, so the progressive preview appears as today.
The remaining passes (step 4, 2, 1, then AA refine) run cluster-major: all of them finish on one cluster before the
next cluster starts. Work stealing stays inside a cluster. A one-cluster schedule is exactly today's behaviour.

**Continue.** The state file gains a per-tile finished level. Resume skips tiles already at the final level and
replays the schedule from the first unfinished cluster.

**Determinism.** Pixels must not depend on the schedule. Grids, lattices, bounce fields and photons are built before
the trace and do not. Lazily built subsurface cells and surface map chunks must produce the same content whichever
tile asks first. This is checked by rendering one scene in row-major order and in a shuffled order and requiring
byte-identical images, with the build digests the engine already prints as the second check.

### 4. Saving caches

**Key.** The parser records every file it opens (scene, includes, images, `.povm`, data files) and hashes its
contents as it reads. A cache key is the hash of:

- that input digest;
- the build ID;
- the `Declare` values;
- the tag filter;
- the settings that shape this cache (for example `domain_cells`, bounce passes, photon quality).

A deterministic parser makes this sound without serialising the scene. View-independent caches (fields, bounce,
lights, media, photons, subsurface cells, meshes) leave the camera out of the key, so other cameras, other jobs and
later renders of the same scene can reuse them. View-dependent ones (screen pretraces, ray maps) include it. A scene
that reads the clock or other environment-dependent values makes its caches uncacheable: the parser marks the input
digest unstable when such a built-in is evaluated. The eager-preparation code already tracks which functions a
snapshot cannot take, which is a similar check.

**When.** Each preparation phase writes its caches when it finishes: meshes, media, lights, bounce, photons. Lazily
built blocks are appended as they complete, and the block index is rewritten at each state-file checkpoint.

**Load.** Before building a cache the engine looks for a file with its key in the cache directory (`Cache_Path`
when given, else beside the state file). It validates the header and the hash of each block. A mismatch means a rebuild and a one-line warning;
a cache is never used silently. A `Cache_Verify=on` mode builds anyway and compares digests, for tests.

**Resume on rented machines.** The job runner carries `<state file>.caches/` the way it carries the state file:

- Files are published when closed, not resent every few seconds.
- Where a content-addressed object store is available in the machine's region, the machine uploads each closed cache
  file there under its key. A restarted machine fetches the files before starting the renderer.
- Without such a store, the runner pulls closed files back to the submitting host once.

**What this does not save.** Parse, and the copying of inputs. On the restart above those were the whole loss (see
"Parse").

### 5. Single-threaded builds worth parallelising

| Build | Evidence | Change | Cost |
|---|---|---|---|
| Volume light preparation runs one light at a time | One light stalled for 1991 s at 7495/7497 units while the others had finished | Prepare independent lights concurrently; split a large light's axis work finer | Low |
| Surface bounce "splits" tail | 85% of a 493 s bounce was spent in the last 9% of its units | Measure utilisation first. If it is serial, widen the look-ahead while it stays deterministic. The committed sequence is already defined independently of thread count | Medium |
| Method 2 photon index | Single-threaded per set | Build sets and receiver partitions in parallel | Low |
| Classic photon sort and kd-tree | Single-threaded | Parallel median split | Low |
| Scene BVH | Single-threaded, but small in every observed scene | Not worth it now | - |
| Mesh tree on read-only inputs | Rebuilt on every render because the `.povt` cannot be written next to the `.povm` | Write it to `Cache_Path` | Trivial |

Every preparation phase gets one line giving its wall time, CPU time and threads, like the existing "Mesh build
phase" line, so that utilisation is measured rather than guessed.

### 6. Parse

Parse is single-threaded and is not a cache. On procedural scenes it is the largest pre-pixel cost: 534 s and 1344
to 1432 s in the renders above. No engine cache saves it. Three options exist:

1. **Profile first.** The parser can report time per include file and per user function. Two hours of work would
   show whether the time goes to parse-time `trace()`/`inside()` loops, token evaluation or file decoding.
2. **Memoised parse-time values.** A scene-language form that stores the value of an expensive deterministic
   function (arrays, dictionaries, vectors, numbers) on disk. It is keyed by the function's code, its arguments and
   the hashes of the files it read, like `mesh_cache` for generated meshes. It is generic and reusable, and it would
   make the restart above lose seconds instead of minutes.
3. **Scene-side bake.** The scene writes its own placements to a data file once.

This is outside the owner's cache design and is listed as a decision.

### 7. The rename

The changes:

- `photons { memory GB }` becomes `photons { photon_memory GB }`.
- `Photon_Memory=` and `POV_PHOTON_MEMORY` keep their names, which are already photon-specific.
- With a budget in place, the photon ceiling stops meaning "RAM photons may use" and means "largest photon map
  allowed", in GB. Its default changes from 40% of currently available memory, which made the same scene pass or
  fail by machine load, to no limit when a budget is set. Without a budget the current default is kept.
- The docs and the code agree on 10^9 bytes; the default becomes 6e9, not 6x1024^3.
- `subsurface { memory MB }` keeps its name for now, but its meaning changes in the same way: it becomes a size
  limit, with residency governed by the budget.

The migration:

- In scenes below `#version 4.0`, `memory` inside `photons` is still accepted, with a one-line deprecation warning
  naming `photon_memory`.
- In `.pov4` scenes, the codemod rewrites `memory` to `photon_memory`, and `memory` inside `photons` becomes a parse
  error that suggests the fix after one release.
- The reference tables (tools/language/reference), `doc/progressive-photons.md`, `doc/html/r3_2.html`/`r3_4.html`,
  `unix/povray.1`, the test scenes under `tests/render/progressive_photons*` and `subsurface_photons_method2.pov`,
  and the parser schema (`kPov4BlockItems_photons`) are updated together.
- A job runner that knows a job's memory allowance passes `Max_Cache_Memory` derived from it, so the declared
  allowance and the enforced one are the same number. It should also set the container memory limit a little above
  it, so that an over-budget render fails alone instead of taking the host down.

## Increments

Each increment starts by adding the statistics line it is measured on. Gains are measured as user-space
instruction counts from the hardware counters, and peak resident memory as `VmHWM`. Wall-clock time is never used.

| # | Increment | Measured gain | Test scene | Size | Main risk |
|---|---|---|---|---|---|
| 1 | Cache keys, the cache directory and save/load of prepared fields: bounce, light lattices, media, optical and refraction grids. Method 2 photons join through their existing files. The mesh tree is written to `Cache_Path`. | A second render of the same scene, or a resumed one, skips preparation. Bounce and light instruction counts should fall by more than 95% (373 to 1036 s per attempt on subsurface figures; 4403 s on the old forest build) | `tests/render/bounce_preparation.pov`, `bounce_self.pov`, `interior_axis_media.pov`: fresh against reloaded, with `Cache_Verify` digest equality | 2 to 3 days | A stale key gives a wrong image. Mitigation: input-file hashing, per-block hashes, verify mode in tests |
| 2 | The job runner carries the cache directory across restarts: an object store where there is one, a single pull otherwise | On a forced restart after preparation, the second attempt's pre-pixel instructions equal parse plus load | The same scenes, with a restart forced by killing the container after preparation | 1 day | Upload time on short-lived machines. Mitigation: publish each file as its phase finishes |
| 3 | `Max_Cache_Memory` with the CacheBudget registry. Immutable caches become file-backed and mapped. Subsurface cells, surface map chunks and image bands spill. The photon rename ships in this increment | Peak resident memory capped at the budget. For example, a subsurface scene with a 13 GB cloud under a 2 GB budget runs without swap. Instruction overhead at most 5% when the budget is not binding | `tests/render/subsurface_photons_method2.pov` scaled up with `domain_cells`; a dense media scene | 3 to 5 days | Page-fault overhead in hot lookups. Pointer-linked structures need an offset form |
| 4 | Parallelise volume light preparation across lights, measure and widen the bounce look-ahead, and build photon indexes in parallel | Instructions unchanged, phase wall/CPU ratio towards the thread count. The light stall case finishes in roughly 1/k of the time | `light_emission.pov` with several volume lights; `bounce_adaptive.pov` | 1 to 2 days | Determinism: the bounce sequence must stay thread-count independent |
| 5 | Footprint recording in the coarse passes, the greedy schedule with MIN eviction, cluster-major final passes, and a per-tile level in the state file | With the budget binding, bytes loaded within 1.5x of the footprint union, and footprint misses under 1% of lookups | A wide scene whose subsurface or media cache is 4x the budget | 4 to 6 days | Progressive behaviour and the state-file format change. A lazily built cell that depends on tile order would change pixels |

Order rationale:

- Increment 1 has the largest gain at moderate risk, and its file format is the one that every later step reuses.
- Increment 2 is what makes that gain count on rented machines.
- Increment 3 addresses the workstation's out-of-memory failures.
- Increment 4 is independent, and could run in parallel with 3.
- Increment 5 matters only once the budget is binding, and carries the most risk, so it comes last.

None of these increments would have saved the restart that rendered nothing. That needs the parse decision in
section 6, plus faster input copying, which is outside the engine.
