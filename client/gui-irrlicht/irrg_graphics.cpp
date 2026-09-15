/***********************************************************************
 Freeciv - gui-irrlicht graphics primitives.
   Phase 4: real 2D rendering. Sprites and canvases are backed by CPU
   SColor* pixel buffers (so all canvas_put_* ops are simple 2D blits);
   a canvas is presented by uploading its buffer to a texture and
   draw2DImage'ing it. Text uses a DejaVuSans IGUIFont (built-in font
   fallback).
***********************************************************************/
#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <irrlicht.h>

#include "irrg_cxxside.h"
#include "graphics.h"
#include "irrg_bfont.h"

using namespace irr;

/* Per-file logging helper (each gui-irrlicht .cpp keeps its own static copy). */
static void irrg_log(const char *msg)
{
  std::fprintf(stderr, "[irrg] %s\n", msg);
  std::fflush(stderr);
}

/* =====================================================================
 * Canvas orientation / upload
 * ===================================================================*/
/* Whether the canvas rows must be vertically reversed before the texture is
 * drawn. The CPU canvas is top-down (row 0 = the TOP of the image). Irrlicht's
 * software driver draws a top-down upload right-side up, but its OpenGL driver
 * maps the texture Y-axis the other way, so the SAME top-down upload comes out
 * upside down there. We therefore reverse the rows for the OpenGL driver.
 * FC_IRR_FLIP=0/1 forces it off/on for a box/driver that disagrees. */
static bool irrg_should_flip(video::IVideoDriver *vd)
{
  static int cache = -1;
  if (cache < 0) {
    const char *f = std::getenv("FC_IRR_FLIP");
    if (f) cache = (f[0] == '0') ? 0 : 1;
    else {
      bool fl = (vd && vd->getDriverType() == video::EDT_OPENGL);
      cache = fl ? 1 : 0;
    }
    std::fprintf(stderr, "[irrg] canvas upload row-flip: %s (driver=%d)\n",
                 cache ? "ON" : "off", (int)(vd ? vd->getDriverType() : -1));
  }
  return cache == 1;
}

/* Upload a top-down CPU SColor buffer (w x h) into a texture, reversing the
 * row order when `flip` is set (see irrg_should_flip). Handles the software
 * driver's A1R5G5B5 render targets (2 bytes/px). Shared by the plain and the
 * zoomed 2D present paths. */
static void irrg_upload_texture(video::ITexture *tex, const video::SColor *src,
                                int w, int h, bool flip)
{
  if (!tex || !src) return;
  void *dst = tex->lock();
  if (!dst) return;
  s32 pitch = tex->getPitch();
  if (tex->getColorFormat() == video::ECF_A1R5G5B5) {
    for (int y = 0; y < h; ++y) {
      int sy = flip ? (h - 1 - y) : y;
      u16 *drow = (u16 *)((u8 *)dst + (size_t)y * pitch);
      const video::SColor *srow = src + (size_t)sy * w;
      for (int x = 0; x < w; ++x)
        drow[x] = video::A8R8G8B8toA1R5G5B5(srow[x].color);
    }
  } else {
    const u8 *s = (const u8 *)src;
    size_t rowbytes = (size_t)w * 4;
    for (int y = 0; y < h; ++y) {
      int sy = flip ? (h - 1 - y) : y;
      std::memcpy((u8 *)dst + (size_t)y * pitch, s + (size_t)sy * rowbytes, rowbytes);
    }
  }
  tex->unlock();
}

/* =====================================================================
 * Fonts
 * ===================================================================*/
static const char *irrg_font_path(void)
{
  static const char *p = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
  return p;
}
/* UTF-8 -> std::wstring (Irrlicht 1.8.5 fonts take wchar_t / stringw). */
static std::wstring irrg_to_wide(const char *utf8)
{
  std::wstring w;
  if (!utf8) return w;
  const unsigned char *p = (const unsigned char *)utf8;
  while (*p) {
    unsigned int cp = 0; int len = 1;
    if (*p < 0x80) { cp = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; len = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; len = 3; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; len = 4; }
    else { cp = *p; len = 1; }
    for (int i = 1; i < len; ++i) {
      if (p[i]) cp = (cp << 6) | (p[i] & 0x3F); else break;
    }
    w += (wchar_t)cp;
    p += len;
  }
  return w;
}
static gui::IGUIFont *irrg_fonts[FONT_COUNT];   /* global font cache */
[[maybe_unused]]
static gui::IGUIFont *irrg_font(enum client_font f)
{
  if (f < 0 || f >= FONT_COUNT) f = FONT_CITY_NAME;
  if (!irrg_fonts[f]) {
    gui::IGUIEnvironment *env = (gui::IGUIEnvironment *)irrg_guienv();
    if (env) {
      /* Irrlicht 1.8.5 getFont takes only a filename (fixed size). */
      irrg_fonts[f] = env->getFont(irrg_font_path());
      if (!irrg_fonts[f]) irrg_fonts[f] = env->getBuiltInFont();
      if (irrg_fonts[f]) irrg_fonts[f]->grab();
    }
  }
  return irrg_fonts[f];
}

