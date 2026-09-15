/***********************************************************************
 Freeciv - gui-irrlicht Phase 5: 3D map view.
   Builds a merged terrain SMesh from the client's real map (wld.map),
   renders it with a fixed 3/4 camera, and picks tiles via an analytical
   ray->y=0-plane intersection. Terrain->demo-.obj mapping is by the
   terrain's untranslated name; fog-of-war (TILE_UNKNOWN) tiles are omitted.
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>

#include <irrlicht.h>

#include "graphics.h"
#include "irrg_map3d.h"

/* Unit billboard support (Phase 5b): turn each of the player's units into a
 * camera-facing billboard using its tileset sprite, and draw a glowing, animated
 * ring around the focused (selected) unit. */
#include <map>
#include <set>
#include "tilespec.h"      /* extern struct tileset *tileset; get_unittype_sprite */
#include "unittype.h"      /* unit_type_get, const struct unit_type */
#include "unitlist.h"      /* unit_list_iterate */
#include "unit.h"          /* struct unit, unit_tile(), unit_owner(), ->id */
#include "control.h"       /* head_of_units_in_focus() */
#include "actions.h"       /* ACTION_FOUND_CITY (test diagnostic) */

/* FreeCiv data access (all C, guarded by extern "C" in the headers). */
#include "world_object.h"      /* extern struct world wld (wld.map) */
#include "map.h"               /* map_pos_to_tile, index_to_map_pos_x/y */
#include "tile.h"              /* struct tile, tile_terrain, tile_index */
#include "terrain.h"           /* struct terrain, terrain_type_terrain_class */
#include "name_translation.h"  /* untranslated_name */
#include "climap.h"            /* client_tile_get_known, enum known_type */
#include "client_main.h"       /* client_player() */
#include "city.h"              /* city_list_iterate, city_tile */
#include "player.h"            /* struct player */
#include "style.h"             /* style_of_city() */
#include "game.h"              /* game.control.num_city_styles */
#include "extras.h"            /* struct extra_type (tile resource), extra_index */

using namespace irr;

