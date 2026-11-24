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
#ifndef FC__VK3D_MAP_H
#define FC__VK3D_MAP_H

/* Vulkan */
#include <vulkan/vulkan.h>

/* Record the 3D map scene pass into the frame command buffer, inside the
 * render pass and before the 2D GUI layer is drawn. */
void vk3d_map_record(VkCommandBuffer cmd_buffer);

#endif /* FC__VK3D_MAP_H */