/* =====================================================================
 * Pixel helpers
 * ===================================================================*/
/* Read a texture's pixels into a freshly-allocated SColor* buffer. */
[[maybe_unused]]
static video::SColor *irrg_read_texture_pixels(video::ITexture *tex)
{
  core::vector2d<u32> sz = tex->getSize();
  if (sz.X == 0 || sz.Y == 0) return 0;
  video::SColor *buf =
    (video::SColor *)std::calloc((size_t)sz.X * sz.Y, sizeof(video::SColor));
  if (!buf) return 0;
  void *data = tex->lock();
  if (!data) { std::free(buf); return 0; }
  s32 pitch = tex->getPitch();
  video::ECOLOR_FORMAT fmt = tex->getColorFormat();
  for (u32 y = 0; y < sz.Y; ++y) {
    const u8 *row = (const u8 *)data + (u32)y * (u32)pitch;
    video::SColor *dst = buf + (size_t)y * sz.X;
    for (u32 x = 0; x < sz.X; ++x) {
      switch (fmt) {
      case video::ECF_A8R8G8B8:
        dst[x] = video::SColor(*(const u32 *)(row + x * 4));
        break;
      case video::ECF_A1R5G5B5:
        dst[x] = video::SColor(video::A1R5G5B5toA8R8G8B8(*(const u16 *)(row + x * 2)));
        break;
      case video::ECF_R8G8B8:
        dst[x] = video::SColor(255, row[x*3], row[x*3+1], row[x*3+2]);
        break;
      default:
        dst[x] = video::SColor(255, 255, 255, 255);
        break;
      }
    }
  }
  tex->unlock();
  return buf;
}

/* Blit a (sub-)region of a sprite's pixels onto a canvas with alpha blend. */
static void irrg_blit_sprite(video::SColor *dst, int dw, int dh,
                             const video::SColor *src, int sw, int sh,
                             int cx, int cy, int ox, int oy, int w, int h)
{
  if (!dst || !src) return;
  for (int y = 0; y < h; ++y) {
    int sy = oy + y, dy = cy + y;
    if (sy < 0 || sy >= sh || dy < 0 || dy >= dh) continue;
    for (int x = 0; x < w; ++x) {
      int sx = ox + x, dx = cx + x;
      if (sx < 0 || sx >= sw || dx < 0 || dx >= dw) continue;
      video::SColor s = src[(size_t)sy * sw + sx];
      u32 sa = s.getAlpha();
      if (sa == 0) continue;
      video::SColor d = dst[(size_t)dy * dw + dx];
      if (sa == 255) {
        dst[(size_t)dy * dw + dx] = s;
      } else {
        u32 r = (s.getRed()   * sa + d.getRed()   * (255 - sa)) / 255;
        u32 g = (s.getGreen() * sa + d.getGreen() * (255 - sa)) / 255;
        u32 b = (s.getBlue()  * sa + d.getBlue()  * (255 - sa)) / 255;
        dst[(size_t)dy * dw + dx] = video::SColor(255, r, g, b);
      }
    }
  }
}

/* =====================================================================
 * Colors
 * ===================================================================*/
struct color *irrg_color_alloc(int r, int g, int b)
{
  struct color *c = (struct color *)std::malloc(sizeof(*c));
  if (c) { c->r = r; c->g = g; c->b = b; }
  return c;
}
void irrg_color_free(struct color *pcolor) { std::free(pcolor); }

static video::SColor irrg_scolor(const struct color *c)
{
  return video::SColor(255, c->r, c->g, c->b);
}

/* =====================================================================
 * Sprites
 * ===================================================================*/
static video::IVideoDriver *irrg_vd(void)
{
  return (video::IVideoDriver *)irrg_vdriver();
}