namespace {

irr::IrrlichtDevice    *dev    = 0;
scene::ISceneManager   *smgr   = 0;
video::IVideoDriver    *vdrv   = 0;
scene::ICameraSceneNode *camera = 0;
scene::ISceneNode      *map_parent = 0;
scene::IMeshSceneNode  *map_node   = 0;
video::ITexture        *map_tex    = 0;   /* held for the map's life */
core::vector3df cam_pos, cam_target;

/* Camera framing, auto-sized to the explored area. A steep (near-top-down)
 * angle makes the explored tiles read as a clean grid block centred on the
 * units rather than a slanted, stretched strip (the "wrong tiles / wrong
 * shape" the user reported). g_cam_panned detaches the auto-framing once the
 * user pans/zooms so we stop fighting their view. */
static float g_cam_height = 11.0f;
static float g_cam_depth  = 4.5f;
static int   g_exp_min_x = 0, g_exp_max_x = 0, g_exp_min_y = 0, g_exp_max_y = 0;
static int   g_cam_cx = 0, g_cam_cy = 0;   /* current camera centre (map coords) */
static bool  g_cam_panned = false;
static int   g_last_focus_unit = -1;       /* last focused unit (for re-centring) */
int mw = 0, mh = 0;
bool built = false;
std::string art_root;
int last_explored = -1;   /* explored tiles present at the last terrain-mesh build */

/* World-space x for a map tile x. The down-looking camera has a fixed
 * (right-handed) chirality, so world +x lands on screen-LEFT and world +y on
 * screen-DOWN -- i.e. "east" is mirror-left-right vs the 2D map. Flipping the
 * tile's x about the map's x-axis makes east render to screen-right, matching
 * the 2D orientation. It is an involution (fx(fx(x))==x), so the exact same
 * expression inverts the screen->tile pick. It MUST be applied to every
 * tile->world placement below AND to the world->tile pick, or the picking and
 * the render will disagree (units would appear to land on the wrong tile). */
static inline int fx(int tx) { return (mw > 0) ? (mw - 1 - tx) : tx; }

std::string tolower_str(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}

/* Map a FreeCiv terrain to a demo .obj path (relative to <art>/terrain/).
 * Uses the base (non-directional) variant for every type; the directional
 * neighbor logic (tile_dir/coast_dir) is a later refinement. */
std::string terrain_obj(const struct terrain *t)
{
  std::string n = tolower_str(untranslated_name(&t->name));
  if (n.find("grass")    != std::string::npos) return "grass/grass.obj";
  if (n.find("desert")   != std::string::npos) return "desert/desert.obj";
  if (n.find("tundra")   != std::string::npos) return "tundra/tundra.obj";
  if (n.find("taiga")    != std::string::npos) return "forest/forest.obj";
  if (n.find("plains")   != std::string::npos) return "plains/plains.obj";
  if (n.find("hill")     != std::string::npos) return "hill/hill.obj";
  if (n.find("mountain") != std::string::npos) return "mountain/mountain.obj";
  if (n.find("volcanic") != std::string::npos) return "mountain/mountain.obj";
  if (n.find("swamp")    != std::string::npos) return "swamp/swamp.obj";
  if (n.find("glacier")  != std::string::npos) return "ice/ice.obj";    /* classic glacier -> ice art */
  if (n.find("ice")      != std::string::npos) return "ice/ice.obj";    /* icecap / ice field */
  if (n.find("jungle")   != std::string::npos) return "forest/forest.obj";
  if (n.find("forest")   != std::string::npos) return "forest/forest.obj";
  if (n.find("coast")    != std::string::npos) return "coast/coast.obj";
  if (n.find("lake")     != std::string::npos) return "water/water.obj";    /* lakes -> water art */
  if (n.find("river")    != std::string::npos) return "water/water.obj";
  if (n.find("deep")     != std::string::npos) return "ocean/ocean.obj";   /* deep sea (before "ocean") */
  if (n.find("ocean")    != std::string::npos) return "coast/coast.obj";   /* near-land sea -> shoreline art */
  if (n.find("water")    != std::string::npos) return "water/water.obj";
  if (n.find("city")     != std::string::npos) return "grass/grass.obj";
  if (terrain_type_terrain_class(t) == TC_OCEAN) return "ocean/ocean.obj";
  return "grass/grass.obj";
}

/* Oriented (directional) terrain variants.
 *
 * coast / desert / ice / plains each ship 16 flat-quad .obj files
 * (name0.obj .. name15.obj) whose geometry is identical and differ only in UV:
 * they are 16 slices of a coastal/biome-edge texture atlas, one per combination
 * of the four cardinal "edge" directions. The slice is chosen to match the demo
 * (Main.cpp coast_dir): for each of the four cardinal neighbours that is a
 * *different terrain* than this tile, a flag is set and the slice is looked up
 * in the demo's table (see terrain_orient_index). So a coast tile "faces" its
 * water on the flagged sides, and a biome edge lights up toward the neighbour.
 *
 * The feature is toggleable via FC_IRR_ORIENT (unset or "1" = on, "0" = off,
 * which falls back to the generic base mesh). Because the geometry is a flat
 * quad, a "wrong" slice can only ever show a different-but-valid edge, never a
 * broken shape. */
static bool terrain_has_oriented(const struct terrain *t)
{
  // A terrain uses the 16-slice directional variants iff its base .obj's art dir
  // ships them. Exactly 7 dirs do (see the art/terrain layout -- each holds
  // <stem>.obj + <stem>0..15.obj):  coast desert ice plains swamp tundra water.
  // Match by the art dir (the base .obj path from terrain_obj) rather than the
  // FreeCiv terrain name, so it stays correct even when several terrain names
  // map to the same art (e.g. glacier+ice->ice, river+lake->water, ocean->coast).
  static const char *oriented_dirs[] = {
    "coast", "desert", "ice", "plains", "swamp", "tundra", "water"
  };
  std::string base = terrain_obj(t);              // "<dir>/<dir>.obj"
  size_t slash = base.rfind('/');
  std::string dir = (slash != std::string::npos) ? base.substr(0, slash) : "";
  for (const char *d : oriented_dirs)
    if (dir == d) return true;
  return false;
}

/* Oriented (directional) variant index for a tile. This replicates Main.cpp's
 * coast_dir() EXACTLY. The .obj variant files (coast0.obj .. coast15.obj, and
 * likewise desert/plains/ice) are named by a SPECIFIC permutation of the four
 * cardinal "edge" flags -- NOT a plain bit mask. The demo scans the four
 * cardinal neighbours and, for each one that is a *different terrain*, sets a
 * flag, then picks the variant index from this table (last matching combination
 * wins):
 *
 *   none=1   N=2   E=9   S=5   W=3
 *   N+E=10   N+S=6   N+W=4   E+S=13   E+W=11   S+W=7
 *   N+E+S=14   N+E+W=12   N+S+W=8   S+E+W=15   all=0
 *
 * (A plain 4-bit mask would e.g. give North-only = 1, but the assets use 2 --
 * that mismatch is why the tiles were showing the wrong slice.)
 *
 * The 3D map is x-mirrored (see fx(): world +x is screen-left), so the demo's
 * east/west slices would land on the wrong side of the tile. We swap the east
 * and west flags before applying the table, so the biome edge faces the actual
 * (mirrored) water side. North/south are unaffected by the mirror. */
static int terrain_orient_index(struct tile *ptile)
{
  bool dn = false, de = false, ds = false, dw = false;
  const struct terrain *self = tile_terrain(ptile);
  struct tile *nb;
  enum direction8 d;
  cardinal_adjc_dir_iterate(&wld.map, ptile, nb, d) {
    if (tile_terrain(nb) != self) {
      switch (d) {
      case DIR8_NORTH: dn = true; break;
      case DIR8_EAST:  de = true; break;
      case DIR8_SOUTH: ds = true; break;
      case DIR8_WEST:  dw = true; break;
      default: break;
      }
    }
  } cardinal_adjc_dir_iterate_end;

  /* x-mirror (fx): swap east/west so the edge faces the water on screen. */
  bool N = dn, E = dw, S = ds, W = de;

  /* Main.cpp coast_dir() table (last matching combination wins). */
  int idx = 1;
  if (N) idx = 2;
  if (E) idx = 9;
  if (S) idx = 5;
  if (W) idx = 3;
  if (N && E) idx = 10;
  if (N && S) idx = 6;
  if (N && W) idx = 4;
  if (E && S) idx = 13;
  if (E && W) idx = 11;
  if (S && W) idx = 7;
  if (N && E && S) idx = 14;
  if (N && E && W) idx = 12;
  if (N && S && W) idx = 8;
  if (S && E && W) idx = 15;
  if (N && E && S && W) idx = 0;
  return idx;
}

/* Per-tile .obj path: the oriented variant when available + enabled, else the
 * generic base mesh. Returns the generic path if the base has no "/stem.obj" we
 * can splice a digit into. */
std::string terrain_obj_for_tile(struct tile *ptile, const struct terrain *t)
{
  static const bool orient_on = [] {
    const char *e = std::getenv("FC_IRR_ORIENT");
    return !e || e[0] != '0';   /* default on */
  }();
  if (!orient_on || !terrain_has_oriented(t)) return terrain_obj(t);
  std::string base = terrain_obj(t);
  size_t slash = base.rfind('/');
  if (slash == std::string::npos) return base;
  std::string file = base.substr(slash + 1);          /* "coast.obj"  */
  if (file.size() < 5 || file.compare(file.size() - 4, 4, ".obj") != 0)
    return base;
  std::string stem = file.substr(0, file.size() - 4); /* "coast"      */
  return base.substr(0, slash + 1) + stem +
         std::to_string(terrain_orient_index(ptile)) + ".obj";
}

/* Size the camera to the explored bounding box so the explored area fills a
 * compact, centred region (with margin) -- a "block", not a strip running off
 * the screen. Steeper (larger height, small depth) = more top-down = a cleaner
 * block. FC_IRR_CAMH / FC_IRR_CAMD override the auto values. */
static void compute_cam_from_bbox(void)
{
  int spanx = g_exp_max_x - g_exp_min_x + 1;
  int spany = g_exp_max_y - g_exp_min_y + 1;
  int span  = spanx > spany ? spanx : spany;
  if (span < 4) span = 4;
  g_cam_height = 3.0f + (float)span * 0.95f;  /* frame the explored area snugly */
  g_cam_depth  = g_cam_height * 0.26f;      /* ~75 deg down: near-top-down */
  const char *eh = std::getenv("FC_IRR_CAMH");
  if (eh) g_cam_height = (float)atof(eh);
  const char *ed = std::getenv("FC_IRR_CAMD");
  if (ed) g_cam_depth  = (float)atof(ed);
}

/* Centre the camera on map tile (cx,cy) at the current auto/override height. */
void place_camera(int cx, int cy)
{
  g_cam_cx = cx;  g_cam_cy = cy;
  cam_target = core::vector3df((f32)fx(cx), 0.0f, (f32)cy);
  cam_pos    = core::vector3df((f32)fx(cx), g_cam_height, (f32)cy + g_cam_depth);
  if (camera) {
    camera->setPosition(cam_pos);
    camera->setTarget(cam_target);
  }
}

/* ---- Unit billboards + selection ring (Phase 5b) ------------------------ */
struct UBB { scene::ISceneNode *node; };
std::map<int, UBB>        unit_bb;        /* unit id -> billboard node */
struct UTex { video::ITexture *tex; int w, h; };
std::map<const void *, UTex> unit_tex;    /* unit_type* -> sprite texture */
scene::ISceneNode *unit_parent = 0;
video::ITexture   *ring_tex    = 0;
scene::ISceneNode *ring_node   = 0;
float              ring_anim_t = 0.0f;

/* City + resource billboards (Phase 5c). Cities key by city id; resources key
 * by tile index (a tile holds at most one resource). Textures cache per city
 * style and per resource type. Reuse the unit billboard parent node. */
std::map<int, UBB>   city_bb;    /* city id -> billboard node */
std::map<int, UTex>  city_tex;   /* city style -> sprite texture */
std::map<int, UBB>   res_bb;     /* tile index -> resource billboard node */
std::map<int, UTex>  res_tex;    /* resource extra_index -> sprite texture */

/* Get (or lazily create + cache) an Irrlicht texture for a unit type's icon
 * sprite. FreeCiv sprites are CPU SColor* (A8R8G8B8), top-down, so we copy the
 * pixels 1:1 into a new texture. Returns 0 if the sprite is unavailable. */
video::ITexture *unit_type_texture(const struct unit_type *ut, int *pw, int *ph)
{
  int tw = 0, th = 0;
  std::map<const void *, UTex>::iterator it = unit_tex.find((const void *)ut);
  if (it != unit_tex.end()) { *pw = it->second.w; *ph = it->second.h; return it->second.tex; }
  struct sprite *uspr = get_unittype_sprite(tileset, ut, ACTIVITY_LAST, direction8_invalid());
  if (!uspr || !uspr->pixels || uspr->width <= 0 || uspr->height <= 0) { *pw = *ph = 0; return 0; }
  int w = uspr->width, h = uspr->height;
  core::dimension2d<u32> dim((u32)w, (u32)h);
  video::IImage *img = vdrv->createImage(video::ECF_A8R8G8B8, dim);
  if (!img) { *pw = *ph = 0; return 0; }
  video::SColor *dst = (video::SColor *)img->lock();
  const video::SColor *src = (const video::SColor *)uspr->pixels;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      dst[(size_t)y * w + x] = src[(size_t)y * w + x];   /* top-down, 1:1 */
  img->unlock();
  /* addTexture caches by name, so each texture needs a unique name. */
  static int tex_counter = 0;
  std::string tn = "irrg_unittex_" + std::to_string(tex_counter++);
  video::ITexture *tex = vdrv->addTexture(tn.c_str(), img);
  img->drop();
  UTex u; u.tex = tex; u.w = w; u.h = h;
  unit_tex[(const void *)ut] = u;
  *pw = w; *ph = h;
  return tex;
}

/* Convert a FreeCiv CPU sprite (top-down SColor* A8R8G8B8) into a fresh
 * Irrlicht texture. The texture is cached by the video driver under a unique
 * name (cache-owned: destroy via removeTexture, never drop). Returns 0 if the
 * sprite has no pixels. (Generalises the pixel copy in unit_type_texture. */
static video::ITexture *sprite_to_texture(struct sprite *spr, int *pw, int *ph)
{
  if (!spr || !spr->pixels || spr->width <= 0 || spr->height <= 0) {
    *pw = *ph = 0; return 0;
  }
  int w = spr->width, h = spr->height;
  core::dimension2d<u32> dim((u32)w, (u32)h);
  video::IImage *img = vdrv->createImage(video::ECF_A8R8G8B8, dim);
  if (!img) { *pw = *ph = 0; return 0; }
  video::SColor *dst = (video::SColor *)img->lock();
  const video::SColor *src = (const video::SColor *)spr->pixels;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      dst[(size_t)y * w + x] = src[(size_t)y * w + x];   /* top-down, 1:1 */
  img->unlock();
  static int stex_counter = 0;
  std::string tn = "irrg_sprite_" + std::to_string(stex_counter++);
  video::ITexture *tex = vdrv->addTexture(tn.c_str(), img);
  img->drop();
  *pw = w; *ph = h;
  return tex;
}

/* Get (or lazily create + cache) the Irrlicht texture for a city-building
 * sprite of the given style (all cities of a style share one texture). */
static video::ITexture *city_style_texture(int style, int *pw, int *ph)
{
  std::map<int, UTex>::iterator it = city_tex.find(style);
  if (it != city_tex.end()) { *pw = it->second.w; *ph = it->second.h; return it->second.tex; }
  struct sprite *spr = get_sample_city_sprite(tileset, style);
  video::ITexture *tex = sprite_to_texture(spr, pw, ph);
  if (!tex || *pw <= 0) { *pw = *ph = 0; return 0; }
  UTex u; u.tex = tex; u.w = *pw; u.h = *ph;
  city_tex[style] = u;
  return tex;
}

/* Get (or lazily create + cache) the Irrlicht texture for a tile resource
 * (bonus/ore) sprite. */
static video::ITexture *resource_texture(const struct extra_type *pextra,
                                         int *pw, int *ph)
{
  if (!pextra) { *pw = *ph = 0; return 0; }
  int id = extra_index(pextra);
  std::map<int, UTex>::iterator it = res_tex.find(id);
  if (it != res_tex.end()) { *pw = it->second.w; *ph = it->second.h; return it->second.tex; }
  struct sprite *spr = get_tile_resource_sprite(tileset, pextra);
  video::ITexture *tex = sprite_to_texture(spr, pw, ph);
  if (!tex || *pw <= 0) { *pw = *ph = 0; return 0; }
  UTex u; u.tex = tex; u.w = *pw; u.h = *ph;
  res_tex[id] = u;
  return tex;
}

/* A soft, glowing cyan ring on a transparent disc (used for the selection
 * highlight). Generated once; animated per-frame by scaling the billboard. */
video::ITexture *make_ring_texture(void)
{
  const int S = 128;
  core::dimension2d<u32> dim((u32)S, (u32)S);
  video::IImage *img = vdrv->createImage(video::ECF_A8R8G8B8, dim);
  if (!img) return 0;
  video::SColor *p = (video::SColor *)img->lock();
  const float cx = S * 0.5f, cy = S * 0.5f, ringR = S * 0.38f, ringW = S * 0.17f;
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      float d = std::sqrt(dx * dx + dy * dy);
      float t = 1.0f - std::fabs(d - ringR) / ringW;      /* 1 at ring centre */
      if (t < 0.0f) t = 0.0f;
      unsigned char a = (unsigned char)(255.0f * t);      /* bright, wide glow */
      p[(size_t)y * S + x] = video::SColor(a, 60, 235, 255); /* cyan */
    }
  img->unlock();
  video::ITexture *tex = vdrv->addTexture("irrg_ring", img);
  img->drop();
  return tex;
}

