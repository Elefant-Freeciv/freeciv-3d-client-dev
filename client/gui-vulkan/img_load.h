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
#ifndef FC__IMG_LOAD_H
#define FC__IMG_LOAD_H

/* SDL3 */
#include <SDL3/SDL.h>

#ifdef HAVE_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#else
/* Fall back to the small libpng based loader in img_png.c so that the
 * 3D client builds on systems without SDL3_image. */
SDL_Surface *IMG_Load(const char *file);
#endif

#endif /* FC__IMG_LOAD_H */