struct sprite *irrg_create_sprite(int width, int height, struct color *pcolor)
{
  struct sprite *s = (struct sprite *)std::malloc(sizeof(*s));
  if (!s) return 0;
  s->width = width; s->height = height;
  s->irr_tex = 0; s->crop_x = 0; s->crop_y = 0;
  s->pixels = std::calloc((size_t)width * height, sizeof(video::SColor));
  if (s->pixels && pcolor) {
    video::SColor col = irrg_scolor(pcolor);
    video::SColor *p = (video::SColor *)s->pixels;
    for (int i = 0; i < width * height; ++i) p[i] = col;
  }
  return s;
}
void irrg_get_sprite_dimensions(struct sprite *sprite, int *width, int *height)
{
  if (sprite) { *width = sprite->width; *height = sprite->height; }
}
void irrg_free_sprite(struct sprite *s)
{
  if (!s) return;
  /* No ITexture to drop (sprites are pure CPU buffers now). */
  std::free(s->pixels);
  std::free(s);
}
/* Load an image file straight into a CPU SColor* buffer using the Irrlicht
 * image loaders (PNG/JPG/BMP/TGA) via createImageFromFile(). Bypasses the
 * video driver's texture cache entirely, so there is no refcounted ITexture
 * to dangle. */
struct sprite *irrg_load_gfxfile(const char *filename, bool svgflag)
{
  (void)svgflag;   /* no SVG support in this build */
  if (!filename) return 0;
  video::IVideoDriver *vd = irrg_vd();
  if (!vd) return 0;
  video::IImage *img = vd->createImageFromFile(filename);
  if (!img) {
    std::fprintf(stderr, "[irrg] load_gfxfile: cannot load %s\n", filename);
    return 0;
  }
  core::vector2d<u32> dim = img->getDimension();
  struct sprite *s = (struct sprite *)std::malloc(sizeof(*s));
  if (!s) { img->drop(); return 0; }
  s->width = (int)dim.X; s->height = (int)dim.Y;
  s->irr_tex = 0; s->crop_x = 0; s->crop_y = 0;
  s->pixels = std::calloc((size_t)dim.X * dim.Y, sizeof(video::SColor));
  if (s->pixels) {
    /* 1:1 copy into A8R8G8B8 (SColor == u32, 4 bytes/pixel, pitch = w*4). */
    img->copyToScaling(s->pixels, dim.X, dim.Y, video::ECF_A8R8G8B8, dim.X * 4);
  }
  img->drop();
  return s;
}
struct sprite *irrg_load_gfxnumber(int num)
{
  /* Draw the NUMBER `num` (the unit stack-size indicator) as a small badge,
   * matching the GTK client (which renders the digit with pango); the SDL2
   * client returns NULL (no stack indicator). tilespec.c calls this with
   * num = 1..stack_count for the sprite shown on a stack of that size (e.g. a
   * 5-unit stack shows "5"), so the badge is a digit, not a marker. Rendered
   * with the CPU bitmap font (white digit + 1px black outline on a transparent
   * background) so it's legible on any terrain. Must NEVER return NULL: the
   * unit-stack loader (tilespec.c fill_sprite_array) does fc_realloc(...,0)
   * which is fatal on a NULL result with count 0 -- so even num<=0 yields a
   * (tiny, empty) sprite. */
  const std::string txt = (num > 0) ? std::to_string(num) : std::string();
  int tw = 0;
  for (const unsigned char *p = (const unsigned char *)txt.c_str(); *p; ++p)
    if (*p >= IRRG_BFONT_FIRST && *p < IRRG_BFONT_FIRST + IRRG_BFONT_COUNT)
      tw += irrg_bfont_glyphs[*p - IRRG_BFONT_FIRST].advance;
  if (tw <= 0) tw = 4;
  const int pad = 1;
  const int W = tw + pad * 2;
  const int H = IRRG_BFONT_H;   /* 19 */
  struct sprite *s = (struct sprite *)std::malloc(sizeof(*s));
  if (!s) return 0;
  s->width = W; s->height = H;
  s->irr_tex = 0; s->crop_x = 0; s->crop_y = 0;
  video::SColor *px = (video::SColor *)std::calloc((size_t)W * H, sizeof(video::SColor));
  if (!px) { std::free(s); return 0; }
  const video::SColor fg(255, 255, 255, 255);       /* white digit   */
  const video::SColor outline(255, 10, 10, 10);     /* black outline */
  /* Blit the digit glyph run at pixel offset (ox,oy) in color c (skips pixels
   * outside the buffer; only sets ON-pixels, so a transparent bg survives). */
  auto blit = [&](int ox, int oy, video::SColor c) {
    int cx = pad + ox;
    for (const unsigned char *p = (const unsigned char *)txt.c_str(); *p; ++p) {
      unsigned char ch = *p;
      if (ch < IRRG_BFONT_FIRST || ch >= IRRG_BFONT_FIRST + IRRG_BFONT_COUNT) continue;
      const struct irrg_bfont_glyph *g = &irrg_bfont_glyphs[ch - IRRG_BFONT_FIRST];
      for (int row = 0; row < IRRG_BFONT_H; ++row) {
        uint32_t bits = g->rows[row];
        if (!bits) continue;
        for (int gx = 0; gx < g->width; ++gx) {
          if (!(bits & (1u << (31 - gx)))) continue;
          const int dx = cx + gx, dy = row + oy;
          if (dx >= 0 && dx < W && dy >= 0 && dy < H) px[(size_t)dy * W + dx] = c;
        }
      }
      cx += g->advance;
    }
  };
  for (int oy = -1; oy <= 1; ++oy)        /* 1px outline (8-way) first */
    for (int ox = -1; ox <= 1; ++ox)
      if (ox != 0 || oy != 0) blit(ox, oy, outline);
  blit(0, 0, fg);                          /* then the white digit on top */
  {
    int wp = 0, kp = 0;
    for (int i = 0; i < W * H; ++i) {
      if (px[i].getAlpha() == 255 && px[i].getRed() > 200) wp++;
      else if (px[i].getAlpha() == 255 && px[i].getRed() < 60) kp++;
    }
    FILE *df = std::fopen("/tmp/gfxnum.txt", "a");
    if (df) { std::fprintf(df, "num=%d txt='%s' W=%d H=%d white=%d dark=%d\n",
                           num, txt.c_str(), W, H, wp, kp); std::fclose(df); }
  }
  s->pixels = px;
  return s;
}
struct sprite *irrg_crop_sprite(struct sprite *source, int x, int y,
                                int width, int height, struct sprite *mask,
                                int mask_offset_x, int mask_offset_y,
                                float scale, bool smooth)
{
  (void)mask; (void)mask_offset_x; (void)mask_offset_y; (void)scale; (void)smooth;
  if (!source) return 0;
  struct sprite *s = (struct sprite *)std::malloc(sizeof(*s));
  if (!s) return 0;
  s->width = width; s->height = height; s->crop_x = x; s->crop_y = y;
  s->irr_tex = 0;
  /* Copy the cropped region from the source's CPU pixels. */
  s->pixels = std::calloc((size_t)width * height, sizeof(video::SColor));
  if (s->pixels && source->pixels) {
    video::SColor *dst = (video::SColor *)s->pixels;
    const video::SColor *src = (const video::SColor *)source->pixels;
    for (int yy = 0; yy < height; ++yy) {
      int sy = y + yy;
      if (sy < 0 || sy >= source->height) continue;
      for (int xx = 0; xx < width; ++xx) {
        int sx = x + xx;
        if (sx < 0 || sx >= source->width) continue;
        dst[(size_t)yy * width + xx] = src[(size_t)sy * source->width + sx];
      }
    }
  }
  return s;
}