/* Build a merged SMesh of every currently-explored tile (TILE_UNKNOWN and
 * no-terrain tiles are skipped). The caller owns the returned mesh (drop it).
 * Returns NULL if nothing was rendered. Out-params carry counters so both the
 * initial build (which also centers the camera on the centroid) and the
 * incremental refresh (irrg_map3d_refresh_tiles) can log what changed. */
static scene::IMesh *build_terrain_mesh(int *p_explored, int *p_rendered,
                                        int *p_fogged, int *p_missing,
                                        int *p_oriented, unsigned int *p_orients,
                                        long *p_sum_x, long *p_sum_y)
{
  scene::IMeshManipulator *manip = smgr->getMeshManipulator();
  scene::SMesh *mesh = new scene::SMesh();
  mesh->setHardwareMappingHint(scene::EHM_STATIC);
  scene::SMeshBuffer *buf = new scene::SMeshBuffer();
  int rendered = 0, fogged = 0, missing = 0, oriented = 0, explored = 0;
  unsigned int orient_bits = 0;
  long sum_x = 0, sum_y = 0;
  bool first_box = true;
  for (int y = 0; y < mh; ++y) {
    for (int x = 0; x < mw; ++x) {
      struct tile *ptile = map_pos_to_tile(&wld.map, x, y);
      if (!ptile) continue;
      if (client_tile_get_known(ptile) == TILE_UNKNOWN) { ++fogged; continue; }
      const struct terrain *terr = tile_terrain(ptile);
      if (!terr) { ++fogged; continue; }
      ++explored;
      std::string tpath = terrain_obj_for_tile(ptile, terr);
      std::string path = art_root + "/terrain/" + tpath;
      scene::IAnimatedMesh *am = smgr->getMesh(path.c_str());
      bool used_oriented = (am != 0) && (tpath != terrain_obj(terr));
      if (!am) {
        /* oriented variant file missing -> fall back to the generic base mesh
         * so the tile is never left as a hole. */
        std::string gpath = art_root + "/terrain/" + terrain_obj(terr);
        am = smgr->getMesh(gpath.c_str());
      }
      if (!am) { ++missing; continue; }
      if (used_oriented) { ++oriented; orient_bits |= (1u << terrain_orient_index(ptile)); }
      scene::IMesh *copy = manip->createMeshCopy(am->getMesh(0));
      if (!copy) { ++missing; continue; }
      core::CMatrix4<f32> tf;
      tf.setTranslation(core::vector3df((f32)fx(x), 0.0f, (f32)y));
      tf.setScale(core::vector3df(0.5f, 0.5f, 0.5f));
      manip->transform(copy, tf);
      scene::IMeshBuffer *cb = copy->getMeshBuffer(0);
      if (cb && cb->getVertexCount() > 0) {
        buf->append(cb->getVertices(), cb->getVertexCount(),
                    cb->getIndices(), cb->getIndexCount());
        ++rendered;
        sum_x += x;
        sum_y += y;
        if (first_box) {
          g_exp_min_x = g_exp_max_x = x; g_exp_min_y = g_exp_max_y = y;
          first_box = false;
        } else {
          if (x < g_exp_min_x) g_exp_min_x = x;
          if (x > g_exp_max_x) g_exp_max_x = x;
          if (y < g_exp_min_y) g_exp_min_y = y;
          if (y > g_exp_max_y) g_exp_max_y = y;
        }
      }
      copy->drop();
    }
  }
  if (p_explored) *p_explored = explored;
  if (p_rendered) *p_rendered = rendered;
  if (p_fogged)   *p_fogged   = fogged;
  if (p_missing)  *p_missing  = missing;
  if (p_oriented) *p_oriented = oriented;
  if (p_orients)  *p_orients  = orient_bits;
  if (p_sum_x)    *p_sum_x    = sum_x;
  if (p_sum_y)    *p_sum_y    = sum_y;
  if (rendered == 0) { buf->drop(); mesh->drop(); return 0; }
  mesh->addMeshBuffer(buf);   /* takes ownership of buf */
  if (std::getenv("FC_IRR_DIAG")) {
    f32 mnx=1e9f,mxx=-1e9f,mnz=1e9f,mxz=-1e9f;
    const video::S3DVertex *verts = (const video::S3DVertex *)buf->getVertices();
    for (int vi=0; vi<buf->getVertexCount(); ++vi) {
      if (verts[vi].Pos.X<mnx) mnx=verts[vi].Pos.X; if (verts[vi].Pos.X>mxx) mxx=verts[vi].Pos.X;
      if (verts[vi].Pos.Z<mnz) mnz=verts[vi].Pos.Z; if (verts[vi].Pos.Z>mxz) mxz=verts[vi].Pos.Z;
    }
    std::fprintf(stderr,
      "[irrg] DIAG terrain bbox x[%g..%g] z[%g..%g] (rendered=%d map=%dx%d, expect x[-0.5..%g] z[-0.5..%g])\n",
      mnx,mxx,mnz,mxz, rendered, mw, mh, mw-0.5, mh-0.5);
    std::fflush(stderr);
  }
  return mesh;
}

