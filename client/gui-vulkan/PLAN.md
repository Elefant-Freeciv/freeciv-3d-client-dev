# 3D Vulkan map renderer for the `gui-vulkan` client

Status: **M0 complete** (client builds, runs as `gui-vulkan`, Vulkan context +
composited frame present). Everything below M1 is design + work plan.

Owner docs: this file. Art source of truth:
`freeciv_isometric_voxel_terrain_studio_19_tiles_improvements_glb.html`
(the "terrain studio" page, three.js, 2 962 lines).

---

## 1. Goal

A **fully featured, playable** 3D map view in a new client
(`client/gui-vulkan/`, binary `freeciv-vulkan`), rendering the Freeciv map as
the voxel/isometric art of the terrain studio page, on a hand-written Vulkan
renderer.

"Fully featured and usable" is defined as the acceptance list in §9. In short:
a player must be able to start a game, see the whole map in 3D, pan/zoom/rotate,
select units, issue orders by clicking the 3D map, see cities, improvements,
units, fog of war, borders, goto routes and city bars, and play a full game to
victory without ever switching to a 2D client.

### Non-goals

* No rewrite of the 2D GUI. The `gui-sdl3` widget toolkit (windows, buttons,
  dialogs, fonts, themes, city dialog, science report, …) is kept verbatim and
  composited over the 3D map.
* No glTF/three.js runtime dependency. The page is the *art specification*;
  the client generates the same geometry natively (deterministic, no assets to
  ship, no licensing questions about generated meshes).
* No changes to server/protocol.
* Not a general scene graph engine — a map renderer with a fixed, small set of
  draw passes.

---

## 2. Where the art comes from

The page builds every tile from merged axis-aligned boxes ("voxels"):

| Concept in the page | Value | Where it lives in the client |
|---|---|---|
| Tile footprint | 10 × 10 world units | `VOX_TILE_UNITS` |
| Art cell | 0.5 × 0.5 units, 20 × 20 cells per tile | `VOX_CELL_UNITS`, `VOX_CELLS_PER_TILE` |
| Terrain surface height | 0.14 – 0.40 (per biome `EDGE_PROFILES.h`) | `voxel_profiles[].height` |
| Biome palette | 3 dither colours per biome | `voxel_profiles[].colors` |
| Plinth | 3 stacked slabs below y = 0 (soil/sand/ocean/ice/swamp) | `voxel_plinths` |
| Water plane | per-biome `waterY` (0.16 – 0.24) | `voxel_profiles[].water_y` |
| Edge blending | `blendCellWithNeighbors()`, feather radius 1–5 cells, "bool"/"full" styles | `voxel_blend_cell()` |
| Determinism | `mulberry32(seed)` per generator (forest 101, meadow 202, …) | `voxel_rand_next()` |
| Props | `addVoxelTree`, `addPineTree`, `addPitchedBuilding`, `addSheep`, `addCow`, `addVoxelSoldier`, `addVoxelHorse`, `addVoxelShipHull` | `voxel_props.c` (M5) |
| 19 tile generators | forest, meadow, plains, jungle, hills, mountains, desert, swamp, tundra, ice, coast, lake, ocean, metropolis, ancient, medieval, homestead, watermill, redbarn | `enum voxel_art_id` |
| 23 improvements | road, railroad, maglev, river, canal, bridge, irrigation, farmland, mine, oil well, oil platform, fort, fortress, castle, bunker, airstrip, airbase, buoy, hideout, village, ruins, pollution, fallout | `enum voxel_improvement` |
| 51 units | 9 civilian, 6 infantry, 5 mounted, 5 gunpowder, 6 armour, 12 naval, 8 air | `voxel_unit_art.c` (M6) |
| Lighting | hemi `#eef4ff`/`#6e5a47` ×0.54, key `#fff5e0` ×1.12 from (16,26,14), rim `#b3dcff` ×0.24, ACES tone map, exposure 0.98, sRGB out | `shaders/scene.frag` |
| Camera | perspective fov 36°, near 0.1, far 300, start (13.5, 11.5, 13.5) → target (0, 0.8, 0) | `vk3d_map_view_default()` |
| Animation | water wheel spin, water bob `sin(t*1.8)*0.025`, rotor `t*10` | M7 |