/* =====================================================================
 * Canvas
 * ===================================================================*/
struct canvas *irrg_canvas_create(int width, int height)
{
  struct canvas *c = (struct canvas *)std::malloc(sizeof(*c));
  if (!c) return 0;
  c->width = width; c->height = height; c->zoom = 1.0f;
  c->is_mapview = false; c->irr_rtt = 0;
  c->pixels = std::calloc((size_t)width * height, sizeof(video::SColor));
  return c;
}
void irrg_canvas_free(struct canvas *store)
{
  if (!store) return;
  if (store->irr_rtt) {
    /* The present texture is owned by the driver's texture cache (addRenderTarget
     * Texture's only ref is the cache's). removeTexture erases the cache entry AND
     * drops that ref; a plain drop() would free the SSurface but leave a dangling
     * cache entry that a later findTexture() binary_search dereferences (SIGSEGV). */
    video::IVideoDriver *vd = irrg_vd();
    if (vd) vd->removeTexture((video::ITexture *)store->irr_rtt);
    store->irr_rtt = 0;
  }
  std::free(store->pixels);
  std::free(store);
}
void irrg_canvas_set_zoom(struct canvas *store, float zoom)
{
  if (store) store->zoom = zoom;
}
bool irrg_has_zoom_support(void) { return true; }
static struct canvas *g_map_canvas = 0;
struct canvas *irrg_get_map_canvas(void) { return g_map_canvas; }
void irrg_canvas_mapview_init(struct canvas *store)
{
  if (store) { store->is_mapview = true; g_map_canvas = store; }
}