/* The unrounded world-space point where the ray from screen pixel (mx,my)
 * meets the y=0 ground plane. Returns false if the ray is parallel to the
 * ground or points away from it. Shared by tile picking and the grab-pan. */
static bool ground_hit(int mx, int my, core::vector3df *hit)
{
  if (!built || !smgr || !camera) return false;
  scene::ISceneCollisionManager *coll = smgr->getSceneCollisionManager();
  core::line3d<f32> ray = coll->getRayFromScreenCoordinates(
      core::position2d<s32>(mx, my), camera);
  core::vector3df dir = ray.end - ray.start;
  if (std::fabs(dir.Y) < 1e-6f) return false;
  f32 t = -ray.start.Y / dir.Y;
  if (t < 0.0f) return false;
  *hit = ray.start + dir * t;
  return true;
}

} // namespace

/* World-space (unrounded) ground point under the screen pixel, for the pan. */
int irrg_map3d_pick_world(int mx, int my, float *ox, float *oy)
{
  core::vector3df h;
  if (!ground_hit(mx, my, &h)) return 0;
  *ox = h.X; *oy = h.Z;
  return 1;
}

/* Rebuild the terrain mesh to include every tile explored so far, WITHOUT
 * touching the camera or the unit/city/resource billboards. Called each frame
 * from the render loop; it only actually rebuilds when the explored-tile set
 * has changed (a unit revealing new tiles), so newly-explored terrain appears
 * immediately. Returns 1 if it rebuilt, 0 otherwise. */
