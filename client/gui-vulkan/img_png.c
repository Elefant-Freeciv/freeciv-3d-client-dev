/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/***********************************************************************
  img_png.c - minimal IMG_Load() replacement used when the build has no
  SDL3_image. Tilesets, themes and cursors are plain PNG, so libpng is
  all the 3D client needs: decode to 8 bit RGBA and hand back an SDL
  surface in the same format the rest of the client uses.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#ifndef HAVE_SDL3_IMAGE

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

/* SDL3 */
#include <SDL3/SDL.h>

/* libpng */
#include <png.h>

/* utility */
#include "log.h"
#include "mem.h"

/* client/gui-vulkan */
#include "img_load.h"

struct png_load_state {
  png_structp png;
  png_infop info;
  png_bytep *rows;
  SDL_Surface *surface;
};

/**********************************************************************//**
  Read a whole PNG into an SDL surface.
**************************************************************************/
static SDL_Surface *load_png(FILE *file)
{
  png_byte signature[8];
  png_structp png;
  png_infop info;
  png_infop end_info;
  SDL_Surface *surface;
  const SDL_PixelFormatDetails *details;
  png_bytep *rows = nullptr;
  png_uint_32 width, height;
  int bit_depth, color_type, channels;
  SDL_Surface *result = nullptr;

  if (fread(signature, 1, 8, file) != 8
      || png_sig_cmp(signature, 0, 8) != 0) {
    return nullptr;
  }

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                               nullptr, nullptr);
  if (png == nullptr) {
    return nullptr;
  }

  info = png_create_info_struct(png);
  end_info = png_create_info_struct(png);
  if (info == nullptr || end_info == nullptr) {
    png_destroy_read_struct(&png, &info, &end_info);
    return nullptr;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, &end_info);
    if (rows != nullptr) {
      free(rows);
    }
    return nullptr;
  }

  rewind(file);
  png_init_io(png, file);
  png_set_sig_bytes(png, 8);
  png_read_info(png, info);

  width = png_get_image_width(png, info);
  height = png_get_image_height(png, info);
  bit_depth = png_get_bit_depth(png, info);
  color_type = png_get_color_type(png, info);
  channels = png_get_channels(png, info);

  /* Normalise everything to 8 bit RGBA. */
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png);
  }
  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY
      || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }

  png_read_update_info(png, info);
  color_type = png_get_color_type(png, info);
  channels = png_get_channels(png, info);

  surface = SDL_CreateSurface((int) width, (int) height,
                              SDL_PIXELFORMAT_RGBA8888);
  if (surface == nullptr) {
    png_destroy_read_struct(&png, &info, &end_info);
    return nullptr;
  }

  details = SDL_GetPixelFormatDetails(surface->format);
  if (details != nullptr && details->Rmask != 0x000000ff) {
    /* Surface stores B,G,R,A in memory. */
    png_set_bgr(png);
  }

  if (channels < 4) {
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
  }

  rows = (png_bytep *) fc_malloc(sizeof(png_bytep) * height);
  for (png_uint_32 y = 0; y < height; y++) {
    rows[y] = (png_bytep) ((char *) surface->pixels
                           + (size_t) y * surface->pitch);
  }

  png_read_image(png, rows);
  png_read_end(png, end_info);

  result = surface;
  free(rows);
  png_destroy_read_struct(&png, &info, &end_info);
  return result;
}

/**********************************************************************//**
  IMG_Load() replacement: only PNG is supported, which is all Freeciv
  ships for tilesets, themes and cursors.
**************************************************************************/
SDL_Surface *IMG_Load(const char *file)
{
  FILE *handle;
  SDL_Surface *surface;

  handle = fopen(file, "rb");
  if (handle == nullptr) {
    SDL_SetError("Could not open %s", file);
    return nullptr;
  }

  surface = load_png(handle);
  fclose(handle);

  if (surface == nullptr) {
    SDL_SetError("Not a supported image (only PNG is supported): %s", file);
  }

  return surface;
}

#endif /* !HAVE_SDL3_IMAGE */