/* Present a canvas to the window: upload its CPU buffer to a single cached
 * ITexture (created/resized on demand, stored in c->irr_rtt) and draw2DImage
 * it at the origin. This is the only texture we manage, so its lifecycle is
 * fully under our control (no texture-cache dangling pointers). */
void irrg_canvas_present(struct canvas *c)
{
  if (!c || !c->pixels || c->width <= 0 || c->height <= 0) return;
  video::IVideoDriver *vd = irrg_vd();
  if (!vd) return;
  core::vector2d<u32> dim((u32)c->width, (u32)c->height);
  video::ITexture *tex = (video::ITexture *)c->irr_rtt;
  if (!tex || tex->getSize() != dim) {
    if (tex) vd->removeTexture(tex);   /* erase cache entry + free (see canvas_free) */
    tex = vd->addRenderTargetTexture(dim, "irrg_canvas", video::ECF_A8R8G8B8);
    c->irr_rtt = tex;
    if (!tex) return;
  }
  irrg_upload_texture(tex, (const video::SColor *)c->pixels,
                      c->width, c->height, irrg_should_flip(vd));
  vd->draw2DImage(tex, core::position2d<s32>(0, 0),
                  core::rect<s32>(0, 0, c->width, c->height), 0,
                  video::SColor(255, 255, 255, 255), true);
}

/* Zoomed 2D present: show a centered crop of the canvas, CPU nearest-neighbor
 * upscaled to fill the window. Used to zoom the 2D map in so the small explored
 * area (fog of war) fills more of the screen. Nearest-neighbor keeps it cheap
 * and crisp; the software driver's draw2DImage cannot scale, so we upscale the
 * CPU buffer ourselves and upload it to a cached texture. */
void irrg_canvas_present_zoomed(struct canvas *c, float zoom,
                                int win_w, int win_h)
{
  if (!c || !c->pixels || c->width <= 0 || c->height <= 0
      || win_w <= 0 || win_h <= 0) return;
  if (zoom < 1.0f) zoom = 1.0f;
  video::IVideoDriver *vd = irrg_vd();
  if (!vd) return;

  /* Central crop of the canvas to show (zoom>1 => smaller region). The capital
   * sits at the canvas center (the mapview is centered on it), so centering the
   * crop keeps the player in the middle. */
  int crop_w = (int)((float)c->width  / zoom);
  int crop_h = (int)((float)c->height / zoom);
  if (crop_w < 1) crop_w = 1;
  if (crop_h < 1) crop_h = 1;
  if (crop_w > c->width)  crop_w = c->width;
  if (crop_h > c->height) crop_h = c->height;
  int cx0 = (c->width  - crop_w) / 2;
  int cy0 = (c->height - crop_h) / 2;

  static video::SColor *zbuf = nullptr;   /* scratch: upscaled crop            */
  static size_t         zbuf_n = 0;       /*                                   */
  static video::ITexture *ztex = nullptr; /* cached present texture            */
  size_t need = (size_t)win_w * win_h;
  if (!zbuf || zbuf_n < need) {
    std::free(zbuf);
    zbuf = (video::SColor *)std::malloc(need * sizeof(video::SColor));
    zbuf_n = zbuf ? need : 0;
    if (!zbuf) return;
  }

  const video::SColor *src = (const video::SColor *)c->pixels;
  for (int y = 0; y < win_h; ++y) {
    int sy = cy0 + (int)((float)y * (float)crop_h / (float)win_h);
    if (sy < 0) sy = 0; else if (sy >= c->height) sy = c->height - 1;
    const video::SColor *srow = src + (size_t)sy * c->width;
    video::SColor *drow = zbuf + (size_t)y * win_w;
    for (int x = 0; x < win_w; ++x) {
      int sx = cx0 + (int)((float)x * (float)crop_w / (float)win_w);
      if (sx < 0) sx = 0; else if (sx >= c->width) sx = c->width - 1;
      drow[x] = srow[sx];
    }
  }

  core::vector2d<u32> dim((u32)win_w, (u32)win_h);
  if (!ztex || ztex->getSize() != dim) {
    if (ztex) vd->removeTexture(ztex);
    ztex = vd->addRenderTargetTexture(dim, "irrg_zoom", video::ECF_A8R8G8B8);
    if (!ztex) return;
  }
  irrg_upload_texture(ztex, zbuf, win_w, win_h, irrg_should_flip(vd));
  vd->draw2DImage(ztex, core::position2d<s32>(0, 0),
                  core::rect<s32>(0, 0, win_w, win_h), 0,
                  video::SColor(255, 255, 255, 255), true);
}