/* Camera auto-follow / auto-refit is OFF by default: the camera is kept STABLE
 * (it only moves when the user explicitly pans, zooms, right-clicks empty
 * space, or clicks the selected-unit dialog). The old per-frame "re-centre on
 * every unit selection" + "re-zoom on every reveal" made the view jump around
 * as you selected/moved units, so aiming a move landed on the wrong tile and
 * the map felt unstable (a regression vs the pre-Phase-5e stable camera).
 * FC_IRR_CAMFOLLOW=1 re-enables the follow + auto-refit. */
static bool camfollow_on(void)
{
  static int v = -1;
  if (v < 0) { const char *e = std::getenv("FC_IRR_CAMFOLLOW"); v = (e && e[0] == '1') ? 1 : 0; }
  return v == 1;
}

int irrg_map3d_refresh_tiles(void)
{
  if (!built || !smgr) return 0;

  /* (opt-in, FC_IRR_CAMFOLLOW=1) Centre the view on the focused unit whenever
   * the selection changes to a unit. OFF by default so the camera stays put. */
  if (camfollow_on()) {
    struct unit *fu = head_of_units_in_focus();
    int key = fu ? (int)(uintptr_t)fu : -1;
    if (fu && key != g_last_focus_unit) {
      g_last_focus_unit = key;
      irrg_map3d_focus_unit(index_to_map_pos_x(tile_index(fu->tile)),
                            index_to_map_pos_y(tile_index(fu->tile)));
    }
  }

  int explored = 0;
  for (int y = 0; y < mh; ++y)
    for (int x = 0; x < mw; ++x) {
      struct tile *pt = map_pos_to_tile(&wld.map, x, y);
      if (pt && client_tile_get_known(pt) != TILE_UNKNOWN && tile_terrain(pt))
        ++explored;
    }
  if (explored == last_explored) return 0;   /* nothing new revealed */
  last_explored = explored;
  int be = 0, rendered = 0, fogged = 0, missing = 0, oriented = 0;
  unsigned int orients = 0;
  scene::IMesh *mesh = build_terrain_mesh(&be, &rendered, &fogged, &missing,
                                          &oriented, &orients, 0, 0);
  if (!mesh) return 0;
  scene::IMeshSceneNode *nnode = smgr->addMeshSceneNode(mesh);
  mesh->drop();               /* the new node owns the mesh now */
  if (map_tex) nnode->setMaterialTexture(0, map_tex);
  nnode->setMaterialFlag(video::EMF_LIGHTING, false);
  if (map_parent) nnode->setParent(map_parent);
  scene::IMeshSceneNode *old = map_node;
  map_node = nnode;           /* swap in the fresh mesh before removing the old */
  if (old) old->remove();
  std::fprintf(stderr,
    "[irrg] map3d: refresh -> explored=%d rendered=%d fog=%d missing=%d\n",
    explored, rendered, fogged, missing);
  /* (opt-in, FC_IRR_CAMFOLLOW=1) refit the zoom to the newly-explored area.
   * OFF by default so the framing stays fixed as tiles are revealed. */
  if (camfollow_on() && !g_cam_panned) {
    compute_cam_from_bbox();
    place_camera(g_cam_cx, g_cam_cy);
  }
  return 1;
}

int irrg_map3d_build(void)
{
  if (built) return 1;
  dev  = (irr::IrrlichtDevice *)irrg_device();
  smgr = (scene::ISceneManager *)irrg_scmgr();
  vdrv = (video::IVideoDriver *)irrg_vdriver();
  if (!dev || !smgr || !vdrv) return 0;

  mw = wld.map.xsize;
  mh = wld.map.ysize;
  if (mw <= 0 || mh <= 0) return 0;

  const char *art = std::getenv("FC_IRR_ART");
  if (art && *art) {
    art_root = art;
  } else {
    const char *home = std::getenv("HOME");
    art_root = std::string(home ? home : ".") + "/src/demo-run/art";
  }

  int rendered = 0, fogged = 0, missing = 0, oriented = 0, explored = 0;
  unsigned int orient_bits = 0;
  long sum_x = 0, sum_y = 0;
  scene::IMesh *mesh = build_terrain_mesh(&explored, &rendered, &fogged,
                                          &missing, &oriented, &orient_bits,
                                          &sum_x, &sum_y);
  if (!mesh) {
    std::fprintf(stderr,
      "[irrg] map3d: no tiles rendered (fog=%d missing=%d map=%dx%d)\n",
      fogged, missing, mw, mh);
    return 0;
  }
  last_explored = explored;
  if (std::getenv("FC_IRR_EXPLORE")) {
    std::fprintf(stderr, "[irrg] EXPLORED GRID (%dx%d, x=explored/terrain, .=fog):\n", mw, mh);
    for (int y = 0; y < mh; ++y) {
      char row[256] = {0}; int n = 0;
      for (int x = 0; x < mw && n < 255; ++x) {
        struct tile *pt = map_pos_to_tile(&wld.map, x, y);
        row[n++] = (pt && client_tile_get_known(pt) != TILE_UNKNOWN &&
                    tile_terrain(pt)) ? 'x' : '.';
      }
      row[n] = 0;
      std::fprintf(stderr, "  y%02d %s\n", y, row);
    }
  }
  map_parent = smgr->addEmptySceneNode();
  map_node = smgr->addMeshSceneNode(mesh);
  mesh->drop();               /* the scene node owns the mesh now */
  map_node->setParent(map_parent);
  std::string texpath = art_root + "/terrain/tex.png";
  map_tex = vdrv->getTexture(texpath.c_str());
  std::fprintf(stderr, "[irrg] map3d: texture %s -> %p size=%ux%u\n",
               texpath.c_str(), (void *)map_tex,
               map_tex ? map_tex->getSize().Width : 0,
               map_tex ? map_tex->getSize().Height : 0);
  if (map_tex)
    map_node->setMaterialTexture(0, map_tex);
  map_node->setMaterialFlag(video::EMF_LIGHTING, false);

  /* Center the camera on the explored tiles' centroid (robust: doesn't depend
   * on the local player's city being loaded yet). */
  int cx = (int)(sum_x / rendered);
  int cy = (int)(sum_y / rendered);
  compute_cam_from_bbox();
  place_camera(cx, cy);
  camera = smgr->addCameraSceneNode(0, cam_pos, cam_target);

  built = true;
  std::fprintf(stderr,
    "[irrg] map3d: built %dx%d rendered=%d fog=%d missing=%d oriented=%d variants=0x%03x center=(%d,%d)\n",
    mw, mh, rendered, fogged, missing, oriented, orient_bits, cx, cy);
  return 1;
}