`sRGB → linear` conversion (`voxel_color_linear()`) is done CPU-side, matching
`convertSRGBToLinear()` on the page; the fragment shader converts back after
tone mapping so the result matches the page pixel for pixel.

---

## 3. Architecture

```
SDL3 window (SDL_WINDOW_VULKAN)
│
├── Vulkan (vk3d_core.c)  ── owns the swapchain and presents
│     pass 1: scene      (vk3d_map.c)   voxel terrain, water, props, units
│     pass 2: overlay    (vk3d_core.c)  the 2D widget layer, alpha blended
│
└── gui-sdl3 widget toolkit (unchanged) draws into main_surface (CPU RGBA)
      dialogs, chatline, menus, city dialog, reports, fonts, cursors
```

Why this shape: `gui-sdl3` renders the whole UI into CPU surfaces and presents
with `SDL_CreateRenderer`/`SDL_RenderPresent` — all of it funnelled through the
single function `update_main_screen()` in `graphics.c`. That gave exactly one
integration point, which is now:

```c
void update_main_screen(void)      /* client/gui-vulkan/graphics.c */
{
  if (vk3d_is_active()) { vk3d_present(main_surface); ... }
  else                  { classic SDL3 renderer path }
}
```

If Vulkan is unavailable (no ICD, no GPU, driver too old) `vk3d_init()` fails,
`set_video_mode()` recreates the window without `SDL_WINDOW_VULKAN` and the
client behaves exactly like `freeciv-sdl3`. That keeps the client usable on
every platform `gui-sdl3` supports while the 3D path matures.

### Module map (`client/gui-vulkan/`)

| File | Responsibility | Milestone |
|---|---|---|
| `vk3d.h` / `vk3d_core.c` | instance, device selection, swapchain, render pass, GUI overlay upload + composite, present, resize | M0 ✅ |
| `vk3d_map.h` / `vk3d_map.c` | camera/view state, chunk cache, scene draw | M2…M7 |
| `vk3d_mesh.c` | CPU voxel mesher (merged boxes, per-material buckets, face culling), chunk buffer upload | M2 |
| `vk3d_shaders.c` | pipeline creation (scene, water, props, instanced props, picking, shadow) | M2…M7 |
| `vk3d_text.c` | font atlas → GPU texture for 3D-space labels (city names, unit counts) | M5 |
| `vk3d_pick.c` | GPU/CPU picking: window coordinate → map tile | M4 |
| `voxel_art.h` / `voxel_art.c` | art catalogue: profiles, palettes, plinths, blending, ruleset→art mapping | M1 ✅ (catalogue) |
| `voxel_gen.c` | the 19 terrain generators → cell heights + prop lists | M2 |
| `voxel_props.c` | prop builders (trees, buildings, animals, improvements) | M2/M5 |
| `voxel_unit_art.c` | the 51 unit models + player-colour tinting | M6 |
| `img_load.h` / `img_png.c` | `IMG_Load()` with libpng when SDL3_image is absent | M0 ✅ |
| `shaders/*.glsl` | overlay.vert/frag (M0 ✅), scene.vert/frag (M2), water, shadow, pick | M2…M7 |

### Data flow, map → GPU

```
server packet → packhand → refresh_tile_mapcanvas()/refresh_unit_mapcanvas()
   → vk3d_map_tile_dirty(tile)            (chunk-granular dirty bits)
   → next frame, for each dirty chunk:
        voxel_gen_tile(art_id, neighbours, seed)  → 20×20 cell heights+palette
        voxel_blend_cell(...)                     → seam blending vs. neighbours
        vk3d_mesh_add_box(...)                    → position/normal/colour
        vkCmdCopyBuffer → chunk vertex+index buffer
   → draw: frustum-cull chunks, bind pipeline, push view/proj+light, drawindexed
```