/* True if an SColor is a near-black "dark" pixel (fog / no data / off-map). */
static bool irrg_is_dark(const video::SColor &p)
{
  return p.getRed() < 20 && p.getGreen() < 20 && p.getBlue() < 20;
}

void irrg_diagnose_present(const struct canvas *c, float zoom)
{
  if (!c || !c->pixels || c->width <= 0 || c->height <= 0) {
    irrg_log("DIAG present: no map canvas/pixels (not initialized yet?)");
    return;
  }
  if (zoom < 1.0f) zoom = 1.0f;
  int crop_w = (int)((float)c->width  / zoom);
  int crop_h = (int)((float)c->height / zoom);
  if (crop_w < 1) crop_w = 1; if (crop_h < 1) crop_h = 1;
  if (crop_w > c->width)  crop_w = c->width;
  if (crop_h > c->height) crop_h = c->height;
  int cx0 = (c->width  - crop_w) / 2;
  int cy0 = (c->height - crop_h) / 2;

  const video::SColor *src = (const video::SColor *)c->pixels;
  long crop_bright = 0, crop_tot = 0, all_bright = 0, all_tot = 0;
  for (int y = cy0; y < cy0 + crop_h; y += 2)
    for (int x = cx0; x < cx0 + crop_w; x += 2) {
      if (!irrg_is_dark(src[(size_t)y * c->width + x])) crop_bright++;
      crop_tot++;
    }
  for (int y = 0; y < c->height; y += 4)
    for (int x = 0; x < c->width; x += 4) {
      if (!irrg_is_dark(src[(size_t)y * c->width + x])) all_bright++;
      all_tot++;
    }

  std::string verdict;
  if (crop_bright == 0 && all_bright == 0)
    verdict = "STORE EMPTY: no terrain anywhere (not connected / all-unexplored fog; an observer sees the full map)";
  else if (crop_bright == 0 && all_bright > 0)
    verdict = "CROP DARK but store has content elsewhere -> view not centered on the explored area (centering/zoom)";
  else
    verdict = "crop shows content (OK)";

  char buf[280];
  std::snprintf(buf, sizeof(buf),
    "DIAG present: store=%dx%d crop=(%d,%d %dx%d) crop_bright=%ld/%ld store_bright=%ld/%ld => %s",
    c->width, c->height, cx0, cy0, crop_w, crop_h,
    crop_bright, crop_tot, all_bright, all_tot, verdict.c_str());
  irrg_log(buf);
}

void irrg_canvas_copy(struct canvas *dest, struct canvas *src, int src_x,
                      int src_y, int dest_x, int dest_y, int width, int height)
{
  if (!dest || !src || !dest->pixels || !src->pixels) return;
  video::SColor *d = (video::SColor *)dest->pixels;
  const video::SColor *s = (const video::SColor *)src->pixels;
  for (int y = 0; y < height; ++y) {
    int sy = src_y + y, dy = dest_y + y;
    if (sy < 0 || sy >= src->height || dy < 0 || dy >= dest->height) continue;
    for (int x = 0; x < width; ++x) {
      int sx = src_x + x, dx = dest_x + x;
      if (sx < 0 || sx >= src->width || dx < 0 || dx >= dest->width) continue;
      d[(size_t)dy * dest->width + dx] = s[(size_t)sy * src->width + sx];
    }
  }
}