int irrg_map3d_is_built(void) { return built; }

/* Diagnostic: does the client hold terrain data for never-seen (TILE_UNKNOWN)
 * tiles? (Determines whether a client-side "reveal" can show them.) */
void irrg_diag_map(void)
{
  int total = 0, unk = 0, unk_terr = 0, unse = 0, vis = 0;
  if (wld.map.xsize > 0 && wld.map.ysize > 0) {
    for (int x = 0; x < (int)wld.map.xsize; ++x)
      for (int y = 0; y < (int)wld.map.ysize; ++y) {
        struct tile *t = map_pos_to_tile(&wld.map, x, y);
        if (!t) continue;
        total++;
        enum known_type k = client_tile_get_known(t);
        if (k == TILE_UNKNOWN) { unk++; if (tile_terrain(t)) unk_terr++; }
        else if (k == TILE_KNOWN_UNSEEN) unse++;
        else if (k == TILE_KNOWN_SEEN) vis++;
      }
  }
  std::fprintf(stderr,
    "[irrg] DIAG map %dx%d topo=%d wrap=%d: total=%d unknown=%d unknown_with_terrain=%d unse=%d vis=%d\n",
    (int)wld.map.xsize, (int)wld.map.ysize,
    (int)wld.map.topology_id, (int)wld.map.wrap_id,
    total, unk, unk_terr, unse, vis);
}

void irrg_map3d_render(void)
{
  if (!built || !vdrv || !smgr) return;
  vdrv->beginScene(true, true, video::SColor(255, 70, 130, 180));
  smgr->setActiveCamera(camera);
  smgr->drawAll();
  vdrv->endScene();
}

/* Draw the 3D scene WITHOUT its own beginScene/endScene, so the caller can
 * render 3D + a 2D overlay inside a single scene (one window present). */
void irrg_map3d_draw3d(void)
{
  if (!built || !smgr) return;
  smgr->setActiveCamera(camera);
  smgr->drawAll();
}

/* Sync the unit billboards + selection ring to the current client state and
 * let smgr->drawAll() (in irrg_map3d_draw3d) render them. Billboards are cached
 * per unit id and repositioned each frame (units move); textures are cached per
 * unit type. The ring follows the focused (selected) unit and pulses. */
void irrg_map3d_draw_units(void)
{
  if (!built || !smgr || !vdrv) return;
  if (!unit_parent) unit_parent = smgr->addEmptySceneNode();
  if (!ring_tex)    ring_tex    = make_ring_texture();

  /* FC_IRR_SELUNIT=1 (headless test): focus the player's first unit once so the
   * selection ring can be verified. */
  static bool selunit_done = false;
  if (!selunit_done && std::getenv("FC_IRR_SELUNIT")) {
    const struct player *mp = client_player();
    if (mp && mp->units && unit_list_size(mp->units) > 0) {
      struct unit *fu = 0, *fs = 0;
      const bool want_settler = std::getenv("FC_IRR_FOCUSSETTLER");
      unit_list_iterate(mp->units, pu) {
        if (!fu) fu = pu;
        if (want_settler && !fs) {
          std::string nm = tolower_str(untranslated_name(&unit_type_get(pu)->name));
          if (nm.find("settl") != std::string::npos
              || nm.find("colon") != std::string::npos) fs = pu;
        }
      } unit_list_iterate_end;
      struct unit *pick = (want_settler && fs) ? fs : fu;
      if (pick) {
        unit_focus_set_and_select(pick);
        std::fprintf(stderr, "[irrg] SELUNIT: focused unit id=%d type=%s\n",
                     pick->id, untranslated_name(&unit_type_get(pick)->name));
        if (std::getenv("FC_IRR_UBDBG") && unit_tile(pick)) {
          struct tile *st = unit_tile(pick);
          const struct terrain *stt = tile_terrain(st);
          std::fprintf(stderr,
            "[irrg] SELUNIT: unit %d at tile(%d,%d) terrain=%s found_city_ok=%d moves=%d\n",
            pick->id,
            index_to_map_pos_x(tile_index(st)), index_to_map_pos_y(tile_index(st)),
            stt ? untranslated_name(&stt->name) : "(none)",
            (int)unit_can_do_action(pick, ACTION_FOUND_CITY), pick->moves_left);
          std::fflush(stderr);
        }
        selunit_done = true;
      }
    }
  }

  const struct player *me = client_player();
  std::set<int> seen;

  if (me && me->units) {
    unit_list_iterate(me->units, punit) {
      int id = punit->id;
      if (id <= 0) continue;
      seen.insert(id);
      struct tile *pt = unit_tile(punit);
      if (!pt) continue;
      int tx = index_to_map_pos_x(tile_index(pt));
      int ty = index_to_map_pos_y(tile_index(pt));
      core::vector3df pos((f32)fx(tx), 0.55f, (f32)ty);

      std::map<int, UBB>::iterator it = unit_bb.find(id);
      if (it == unit_bb.end()) {
        int tw = 0, th = 0;
        video::ITexture *tex = unit_type_texture(unit_type_get(punit), &tw, &th);
        if (!tex || tw <= 0) continue;
        f32 scale = 0.9f;                          /* a bit under one tile wide */
        f32 bh = (f32)th / (f32)tw * scale;        /* keep the sprite aspect */
        /* 1.8.5 billboards are untextured quads; the texture is set after. */
        scene::ISceneNode *node = smgr->addBillboardSceneNode(
            unit_parent, core::dimension2d<f32>(scale, bh), pos, -1,
            video::SColor(255, 255, 255, 255), video::SColor(255, 255, 255, 255));
        if (!node) continue;
        node->setMaterialTexture(0, tex);
        node->setMaterialFlag(video::EMF_LIGHTING, false);
        node->setMaterialFlag(video::EMF_ZWRITE_ENABLE, false);
        node->setMaterialType(video::EMT_TRANSPARENT_ALPHA_CHANNEL);
        UBB u; u.node = node;
        unit_bb[id] = u;
      } else if (it->second.node) {
        it->second.node->setPosition(pos);
      }
    } unit_list_iterate_end;
  }

  /* Drop billboards whose units no longer exist (moved out of our list / gone). */
  for (std::map<int, UBB>::iterator it = unit_bb.begin(); it != unit_bb.end(); ) {
    if (seen.find(it->first) == seen.end()) {
      if (it->second.node) it->second.node->remove();
      it = unit_bb.erase(it);
    } else {
      ++it;
    }
  }

  /* Glowing, animated selection ring around the focused unit. */
  struct unit *funit = head_of_units_in_focus();
  if (funit && unit_tile(funit) && ring_tex) {
    int tx = index_to_map_pos_x(tile_index(unit_tile(funit)));
    int ty = index_to_map_pos_y(tile_index(unit_tile(funit)));
    ring_anim_t += 0.033f;
    f32 pulse = 0.95f + 0.18f * std::sin(ring_anim_t * 3.2f);
    if (!ring_node) {
      scene::ISceneNode *rn = smgr->addBillboardSceneNode(
          unit_parent, core::dimension2d<f32>(1.8f, 1.8f),
          core::vector3df((f32)fx(tx), 0.5f, (f32)ty), -1,
          video::SColor(255, 255, 255, 255), video::SColor(255, 255, 255, 255));
      if (rn) {
        rn->setMaterialTexture(0, ring_tex);
        rn->setMaterialFlag(video::EMF_LIGHTING, false);
        rn->setMaterialFlag(video::EMF_ZWRITE_ENABLE, false);
        rn->setMaterialType(video::EMT_TRANSPARENT_ADD_COLOR);   /* additive => glow */
        ring_node = rn;
      }
    }
    if (ring_node) {
      ring_node->setVisible(true);
      ring_node->setPosition(core::vector3df((f32)fx(tx), 0.5f, (f32)ty));
      ring_node->setScale(core::vector3df(pulse, pulse, pulse));
    }
  } else if (ring_node) {
    ring_node->setVisible(false);
  }
}