Chunk = 8 × 8 tiles (`VK3D_CHUNK_TILES`). A chunk mesh is rebuilt only when a
tile in it (or one of its 1-tile halo, needed for blending) changes. Blending
makes a tile's edge cells depend on its neighbours, so a tile change marks the
3 × 3 chunk neighbourhood dirty when the tile is on a chunk border.

Budget: 64 × 64 tile map = 64 chunks ≈ 1.6 M triangles worst case; with the
face-culling and cell-merge rules of the page (interior faces are never
emitted, a flat area of equal-height cells merges into one box per row run) a
typical land chunk is ~12 k triangles. Target: 60 fps at 1080p on a 2016-era
iGPU with shadows off.

---

## 4. Renderer design

**Pipelines**

1. `scene` — voxel terrain. Vertex: `pos3, normal3, colour3` (linear). Push
   constants: `view_proj, camera_pos, sun_dir, sun_color, fog_color,
   fog_density, time, tile_size`. Lighting exactly as the page (§2). Depth
   enabled, backface culling on (the page's boxes are closed).
2. `water` — translucent plane per water tile at `water_y`, animated normals
   from a cheap procedural ripple in the vertex shader, Fresnel-tinted, depth
   pre-pass first so water is drawn after opaque with depth-write off.
3. `props` — cities, improvements, trees, animals: instanced draws. Each prop
   model is uploaded once to a shared vertex buffer; per-instance data is
   `mat4 (translation+scale+Y rotation), colour tint, model id`. Keeps a
   forest-heavy map at a few hundred draws.
4. `units` — same instanced path, plus a per-player colour slot and a 2-frame
   "march" bob; naval units sit on the water plane and bob with it.
5. `shadow` — one directional shadow map (2048², cascaded 2×) rendered from the
   sun direction; `scene`/`props` sample it. Toggleable (`gui_vulkan_3d_shadows`).
6. `labels` — city names / unit stacks / production, drawn from a font atlas
   generated from the theme font via SDL3_ttf into a GPU atlas; billboarded,
   depth-tested so they hide behind hills.
7. `overlay` — the 2D widget layer (M0 ✅, currently opaque; alpha in M4).
8. `pick` — id-buffer pass at 1/4 resolution: each tile writes a packed tile
   index as colour, one pixel is read back at the cursor (async, one frame
   latency, ring buffer + fence so it never stalls the frame).

**Camera** — orbit rig around a ground point, matching the page: yaw/pitch
clamp `0.12 … 0.48π`, distance 6 … 110 units, fov 36°. Right-drag/shift-drag
pans along the ground plane, wheel zooms towards the cursor, middle-drag
rotates. Smoothed with the page's damping factors (0.25 apply, 0.75 decay).

**Fog of war / visibility** — per-tile material flags in the mesh: *unknown*
(no geometry, dark void + grid), *tile seen but not visible* (desaturated,
50 % darkening, no props), *visible* (full). Because this is per-vertex colour
in the chunk mesh, a visibility change marks chunks dirty; to avoid remeshing
on every unit move, visibility is a **separate per-chunk uniform/vertex-colour
update** (a small colour VBO per chunk updated without touching geometry).

**Borders / roads overlays** — drawn as thin extruded ribbons just above the
terrain from the same tile loop that the 2D client uses
(`mapdeco_*`, `tile_borders`), so behaviour matches the other clients.

---

## 5. Freeciv integration

Hooks into the existing client code (all of these already exist and are called
by `mapview_common.c` / `climap.c` / `packhand.c`):

| Existing entry point | 3D client implementation |
|---|---|
| `refresh_tile_mapcanvas()` | `vk3d_map_tile_dirty()` |
| `refresh_unit_mapcanvas()` / `refresh_city_mapcanvas()` | `vk3d_map_tile_decor_dirty()` |
| `dirty_rect()` / `dirty_all()` / `flush_dirty()` | overlay dirty + chunk dirty flush |
| `tileset_changed()` | `vk3d_map_invalidate_all()` + rebuild art→ruleset cache |
| `update_map_canvas(x, y, w, h)` | no-op (kept for the 2D fallback path) |
| `mapdeco_set_highlight/crosshair/gotoroute()` | 3D ribbon overlays |
| `update_info_label()` / `update_unit_info_label()` | unchanged (2D layer) |
| `put_cross_overlay_tile()` | 3D crosshair mesh |

Ruleset → art mapping lives in `voxel_art.c` and is rebuilt on
`tileset_changed()`/ruleset load:

* terrain → `enum voxel_art_id`: name match first (`voxel_art_for_tile()`,
  implemented), then terrain flags (`terrain_flag_id_by_name("Ocean"/…)`),
  then `is_ocean()` fallback. Cities pick `VOX_ART_CITY` ground + city props.
* extras → `enum voxel_improvement`: `voxel_improvement_for_extra()`
  (implemented, name based); to be hardened with extra flags
  (`E_HIGHWAY`/`E_RAILROAD`/`E_MAGLEV` road types, `EF_*`) so custom rulesets
  map sanely.
* unit type → `voxel_unit_art.c` by unit flag (`f_`/`utf_*`: Sea, Air,
  Missiles, Settle, CityDestroyer, …) with a name table for the classic 51.

**2D/3D coexistence in the widget layer.** The map viewport rectangle in
`main_surface` becomes transparent (alpha 0) and the overlay pipeline switches
to real alpha blending (M4). Everything outside that rectangle — bars,
minimap, dialogs — keeps painting as today, so no dialog code changes.

**City dialog, reports, editor**: untouched. `do_city_map()` and the canvas API
(`canvas.c`) keep working on CPU surfaces; the 3D client only replaces the map
viewport.

**Options** (`client/options.c`, already registered for `GUI_VULKAN`):
`gui_vulkan_3d_enabled`, `gui_vulkan_3d_shadows`, `gui_vulkan_3d_lod_bias`,
plus the inherited SDL set (fullscreen, resolution, fonts, theme). Later:
vsync, anisotropic filter, water detail, "rotate map to north" and a
`--tiles`-style `--renderer` debug flag.

---

## 6. Input

Handled in `mapctrl.c` (existing) with the map branch redirected to the 3D
view:

* Left click → `vk3d_pick_tile(x, y)` → existing `left_click_tile()`.
* Left drag → selection rectangle → `select_units_rectangle` (project the
  rectangle to a ground quad, select units whose tile is inside).
* Right click → `right_click_tile()`; drag → pan.
* Middle drag → rotate, wheel → zoom, `Shift` + wheel → pitch.
* Keyboard: existing bindings (`mapctrl.c`) unchanged; add `Numpad 4/6` rotate,
  `Numpad +/-` zoom, `Home` reset camera.
* Smooth unit movement (`smooth_move_unit_msec`) interpolates the instance
  transform along the goto path instead of blitting sprites.

---

## 7. Milestones

Each milestone must leave the client buildable and runnable, with the 2D
fallback intact.

### M0 — Client skeleton + build ✅ *(this commit)*
* `client/gui-vulkan/` forked from `gui-sdl3`; `GUI_VULKAN` gui type
  (`common/fc_types.h`), own option set (`client/options.{c,h}`), theme
  directory fallback `gui-vulkan → gui-sdl3`.
* Meson: `-Dclients=vulkan`, glslc shader build + SPIR-V embedding, `vulkan`
  dependency, libpng `IMG_Load()` fallback when SDL3_image is missing.
* Autotools: dist-only (`client/gui-vulkan/Makefile.am`), like `gui-sdl3`.
* Vulkan instance/device/swapchain, GUI overlay composite, SDL fallback.
* Verified: `freeciv-vulkan --version` → `gui-vulkan`; `freeciv-gtk4` still
  builds (shared files unbroken).

### M1 — Art catalogue ✅ *(this commit)*
`voxel_art.{h,c}`: 19 profiles, 5 plinths, 23 improvements, blending,
mulberry32, sRGB→linear, ruleset name mapping.

### M2 — Terrain on screen *(the "it's 3D" milestone)*
* `voxel_gen.c`: port the 19 generators' **ground** pass (cell heights +
  palette dither + plinth + water plane). Deterministic seeds from the page.