void irrg_canvas_put_sprite(struct canvas *pcanvas, int canvas_x, int canvas_y,
                            struct sprite *psprite, int offset_x, int offset_y,
                            int width, int height)
{
  if (!pcanvas || !pcanvas->pixels || !psprite) return;
  irrg_blit_sprite((video::SColor *)pcanvas->pixels, pcanvas->width,
                   pcanvas->height, (const video::SColor *)psprite->pixels,
                   psprite->width, psprite->height, canvas_x, canvas_y,
                   offset_x, offset_y, width, height);
}
void irrg_canvas_put_sprite_full(struct canvas *pcanvas, int canvas_x,
                                 int canvas_y, struct sprite *psprite)
{
  if (!psprite) return;
  irrg_canvas_put_sprite(pcanvas, canvas_x, canvas_y, psprite, 0, 0,
                         psprite->width, psprite->height);
}
void irrg_canvas_put_sprite_full_scaled(struct canvas *pcanvas, int canvas_x,
                                        int canvas_y, int canvas_w, int canvas_h,
                                        struct sprite *psprite)
{
  /* Nearest-neighbor scale: blit each dest pixel from the scaled src pixel. */
  if (!pcanvas || !pcanvas->pixels || !psprite || !psprite->pixels) return;
  if (canvas_w <= 0 || canvas_h <= 0) return;
  video::SColor *d = (video::SColor *)pcanvas->pixels;
  const video::SColor *s = (const video::SColor *)psprite->pixels;
  for (int y = 0; y < canvas_h; ++y) {
    int dy = canvas_y + y;
    if (dy < 0 || dy >= pcanvas->height) continue;
    int sy = (y * psprite->height) / canvas_h;
    if (sy >= psprite->height) sy = psprite->height - 1;
    for (int x = 0; x < canvas_w; ++x) {
      int dx = canvas_x + x;
      if (dx < 0 || dx >= pcanvas->width) continue;
      int sx = (x * psprite->width) / canvas_w;
      if (sx >= psprite->width) sx = psprite->width - 1;
      video::SColor sp = s[(size_t)sy * psprite->width + sx];
      if (sp.getAlpha() == 0) continue;
      video::SColor dp = d[(size_t)dy * pcanvas->width + dx];
      u32 a = sp.getAlpha();
      d[(size_t)dy * pcanvas->width + dx] = (a == 255) ? sp :
        video::SColor(255,
          (sp.getRed()*a + dp.getRed()*(255-a))/255,
          (sp.getGreen()*a + dp.getGreen()*(255-a))/255,
          (sp.getBlue()*a + dp.getBlue()*(255-a))/255);
    }
  }
}
void irrg_canvas_put_sprite_fogged(struct canvas *pcanvas, int canvas_x,
                                   int canvas_y, struct sprite *psprite,
                                   bool fog, int fog_x, int fog_y)
{
  if (!psprite) return;
  if (!fog) {
    irrg_canvas_put_sprite_full(pcanvas, canvas_x, canvas_y, psprite);
    return;
  }
  /* Fogged: draw the sprite darkened (blend toward black) as a placeholder
   * for FreeCiv's fogged-tile look. */
  if (!pcanvas || !pcanvas->pixels || !psprite->pixels) return;
  video::SColor *d = (video::SColor *)pcanvas->pixels;
  const video::SColor *s = (const video::SColor *)psprite->pixels;
  (void)fog_x; (void)fog_y;
  for (int y = 0; y < psprite->height; ++y) {
    int dy = canvas_y + y;
    if (dy < 0 || dy >= pcanvas->height) continue;
    for (int x = 0; x < psprite->width; ++x) {
      int dx = canvas_x + x;
      if (dx < 0 || dx >= pcanvas->width) continue;
      video::SColor sp = s[(size_t)y * psprite->width + x];
      if (sp.getAlpha() == 0) continue;
      d[(size_t)dy * pcanvas->width + dx] =
        video::SColor(255, sp.getRed()/3, sp.getGreen()/3, sp.getBlue()/3);
    }
  }
}

void irrg_canvas_put_rectangle(struct canvas *pcanvas, struct color *pcolor,
                               int canvas_x, int canvas_y, int width, int height)
{
  if (!pcanvas || !pcanvas->pixels || !pcolor) return;
  video::SColor col = irrg_scolor(pcolor);
  video::SColor *d = (video::SColor *)pcanvas->pixels;
  for (int y = 0; y < height; ++y) {
    int dy = canvas_y + y;
    if (dy < 0 || dy >= pcanvas->height) continue;
    for (int x = 0; x < width; ++x) {
      int dx = canvas_x + x;
      if (dx < 0 || dx >= pcanvas->width) continue;
      d[(size_t)dy * pcanvas->width + dx] = col;
    }
  }
}
void irrg_canvas_fill_sprite_area(struct canvas *pcanvas, struct sprite *psprite,
                                  struct color *pcolor, int canvas_x, int canvas_y)
{
  if (!psprite) return;
  irrg_canvas_put_rectangle(pcanvas, pcolor, canvas_x, canvas_y,
                            psprite->width, psprite->height);
}