/* Sync city + resource billboards to the current client state and let
 * smgr->drawAll() (in irrg_map3d_draw3d) render them. Cities are the player's
 * cities (always known to their owner); resources (bonus/ore) are drawn only on
 * tiles the player has explored, matching the terrain (which omits TILE_UNKNOWN
 * tiles). Cities key by city id, resources by tile index. */
void irrg_map3d_draw_cities_and_resources(void)
{
  if (!built || !smgr || !vdrv) return;
  if (!unit_parent) unit_parent = smgr->addEmptySceneNode();

  const struct player *me = client_player();

  /* ---- Cities ---- */
  std::set<int> city_seen;
  if (me && me->cities) {
    city_list_iterate(me->cities, pcity) {
      int id = pcity->id;
      if (id <= 0) continue;
      city_seen.insert(id);
      struct tile *pt = pcity->tile;
      if (!pt) continue;
      int tx = index_to_map_pos_x(tile_index(pt));
      int ty = index_to_map_pos_y(tile_index(pt));
      core::vector3df pos((f32)fx(tx), 0.6f, (f32)ty);
      std::map<int, UBB>::iterator it = city_bb.find(id);
      if (it == city_bb.end()) {
        int style = style_of_city(pcity);
        int nst = game.control.num_city_styles;
        if (style < 0) style = 0;
        if (nst > 0 && style >= nst) style = nst - 1;
        int tw = 0, th = 0;
        video::ITexture *tex = city_style_texture(style, &tw, &th);
        if (!tex || tw <= 0) continue;
        f32 scale = 1.4f;                              /* a bit over one tile */
        f32 bh = (f32)th / (f32)tw * scale;            /* keep the aspect */
        scene::ISceneNode *node = smgr->addBillboardSceneNode(
            unit_parent, core::dimension2d<f32>(scale, bh), pos, -1,
            video::SColor(255, 255, 255, 255), video::SColor(255, 255, 255, 255));
        if (!node) continue;
        node->setMaterialTexture(0, tex);
        node->setMaterialFlag(video::EMF_LIGHTING, false);
        node->setMaterialFlag(video::EMF_ZWRITE_ENABLE, false);
        node->setMaterialType(video::EMT_TRANSPARENT_ALPHA_CHANNEL);
        UBB c; c.node = node;
        city_bb[id] = c;
      } else if (it->second.node) {
        it->second.node->setPosition(pos);
      }
    } city_list_iterate_end;
  }
  for (std::map<int, UBB>::iterator it = city_bb.begin(); it != city_bb.end(); ) {
    if (city_seen.find(it->first) == city_seen.end()) {
      if (it->second.node) it->second.node->remove();
      it = city_bb.erase(it);
    } else ++it;
  }

  /* ---- Resources (bonus/ore on explored tiles) ---- */
  std::set<int> res_seen;
  for (int y = 0; y < mh; ++y) {
    for (int x = 0; x < mw; ++x) {
      struct tile *ptile = map_pos_to_tile(&wld.map, x, y);
      if (!ptile || !ptile->resource) continue;
      if (client_tile_get_known(ptile) == TILE_UNKNOWN) continue;
      int tidx = tile_index(ptile);
      res_seen.insert(tidx);
      core::vector3df pos((f32)fx(x), 0.5f, (f32)y);
      std::map<int, UBB>::iterator it = res_bb.find(tidx);
      if (it == res_bb.end()) {
        int tw = 0, th = 0;
        video::ITexture *tex = resource_texture(ptile->resource, &tw, &th);
        if (!tex || tw <= 0) continue;
        f32 scale = 0.7f;                               /* smaller than a tile */
        f32 bh = (f32)th / (f32)tw * scale;
        scene::ISceneNode *node = smgr->addBillboardSceneNode(
            unit_parent, core::dimension2d<f32>(scale, bh), pos, -1,
            video::SColor(255, 255, 255, 255), video::SColor(255, 255, 255, 255));
        if (!node) continue;
        node->setMaterialTexture(0, tex);
        node->setMaterialFlag(video::EMF_LIGHTING, false);
        node->setMaterialFlag(video::EMF_ZWRITE_ENABLE, false);
        node->setMaterialType(video::EMT_TRANSPARENT_ALPHA_CHANNEL);
        UBB r; r.node = node;
        res_bb[tidx] = r;
      } else if (it->second.node) {
        it->second.node->setPosition(pos);
      }
    }
  }
  for (std::map<int, UBB>::iterator it = res_bb.begin(); it != res_bb.end(); ) {
    if (res_seen.find(it->first) == res_seen.end()) {
      if (it->second.node) it->second.node->remove();
      it = res_bb.erase(it);
    } else ++it;
  }
}