* `vk3d_mesh.c`: box emitter with the page's face list, interior-face culling
  (skip a face when the neighbouring cell has covering height), per-material
  bucketing, 16-bit indices, chunk vertex/index buffers.
* `scene` pipeline + `scene.vert/frag` (already compiling in-tree), depth
  buffer + depth-resolve, camera from `vk3d_map.c`, chunk frustum culling.
* Wire `refresh_tile_mapcanvas()` → chunk dirty.
* **Accept:** a new game shows the whole map in 3D; land/sea/hills/mountains
  read correctly; scrolling and zoom are smooth; memory < 300 MB on a 128² map.

### M3 — Water, fog, atmosphere
* Water pipeline with animated ripples, `water_y` per biome, translucent
  shallows over sand, whitecaps on ocean (page's `animWater`).
* Distance fog to `#dedbd4`, exponential² like the page's background gradient.
* **Accept:** coastlines look like the page's "Random Map" mode; no z-fighting;
  fog does not swallow the near map.

### M4 — Camera, picking, transparent map viewport
* Orbit/pan/zoom rig with damping, `vk3d_pick.c` id-buffer picking, overlay
  alpha blending with a transparent map rectangle, resize handling
  (`SDL_EVENT_WINDOW_RESIZED` → `vk3d_resize()`), windowed↔fullscreen.
* **Accept:** clicking a unit in 3D selects it; orders (move, attack, fortify,
  build) work from the 3D view; dialogs still receive their clicks.

### M5 — Decorations: cities, improvements, overlays, labels
* `voxel_props.c`: `addPitchedBuilding`, `addVoxelTree`, `addPineTree`,
  `addSheep`, `addCow` + the 23 improvement models from
  `buildImprovementsOverlay()`.
* City models: the page's Metropolis generator (skyscrapers, park, vehicles)
  scaled by city size, player-coloured banners; ancient/medieval variants for
  early eras (era → generator).
* Border ribbons, goto routes, highlights, crosshair, "war fog" darkening.
* `vk3d_text.c` font atlas for city names and unit stack counts.
* **Accept:** a developed map shows roads, mines, farms, cities and borders;
  fog-of-war states are visually distinct; city names readable at zoom 3–8.

### M6 — Units
* `voxel_unit_art.c`: the 51 models, grouped as on the page; player colour
  tint on standard parts; health/flagsprites stay in the 2D layer for now.
* Instancing, movement interpolation, naval bobbing, air units on a
  height offset with shadow on the ground.
* **Accept:** stacks of 6+ units render correctly and animate; a moving unit
  glides along its route; naval/air units read as such.

### M7 — Lighting quality + animation
* Shadow map (2 cascades) from the page's sun vector, PCF 4×4; toggleable.
* Animated water wheels, rotors, crop sway, smoke for pollution — driven by the
  `time` push constant, no CPU work.
* Quality presets (Low = no shadows/no water animation, Medium, High = 4× SSAA
  resolve) exposed as options.
* **Accept:** 60 fps at 1080p on an Intel iGPU (Low) and a discrete GPU (High);
  no animation stutter when the map is static.

### M8 — Feature parity sweep
* Minimap/overview in 3D-friendly form (keep the 2D overview window — it works).
* City-outliner/fogmap/trade report windows unchanged; verify `editor.c`
  (map editor) works through the 3D hooks; screenshot key; `mapimg` export
  (keep using the 2D renderer path internally).
* Tileset/theme interaction: 3D art is tileset-independent, but the theme still
  supplies fonts and UI colours; document the `data/themes/gui-vulkan/` drop-in.
* **Accept:** the full §9 checklist.

### M9 — Polish, packaging, upstreaming
* Install rules for `freeciv-vulkan` (desktop file, metainfo, man page),
  Windows/macOS notes (Vulkan SDK / MoltenVK), `--renderer` diagnostics
  overlay (draw calls, chunk rebuilds, VRAM), crash-safe device lost recovery.
* Performance pass with `RenderDoc`/`nsight`, then a clean, reviewable
  patch series for upstream freeciv.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Two swapchains on one window (SDL renderer vs Vulkan) | Vulkan owns the window; SDL renderer only created when Vulkan init failed |
| No Vulkan on the user's box | Automatic fallback to the SDL3 2D path at window creation |
| Per-frame 4 MB GUI texture upload | Dirty-rect uploads (M4); the GUI only repaints on events |
| Chunk rebuild storms (fog, unit moves) | Separate colour/visibility VBO; decoration layer instanced separately |
| Ruleset variety (custom terrains/extras) | Name + flag mapping table with a documented fallback (`meadow`/`ocean`) and a `--debug-art` dump |
| Shader toolchain missing at build time | `glslc` is a hard requirement of the `vulkan` client, reported at configure time; SPIR-V is embedded, so no runtime dependency |
| This fork's `tiledef` API churn | All ruleset access is funnelled through `voxel_art.c` / `vk3d_map.c`, one place to fix |
| Validation layers off ⇒ silent UB | Enable `VK_LAYER_KHRONOS_validation` when `--debug >= 2`; run the M2+ acceptance with it on |

---

## 9. Acceptance checklist ("fully featured and usable")

* [ ] New game / load game / multi-player game connect and play.
* [ ] Whole map visible, pannable, zoomable, rotatable; 60 fps on a 128² map.
* [ ] Click-select, drag-select, right-click orders, keyboard shortcuts.
* [ ] Terrain: all 13 classic terrains + city ground, blended seams, water.
* [ ] Improvements: road, rail, maglev, river, canal, irrigation, mine, oil
      well/platform, fort/fortress/castle/bunker, airbase/airstrip, buoy,
      village/hut, ruins, pollution, fallout.
* [ ] Cities: size-appropriate models, banners, names, citizen/production bars
      (2D layer), 20-size growth visible.
* [ ] Units: all 51 classic models + graceful generic fallback for custom
      unit types; movement animation; stacks; health bars.
* [ ] Fog of war (3 states), borders, goto routes, highlights, crosshair,
      selected-unit ring, "units left to play" arrow.
* [ ] Every dialog, report, chatline, menu, city dialog, editor and the
      overview window still work.
* [ ] Options persist; fullscreen/windowed; resolution change; DPI scaling.
* [ ] Runs with validation layers clean; recovers from device lost.
* [ ] Falls back to 2D rendering when Vulkan is unavailable, without losing
      functionality.

---

## 10. Build & run

```sh
meson setup build -Dclients=vulkan -Daudio=sdl3
ninja -C build freeciv-vulkan
./build/freeciv-vulkan
```

Requirements: SDL3 (≥3.2), SDL3_ttf, Vulkan loader + ICD, glslc (glslang),
libpng **or** SDL3_image (auto-detected; `img_png.c` is the fallback and
currently supports PNG only — enough for tilesets and themes).

Debug: `VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation`,
`--debug 2`, and (planned M7) `--renderer-stats`.

---

## 11. Immediate next tasks (start of M2)

1. `vk3d_mesh.c`: `struct voxel_mesh` + `vm_add_box()` mirroring
   `VoxelBuilder.addBox()` (same 6-face order, same winding), face culling,
   `vm_finish()` → interleaved `pos3/normal3/colour3` + u32 indices.
2. `voxel_gen.c`: `voxel_gen_ground(art_id, neighbours[4], seed, out_cells[20][20])`
   — port the ground loop of each generator (forest `mulberry32(101)`,
   meadow `202`, jungle `3030`, …) and call `voxel_blend_cell()` per cell.
3. `vk3d_map.c`: chunk table (`8×8` tiles), dirty bits, rebuild queue with a
   per-frame triangle budget (≈200 k) so a big map load never hitches.
4. `vk3d_shaders.c`: scene pipeline with depth attachment; extend the render
   pass with a `D24_UNORM_S8_UINT` depth buffer; view/projection matrices
   (right-handed, `y` up, Vulkan NDC `z∈[0,1]`, `y` flipped).
5. Replace the `vk3d_map_record()` no-op with the chunk draw loop; hook
   `refresh_tile_mapcanvas()` in `mapview.c`.
6. Debug HUD: chunk count, triangle count, rebuilds/frame (drawn via the
   existing 2D layer into the map viewport corner).