/* Bresenham line (LINE_* variants all draw a 1px line for now). */
static void irrg_put_line_px(struct canvas *pcanvas, struct color *pcolor,
                             int x0, int y0, int x1, int y1)
{
  if (!pcanvas || !pcanvas->pixels || !pcolor) return;
  video::SColor col = irrg_scolor(pcolor);
  video::SColor *d = (video::SColor *)pcanvas->pixels;
  int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    if (x0 >= 0 && x0 < pcanvas->width && y0 >= 0 && y0 < pcanvas->height)
      d[(size_t)y0 * pcanvas->width + x0] = col;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void irrg_canvas_put_line(struct canvas *pcanvas, struct color *pcolor,
                          enum line_type ltype, int start_x, int start_y,
                          int dx, int dy)
{
  (void)ltype;
  irrg_put_line_px(pcanvas, pcolor, start_x, start_y, start_x + dx, start_y + dy);
}
void irrg_canvas_put_curved_line(struct canvas *pcanvas, struct color *pcolor,
                                 enum line_type ltype, int start_x, int start_y,
                                 int dx, int dy)
{
  (void)ltype;
  /* Approximate the curve with a straight line for now. */
  irrg_put_line_px(pcanvas, pcolor, start_x, start_y, start_x + dx, start_y + dy);
}

/* =====================================================================
 * Text
 * ===================================================================*/
/* Integer scale for a font slot. The city name is a heading, so it is drawn
 * at 2x the base 16px bitmap font; the other slots use the base size. */
static int irrg_font_scale(enum client_font font)
{
  return (font == FONT_CITY_NAME) ? 2 : 1;
}

void irrg_get_text_size(int *width, int *height, enum client_font font,
                        const char *text)
{
  if (width) *width = 0;
  if (height) *height = 0;
  if (!text) return;
  int scale = irrg_font_scale(font);
  int line_w = 0, max_w = 0, lines = 1;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    if (*p == '\n') { if (line_w > max_w) max_w = line_w; line_w = 0; lines++; }
    else if (*p >= IRRG_BFONT_FIRST && *p < IRRG_BFONT_FIRST + IRRG_BFONT_COUNT)
      line_w += irrg_bfont_glyphs[*p - IRRG_BFONT_FIRST].advance;
  }
  if (line_w > max_w) max_w = line_w;
  if (width) *width = max_w * scale;
  if (height) *height = IRRG_BFONT_H * scale * lines;
}

/* Text is rasterized with a compact CPU bitmap font (see irrg_bfont.h) blitted
 * straight into the canvas SColor buffer. This replaces the earlier approach of
 * drawing an IGUIFont into a render target and reading the pixels back: the
 * Irrlicht *software* driver's render-target lock() returns garbage, so that
 * path produced no visible glyphs headlessly. The bitmap font is deterministic,
 * fast (no per-glyph texture work), and identical under any driver. */
void irrg_canvas_put_text(struct canvas *pcanvas, int canvas_x, int canvas_y,
                          enum client_font font, struct color *pcolor,
                          const char *text)
{
  if (!pcanvas || !pcanvas->pixels || !text) return;
  int scale = irrg_font_scale(font);
  video::SColor col = pcolor ? irrg_scolor(pcolor) : video::SColor(255, 255, 255, 255);
  video::SColor *dst = (video::SColor *)pcanvas->pixels;
  const int W = pcanvas->width, H = pcanvas->height;
  int cx = canvas_x, cy = canvas_y;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    unsigned char ch = *p;
    if (ch == '\n') { cy += IRRG_BFONT_H * scale; cx = canvas_x; continue; }
    if (ch < IRRG_BFONT_FIRST || ch >= IRRG_BFONT_FIRST + IRRG_BFONT_COUNT)
      continue;
    const struct irrg_bfont_glyph *g = &irrg_bfont_glyphs[ch - IRRG_BFONT_FIRST];
    for (int row = 0; row < IRRG_BFONT_H; ++row) {
      uint32_t bits = g->rows[row];
      if (!bits) continue;
      for (int gx = 0; gx < g->width; ++gx) {
        if (!(bits & (1u << (31 - gx)))) continue;
        for (int sy = 0; sy < scale; ++sy)
          for (int sx = 0; sx < scale; ++sx) {
            int dx = cx + gx * scale + sx;
            int dy = cy + row * scale + sy;
            if (dx < 0 || dx >= W || dy < 0 || dy >= H) continue;
            dst[(size_t)dy * W + dx] = col;
          }
      }
    }
    cx += g->advance * scale;
  }
}

void irrg_map_canvas_size_refresh(void) {}

/* =====================================================================
 * Tileset / font option hooks
 * ===================================================================*/
void irrg_tileset_type_set(enum ts_type type) { (void)type; }
void irrg_gui_update_font(const char *font_name, const char *font_value)
{
  (void)font_name; (void)font_value;
  /* Reload fonts on option change: drop the cached fonts. */
  for (int i = 0; i < FONT_COUNT; ++i) {
    if (irrg_fonts[i]) { irrg_fonts[i]->drop(); irrg_fonts[i] = 0; }
  }
}