static video::ITexture *scene_rt = 0;

/* Render the 3D scene to an offscreen render target (window-sized) and return
 * the texture. The 3D rasterization writes into the RT (which works under the
 * software driver); gui_main then draw2DImage's this texture together with the
 * 2D dialog overlay in ONE beginScene/endScene so the window is presented
 * exactly once per frame. This eliminates the flicker that came from the
 * overlay's separate second endScene only reaching the window on ~half the
 * frames. */
void *irrg_map3d_render_scene(void)
{
  if (!built || !vdrv || !smgr) return 0;
  core::dimension2d<u32> ssz = vdrv->getScreenSize();
  core::dimension2d<u32> dim(ssz.Width, ssz.Height);
  if (!scene_rt || scene_rt->getSize() != dim) {
    if (scene_rt) vdrv->removeTexture(scene_rt);   /* cache-owned: remove, not drop */
    scene_rt = vdrv->addRenderTargetTexture(dim, "irrg_scene3d", video::ECF_A8R8G8B8);
    if (!scene_rt) return 0;
  }
  vdrv->setRenderTarget(scene_rt, true, true, video::SColor(255, 70, 130, 180));
  vdrv->beginScene(true, true, video::SColor(255, 70, 130, 180));
  smgr->setActiveCamera(camera);
  smgr->drawAll();
  vdrv->endScene();
  vdrv->setRenderTarget(0, true, true, video::SColor(255, 0, 0, 0));
  return scene_rt;
}

int irrg_map3d_pick(int mx, int my, int *out_x, int *out_y)
{
  core::vector3df hit;
  if (!ground_hit(mx, my, &hit)) return 0;
  *out_x = fx((int)std::lround(hit.X));
  *out_y = (int)std::lround(hit.Z);
  return 1;
}

void irrg_map3d_center_on(int tx, int ty)
{
  if (!built) return;
  place_camera(tx, ty);
}

/* Re-centre the 3D camera on a unit's tile (used when the unit dialog is
 * clicked or a unit is selected). Re-attaches the auto-framing (clears the
 * pan-detach flag). */
void irrg_map3d_focus_unit(int tx, int ty)
{
  if (!built) return;
  g_cam_panned = false;
  place_camera(tx, ty);
}

/* Re-centre the 3D view on whatever unit is currently focused (used when the
 * unit dialog is clicked, to snap the camera back to the unit). */
void irrg_map3d_refocus_current_unit(void)
{
  struct unit *fu = head_of_units_in_focus();
  if (!fu || !built) return;
  irrg_map3d_focus_unit(index_to_map_pos_x(tile_index(fu->tile)),
                        index_to_map_pos_y(tile_index(fu->tile)));
}

void irrg_map3d_pan(int dx, int dz)
{
  if (!built) return;
  g_cam_panned = true;   /* the user is steering the camera; stop auto-framing */
  cam_pos.X -= (f32)dx;  cam_target.X -= (f32)dx;
  cam_pos.Z -= (f32)dz;  cam_target.Z -= (f32)dz;
  if (camera) {
    camera->setPosition(cam_pos);
    camera->setTarget(cam_target);
  }
}

void irrg_map3d_zoom(int delta)
{
  if (!built) return;
  g_cam_panned = true;   /* manual zoom detaches the auto-framing */
  f32 factor = (delta > 0) ? 0.9f : 1.1f;
  core::vector3df d = cam_pos - cam_target;
  d *= factor;
  if (d.getLength() < 3.0f)   d.setLength(3.0f);
  if (d.getLength() > 200.0f) d.setLength(200.0f);
  cam_pos = cam_target + d;
  if (camera) {
    camera->setPosition(cam_pos);
    camera->setTarget(cam_target);
  }
}

void irrg_map3d_destroy(void)
{
  if (!built || !smgr) return;
  if (camera)     { camera->remove();     camera = 0; }
  if (map_node)   { map_node->remove();   map_node = 0; }
  if (map_parent) { map_parent->remove(); map_parent = 0; }
  /* Unit billboards + selection ring (Phase 5b). */
  if (ring_node)   { ring_node->remove();   ring_node = 0; }
  for (std::map<int, UBB>::iterator it = unit_bb.begin(); it != unit_bb.end(); ++it)
    if (it->second.node) it->second.node->remove();
  unit_bb.clear();
  /* City + resource billboards (Phase 5c). */
  for (std::map<int, UBB>::iterator it = city_bb.begin(); it != city_bb.end(); ++it)
    if (it->second.node) it->second.node->remove();
  city_bb.clear();
  for (std::map<int, UBB>::iterator it = res_bb.begin(); it != res_bb.end(); ++it)
    if (it->second.node) it->second.node->remove();
  res_bb.clear();
  if (unit_parent) { unit_parent->remove(); unit_parent = 0; }
  for (std::map<const void *, UTex>::iterator it = unit_tex.begin(); it != unit_tex.end(); ++it)
    if (it->second.tex && vdrv) vdrv->removeTexture(it->second.tex);
  unit_tex.clear();
  for (std::map<int, UTex>::iterator it = city_tex.begin(); it != city_tex.end(); ++it)
    if (it->second.tex && vdrv) vdrv->removeTexture(it->second.tex);
  city_tex.clear();
  for (std::map<int, UTex>::iterator it = res_tex.begin(); it != res_tex.end(); ++it)
    if (it->second.tex && vdrv) vdrv->removeTexture(it->second.tex);
  res_tex.clear();
  if (ring_tex && vdrv) vdrv->removeTexture(ring_tex);
  ring_tex = 0;
  if (scene_rt)   {
    if (vdrv) vdrv->removeTexture(scene_rt);
    scene_rt = 0;
  }
  if (map_tex)    {
    /* map_tex is cache-owned (getTexture's only ref is the cache's); remove it
     * from the cache rather than drop()ing it (dangling-entry SIGSEGV). */
    if (vdrv) vdrv->removeTexture(map_tex);
    map_tex = 0;
  }
  built = false;
}
