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
  vk3d_core.c - Vulkan instance, device, swapchain and the frame that
  composites the 3D map scene with the 2D SDL widget layer.

  Milestone M1 of the 3D renderer: the scene pass is a sky coloured
  clear plus the (currently empty) map scene hook in vk3d_map.c, and the
  GUI layer is blitted opaquely on top exactly like the SDL3 renderer
  did. Later milestones replace the clear with the lit voxel terrain and
  punch a transparent hole in the GUI layer for the map viewport.

  See client/gui-vulkan/PLAN.md.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <string.h>
#include <stdlib.h>

/* SDL3 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

/* Vulkan */
#include <vulkan/vulkan.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"

/* client/gui-vulkan */
#include "vk3d.h"
#include "vk3d_map.h"

/* Generated from client/gui-vulkan/shaders/ by glslc at build time. */
#include "vk_shader_overlay_vert.h"
#include "vk_shader_overlay_frag.h"

#define VK3D_MAX_SWAPCHAIN_IMAGES 8
#define VK3D_MAX_FRAMES_IN_FLIGHT 2

/* Sky colour of the empty scene pass, linear-ish slate like the
 * terrain studio page background. */
static const float vk3d_sky_rgba[4] = { 0.190f, 0.215f, 0.258f, 1.0f };

struct vk3d_context {
  SDL_Window *window;
  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical_device;
  VkDevice device;
  uint32_t queue_family_index;
  VkQueue queue;
  VkCommandPool command_pool;

  /* Swapchain and its attachments. */
  VkSwapchainKHR swapchain;
  VkFormat swapchain_format;
  VkExtent2D swapchain_extent;
  uint32_t swapchain_image_count;
  VkImage swapchain_images[VK3D_MAX_SWAPCHAIN_IMAGES];
  VkImageView swapchain_views[VK3D_MAX_SWAPCHAIN_IMAGES];
  VkRenderPass render_pass;
  VkFramebuffer framebuffers[VK3D_MAX_SWAPCHAIN_IMAGES];

  /* 2D GUI layer texture. */
  VkFormat overlay_format;
  VkImage overlay_image;
  VkDeviceMemory overlay_memory;
  VkImageView overlay_view;
  VkImageLayout overlay_layout;
  uint32_t overlay_width;
  uint32_t overlay_height;
  VkSampler sampler;
  VkDescriptorSetLayout descriptor_layout;
  VkDescriptorPool descriptor_pool;
  VkDescriptorSet descriptor_set;
  VkBuffer staging_buffer;
  VkDeviceMemory staging_memory;
  VkDeviceSize staging_size;
  bool overlay_dirty;

  /* Fullscreen quad pipeline. */
  VkShaderModule vertex_shader;
  VkShaderModule fragment_shader;
  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;

  /* Per frame sync. */
  VkCommandBuffer command_buffers[VK3D_MAX_FRAMES_IN_FLIGHT];
  VkSemaphore image_available[VK3D_MAX_FRAMES_IN_FLIGHT];
  VkSemaphore render_finished[VK3D_MAX_FRAMES_IN_FLIGHT];
  VkFence frames_in_flight[VK3D_MAX_FRAMES_IN_FLIGHT];
  uint32_t current_frame;

  int width;
  int height;
  bool active;
};

static struct vk3d_context vk3d;

/* Fullscreen triangle: covers the viewport, carries GUI layer UVs. */
static const float vk3d_quad_vertices[] = {
  /*  position       uv */
  -1.0f, -1.0f, 0.0f, 1.0f,
   3.0f, -1.0f, 2.0f, 1.0f,
  -1.0f,  3.0f, 0.0f, -1.0f
};

/**********************************************************************//**
  Log a Vulkan failure and report it to the caller.
**************************************************************************/
static bool vk3d_fail(const char *what, VkResult result)
{
  const char *name = nullptr;

  switch (result) {
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:  name = "out of device memory"; break;
  case VK_ERROR_OUT_OF_HOST_MEMORY:    name = "out of host memory"; break;
  case VK_ERROR_INITIALIZATION_FAILED: name = "initialization failed"; break;
  case VK_ERROR_DEVICE_LOST:           name = "device lost"; break;
  case VK_ERROR_EXTENSION_NOT_PRESENT: name = "extension not present"; break;
  case VK_ERROR_FEATURE_NOT_PRESENT:   name = "feature not present"; break;
  case VK_ERROR_INCOMPATIBLE_DRIVER:   name = "incompatible driver"; break;
  case VK_ERROR_SURFACE_LOST_KHR:      name = "surface lost"; break;
  case VK_ERROR_OUT_OF_DATE_KHR:       name = "out of date"; break;
  default:                             name = "unknown error"; break;
  }

  log_error("%s failed: %s (%d)", what, name, (int) result);
  return FALSE;
}

#define VK_CALL(expr)                                             \
  do {                                                            \
    VkResult result__ = (expr);                                   \
    if (result__ != VK_SUCCESS) {                                 \
      return vk3d_fail(#expr, result__);                          \
    }                                                             \
  } while (0)

/**********************************************************************//**
  Find a memory type acceptable for both the given buffer/image features
  and the wanted properties.
**************************************************************************/
static uint32_t vk3d_find_memory_type(uint32_t type_bits,
                                      VkMemoryPropertyFlags props)
{
  VkPhysicalDeviceMemoryProperties mem_props;

  vkGetPhysicalDeviceMemoryProperties(vk3d.physical_device, &mem_props);

  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
    if ((type_bits & (1u << i))
        && (mem_props.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }

  log_error("No suitable Vulkan memory type found.");
  return UINT32_MAX;
}

/**********************************************************************//**
  Create the VkInstance with the extensions SDL3 needs to hand us a
  window surface.
**************************************************************************/
static bool vk3d_create_instance(void)
{
  uint32_t count = 0;
  const char *const *sdl_exts;
  const char *exts[8];
  uint32_t n_exts = 0;
  VkApplicationInfo app_info;
  VkInstanceCreateInfo info;

  if (!SDL_Vulkan_LoadLibrary(NULL)) {
    log_error("Failed to load Vulkan loader: %s", SDL_GetError());
    return FALSE;
  }

  sdl_exts = SDL_Vulkan_GetInstanceExtensions(&count);
  for (uint32_t i = 0; i < count && n_exts < 6; i++) {
    exts[n_exts++] = sdl_exts[i];
  }

  app_info = (VkApplicationInfo) {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "Freeciv Vulkan client",
    .applicationVersion = VK_MAKE_VERSION(3, 0, 0),
    .pEngineName = "Freeciv",
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_1
  };

  info = (VkInstanceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
    .enabledExtensionCount = n_exts,
    .ppEnabledExtensionNames = exts
  };

  VK_CALL(vkCreateInstance(&info, nullptr, &vk3d.instance));
  return TRUE;
}

/**********************************************************************//**
  Score a physical device so that discrete GPUs win when several are
  present (laptop Optimus style setups).
**************************************************************************/
static int vk3d_score_device(VkPhysicalDevice device, uint32_t *queue_family)
{
  uint32_t count = 0;
  int score = 0;

  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  if (count == 0) {
    return -1;
  }

  VkQueueFamilyProperties *props = fc_malloc(sizeof(*props) * count);

  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

  for (uint32_t i = 0; i < count; i++) {
    VkBool32 present = VK_FALSE;

    if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      continue;
    }

    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vk3d.surface, &present);
    if (present == VK_TRUE) {
      *queue_family = i;
      score = 1;
      break;
    }
  }

  if (score < 0) {
    for (uint32_t i = 0; i < count; i++) {
      if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        *queue_family = i;
        score = 0;
        break;
      }
    }
  }

  if (score >= 0) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score += 100;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      score += 50;
    }
    score += (int) (properties.limits.maxImageDimension2D / 1024);
  }

  free(props);
  return score;
}

/**********************************************************************//**
  Pick the GPU to render on.
**************************************************************************/
static bool vk3d_pick_physical_device(void)
{
  uint32_t count = 0;
  int best = -1;

  VK_CALL(vkEnumeratePhysicalDevices(vk3d.instance, &count, nullptr));
  if (count == 0) {
    log_error("No Vulkan capable GPU found.");
    return FALSE;
  }

  VkPhysicalDevice *devices = fc_malloc(sizeof(*devices) * count);
  vkEnumeratePhysicalDevices(vk3d.instance, &count, devices);

  for (uint32_t i = 0; i < count; i++) {
    uint32_t family = 0;
    int score = vk3d_score_device(devices[i], &family);

    if (score > best) {
      best = score;
      vk3d.physical_device = devices[i];
      vk3d.queue_family_index = family;
    }
  }

  free(devices);

  if (best < 0) {
    log_error("No Vulkan GPU with a presentable queue for this window.");
    return FALSE;
  }

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(vk3d.physical_device, &properties);
  log_normal(_("Using Vulkan device: %s"), properties.deviceName);
  return TRUE;
}

/**********************************************************************//**
  Does the chosen GPU expose the swapchain extension?
**************************************************************************/
static bool vk3d_surface_supported(void)
{
  uint32_t count = 0;

  vkEnumerateDeviceExtensionProperties(vk3d.physical_device, nullptr,
                                       &count, nullptr);

  VkExtensionProperties *exts = fc_malloc(sizeof(*exts) * count);
  vkEnumerateDeviceExtensionProperties(vk3d.physical_device, nullptr,
                                       &count, exts);

  bool found = FALSE;
  for (uint32_t i = 0; i < count; i++) {
    if (strcmp(exts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
      found = TRUE;
      break;
    }
  }

  free(exts);
  return found;
}

/**********************************************************************//**
  Logical device with a single graphics+present queue.
**************************************************************************/
static bool vk3d_create_device(void)
{
  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = vk3d.queue_family_index,
    .queueCount = 1,
    .pQueuePriorities = &priority
  };
  VkDeviceCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &queue_info,
    .enabledExtensionCount = 1,
    .ppEnabledExtensionNames = &ext
  };

  VK_CALL(vkCreateDevice(vk3d.physical_device, &info, nullptr, &vk3d.device));
  vkGetDeviceQueue(vk3d.device, vk3d.queue_family_index, 0, &vk3d.queue);
  return TRUE;
}

/**********************************************************************//**
  Vulkan format matching the byte order of the SDL widget surface.
**************************************************************************/
static VkFormat vk3d_sdl_surface_format(void)
{
  const SDL_PixelFormatDetails *details
    = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);

  if (details != nullptr && details->Rmask == 0x00ff0000) {
    /* Bytes are B,G,R,A in memory. */
    return VK_FORMAT_B8G8R8A8_UNORM;
  }
  return VK_FORMAT_R8G8B8A8_UNORM;
}

/**********************************************************************//**
  Choose the surface format. We want an 8 bit UNORM format so that the
  already sRGB encoded GUI surface bytes pass through untouched.
**************************************************************************/
static bool vk3d_pick_surface_format(VkFormat *format)
{
  uint32_t count = 0;
  VkFormat want = vk3d_sdl_surface_format();

  VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk3d.physical_device,
                                              vk3d.surface, &count, nullptr));
  VkSurfaceFormatKHR *formats = fc_malloc(sizeof(*formats) * count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk3d.physical_device, vk3d.surface,
                                       &count, formats);

  bool found = FALSE;
  for (uint32_t i = 0; i < count; i++) {
    if (formats[i].format == want
        && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      *format = formats[i].format;
      found = TRUE;
      break;
    }
  }
  if (!found) {
    for (uint32_t i = 0; i < count; i++) {
      if (formats[i].format != VK_FORMAT_UNDEFINED) {
        *format = formats[i].format;
        found = TRUE;
        break;
      }
    }
  }

  free(formats);
  return found;
}

/**********************************************************************//**
  (Re)create the swapchain, its views, the render pass and framebuffers.
**************************************************************************/
static bool vk3d_create_swapchain(void)
{
  VkSurfaceCapabilitiesKHR caps;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkSwapchainCreateInfoKHR info;
  uint32_t wanted = 3;

  VK_CALL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk3d.physical_device,
                                                   vk3d.surface, &caps));

  if (caps.currentExtent.width != UINT32_MAX) {
    vk3d.swapchain_extent = caps.currentExtent;
  } else {
    vk3d.swapchain_extent.width = (uint32_t) MAX(vk3d.width, 1);
    vk3d.swapchain_extent.height = (uint32_t) MAX(vk3d.height, 1);
  }

  if (caps.minImageCount > wanted) {
    wanted = caps.minImageCount;
  }
  if (caps.maxImageCount > 0 && wanted > caps.maxImageCount) {
    wanted = caps.maxImageCount;
  }

  if (!vk3d_pick_surface_format(&vk3d.swapchain_format)) {
    log_error("Surface exposes no usable Vulkan format.");
    return FALSE;
  }

  uint32_t pm_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(vk3d.physical_device,
                                           vk3d.surface, &pm_count, nullptr);
  if (pm_count > 0) {
    VkPresentModeKHR *modes = fc_malloc(sizeof(*modes) * pm_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk3d.physical_device,
                                             vk3d.surface, &pm_count, modes);
    bool fifo = FALSE;
    for (uint32_t i = 0; i < pm_count; i++) {
      if (modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
        fifo = TRUE;
      }
    }
    if (!fifo) {
      present_mode = modes[0];
    }
    free(modes);
  }

  info = (VkSwapchainCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = vk3d.surface,
    .minImageCount = wanted,
    .imageFormat = vk3d.swapchain_format,
    .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    .imageExtent = vk3d.swapchain_extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .preTransform = caps.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = present_mode,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE
  };

  VK_CALL(vkCreateSwapchainKHR(vk3d.device, &info, nullptr, &vk3d.swapchain));

  vkGetSwapchainImagesKHR(vk3d.device, vk3d.swapchain,
                          &vk3d.swapchain_image_count, nullptr);
  if (vk3d.swapchain_image_count > VK3D_MAX_SWAPCHAIN_IMAGES) {
    vk3d.swapchain_image_count = VK3D_MAX_SWAPCHAIN_IMAGES;
  }
  vkGetSwapchainImagesKHR(vk3d.device, vk3d.swapchain,
                          &vk3d.swapchain_image_count, vk3d.swapchain_images);

  /* Render pass: one colour attachment, cleared, straight to present. */
  VkAttachmentDescription color_attach = {
    .format = vk3d.swapchain_format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  };
  VkAttachmentReference color_ref = {
    .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };
  VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_ref
  };
  VkSubpassDependency deps[2] = {
    {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    },
    {
      .srcSubpass = 0,
      .dstSubpass = VK_SUBPASS_EXTERNAL,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = 0
    }
  };
  VkRenderPassCreateInfo rp_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &color_attach,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 2,
    .pDependencies = deps
  };

  VK_CALL(vkCreateRenderPass(vk3d.device, &rp_info, nullptr,
                             &vk3d.render_pass));

  for (uint32_t i = 0; i < vk3d.swapchain_image_count; i++) {
    VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = vk3d.swapchain_images[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = vk3d.swapchain_format,
      .components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                      VK_COMPONENT_SWIZZLE_IDENTITY,
                      VK_COMPONENT_SWIZZLE_IDENTITY,
                      VK_COMPONENT_SWIZZLE_IDENTITY },
      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };
    VK_CALL(vkCreateImageView(vk3d.device, &view_info, nullptr,
                              &vk3d.swapchain_views[i]));

    VkFramebufferCreateInfo fb_info = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = vk3d.render_pass,
      .attachmentCount = 1,
      .pAttachments = &vk3d.swapchain_views[i],
      .width = vk3d.swapchain_extent.width,
      .height = vk3d.swapchain_extent.height,
      .layers = 1
    };
    VK_CALL(vkCreateFramebuffer(vk3d.device, &fb_info, nullptr,
                                &vk3d.framebuffers[i]));
  }

  return TRUE;
}

/**********************************************************************//**
  Free swapchain level objects. The VkSurfaceKHR survives.
**************************************************************************/
static void vk3d_destroy_swapchain(void)
{
  if (vk3d.device == VK_NULL_HANDLE) {
    return;
  }

  vkDeviceWaitIdle(vk3d.device);

  for (uint32_t i = 0; i < VK3D_MAX_SWAPCHAIN_IMAGES; i++) {
    if (vk3d.framebuffers[i] != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(vk3d.device, vk3d.framebuffers[i], nullptr);
      vk3d.framebuffers[i] = VK_NULL_HANDLE;
    }
    if (vk3d.swapchain_views[i] != VK_NULL_HANDLE) {
      vkDestroyImageView(vk3d.device, vk3d.swapchain_views[i], nullptr);
      vk3d.swapchain_views[i] = VK_NULL_HANDLE;
    }
  }

  if (vk3d.swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(vk3d.device, vk3d.swapchain, nullptr);
    vk3d.swapchain = VK_NULL_HANDLE;
  }
  if (vk3d.render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(vk3d.device, vk3d.render_pass, nullptr);
    vk3d.render_pass = VK_NULL_HANDLE;
  }
}

/**********************************************************************//**
  Create a host visible buffer.
**************************************************************************/
static bool vk3d_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags props,
                               VkBuffer *buffer, VkDeviceMemory *memory)
{
  VkBufferCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = (VkDeviceSize) MAX(size, 16),
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE
  };

  VK_CALL(vkCreateBuffer(vk3d.device, &info, nullptr, buffer));

  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(vk3d.device, *buffer, &req);

  uint32_t type = vk3d_find_memory_type(req.memoryTypeBits, props);
  if (type == UINT32_MAX) {
    return FALSE;
  }

  VkMemoryAllocateInfo alloc = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = req.size,
    .memoryTypeIndex = type
  };
  VK_CALL(vkAllocateMemory(vk3d.device, &alloc, nullptr, memory));
  VK_CALL(vkBindBufferMemory(vk3d.device, *buffer, *memory, 0));
  return TRUE;
}

/**********************************************************************//**
  Create the GUI layer texture and its staging buffer.
**************************************************************************/
static bool vk3d_create_overlay(uint32_t width, uint32_t height)
{
  VkImageCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = vk3d.overlay_format,
    .extent = { width, height, 1 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
  };

  VK_CALL(vkCreateImage(vk3d.device, &info, nullptr, &vk3d.overlay_image));

  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(vk3d.device, vk3d.overlay_image, &req);

  uint32_t type = vk3d_find_memory_type(req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) {
    return FALSE;
  }

  VkMemoryAllocateInfo alloc = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = req.size,
    .memoryTypeIndex = type
  };
  VK_CALL(vkAllocateMemory(vk3d.device, &alloc, nullptr,
                           &vk3d.overlay_memory));
  VK_CALL(vkBindImageMemory(vk3d.device, vk3d.overlay_image,
                            vk3d.overlay_memory, 0));

  VkImageViewCreateInfo view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = vk3d.overlay_image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = vk3d.overlay_format,
    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };
  VK_CALL(vkCreateImageView(vk3d.device, &view_info, nullptr,
                            &vk3d.overlay_view));

  vk3d.overlay_width = width;
  vk3d.overlay_height = height;
  vk3d.overlay_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  vk3d.overlay_dirty = TRUE;
  return TRUE;
}

static void vk3d_destroy_overlay(void)
{
  if (vk3d.overlay_view != VK_NULL_HANDLE) {
    vkDestroyImageView(vk3d.device, vk3d.overlay_view, nullptr);
    vk3d.overlay_view = VK_NULL_HANDLE;
  }
  if (vk3d.overlay_image != VK_NULL_HANDLE) {
    vkDestroyImage(vk3d.device, vk3d.overlay_image, nullptr);
    vk3d.overlay_image = VK_NULL_HANDLE;
  }
  if (vk3d.overlay_memory != VK_NULL_HANDLE) {
    vkFreeMemory(vk3d.device, vk3d.overlay_memory, nullptr);
    vk3d.overlay_memory = VK_NULL_HANDLE;
  }
}

/**********************************************************************//**
  Load a SPIR-V module embedded from the shaders directory.
**************************************************************************/
static bool vk3d_create_shader(const uint32_t *code, size_t code_size,
                               VkShaderModule *module)
{
  VkShaderModuleCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = code_size,
    .pCode = code
  };

  VK_CALL(vkCreateShaderModule(vk3d.device, &info, nullptr, module));
  return TRUE;
}

/**********************************************************************//**
  The overlay pipeline: fullscreen triangle sampling the GUI layer.
  Opaque for now; alpha blending arrives with the transparent map
  viewport (milestone M4 in PLAN.md).
**************************************************************************/
static bool vk3d_create_pipeline(void)
{
  VkVertexInputBindingDescription binding = {
    .binding = 0,
    .stride = 4 * sizeof(float),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
  };
  VkVertexInputAttributeDescription attributes[2] = {
    { .location = 0, .binding = 0,
      .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0 },
    { .location = 1, .binding = 0,
      .format = VK_FORMAT_R32G32_SFLOAT, .offset = 2 * sizeof(float) }
  };
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
  };
  VkViewport viewport = {
    .x = 0.0f, .y = 0.0f,
    .width = (float) vk3d.swapchain_extent.width,
    .height = (float) vk3d.swapchain_extent.height,
    .minDepth = 0.0f, .maxDepth = 1.0f
  };
  VkRect2D scissor = {
    .offset = { 0, 0 }, .extent = vk3d.swapchain_extent
  };
  VkPipelineViewportStateCreateInfo viewport_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports = &viewport,
    .scissorCount = 1,
    .pScissors = &scissor
  };
  VkPipelineRasterizationStateCreateInfo rasterizer = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f
  };
  VkPipelineMultisampleStateCreateInfo multisampling = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
  };
  VkPipelineColorBlendAttachmentState blend_attach = {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    .blendEnable = VK_FALSE
  };
  VkPipelineColorBlendStateCreateInfo blending = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blend_attach
  };
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
  };
  VkPipelineDynamicStateCreateInfo dynamic = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = (VkDynamicState[]) { VK_DYNAMIC_STATE_VIEWPORT,
                                           VK_DYNAMIC_STATE_SCISSOR }
  };
  VkGraphicsPipelineCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = 2,
    .pStages = (VkPipelineShaderStageCreateInfo[]) {
      { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vk3d.vertex_shader, .pName = "main" },
      { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = vk3d.fragment_shader, .pName = "main" }
    },
    .pVertexInputState = &(VkPipelineVertexInputStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding,
      .vertexAttributeDescriptionCount = 2,
      .pVertexAttributeDescriptions = attributes
    },
    .pInputAssemblyState = &input_assembly,
    .pViewportState = &viewport_state,
    .pRasterizationState = &rasterizer,
    .pMultisampleState = &multisampling,
    .pDepthStencilState = &depth_stencil,
    .pColorBlendState = &blending,
    .pDynamicState = &dynamic,
    .layout = vk3d.pipeline_layout,
    .renderPass = vk3d.render_pass,
    .subpass = 0
  };

  VK_CALL(vkCreateGraphicsPipelines(vk3d.device, VK_NULL_HANDLE, 1, &info,
                                    nullptr, &vk3d.pipeline));
  return TRUE;
}

/**********************************************************************//**
  Descriptor set holding the GUI layer texture.
**************************************************************************/
static bool vk3d_create_descriptor(void)
{
  VkDescriptorSetLayoutBinding layout_binding = {
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
  };
  VkDescriptorSetLayoutCreateInfo layout_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 1,
    .pBindings = &layout_binding
  };
  VK_CALL(vkCreateDescriptorSetLayout(vk3d.device, &layout_info, nullptr,
                                      &vk3d.descriptor_layout));

  VkPipelineLayoutCreateInfo pipeline_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &vk3d.descriptor_layout
  };
  VK_CALL(vkCreatePipelineLayout(vk3d.device, &pipeline_info, nullptr,
                                 &vk3d.pipeline_layout));

  VkSamplerCreateInfo sampler_info = {
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
  };
  VK_CALL(vkCreateSampler(vk3d.device, &sampler_info, nullptr, &vk3d.sampler));

  VkDescriptorPoolSize pool_size = {
    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1
  };
  VkDescriptorPoolCreateInfo pool_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 1,
    .pPoolSizes = &pool_size
  };
  VK_CALL(vkCreateDescriptorPool(vk3d.device, &pool_info, nullptr,
                                 &vk3d.descriptor_pool));

  VkDescriptorSetAllocateInfo alloc = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = vk3d.descriptor_pool,
    .descriptorSetCount = 1,
    .pSetLayouts = &vk3d.descriptor_layout
  };
  VK_CALL(vkAllocateDescriptorSets(vk3d.device, &alloc, &vk3d.descriptor_set));
  return TRUE;
}

/**********************************************************************//**
  Point the descriptor set at the current GUI layer image view.
**************************************************************************/
static void vk3d_update_descriptor(void)
{
  VkDescriptorImageInfo image_info = {
    .sampler = vk3d.sampler,
    .imageView = vk3d.overlay_view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  };
  VkWriteDescriptorSet write = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = vk3d.descriptor_set,
    .dstBinding = 0,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = &image_info
  };

  vkUpdateDescriptorSets(vk3d.device, 1, &write, 0, nullptr);
}

/**********************************************************************//**
  Everything that does not depend on the swapchain extent.
**************************************************************************/
static bool vk3d_create_frame_resources(void)
{
  VkCommandPoolCreateInfo pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = vk3d.queue_family_index
  };
  VK_CALL(vkCreateCommandPool(vk3d.device, &pool_info, nullptr,
                              &vk3d.command_pool));

  if (!vk3d_create_shader(vk_shader_overlay_vert,
                          vk_shader_overlay_vert_size,
                          &vk3d.vertex_shader)) {
    return FALSE;
  }
  if (!vk3d_create_shader(vk_shader_overlay_frag,
                          vk_shader_overlay_frag_size,
                          &vk3d.fragment_shader)) {
    return FALSE;
  }

  if (!vk3d_create_buffer(sizeof(vk3d_quad_vertices),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk3d.vertex_buffer, &vk3d.vertex_buffer_memory)) {
    return FALSE;
  }

  {
    void *data = nullptr;
    VK_CALL(vkMapMemory(vk3d.device, vk3d.vertex_buffer_memory, 0,
                        sizeof(vk3d_quad_vertices), 0, &data));
    memcpy(data, vk3d_quad_vertices, sizeof(vk3d_quad_vertices));
    vkUnmapMemory(vk3d.device, vk3d.vertex_buffer_memory);
  }

  if (!vk3d_create_descriptor()) {
    return FALSE;
  }

  for (uint32_t i = 0; i < VK3D_MAX_FRAMES_IN_FLIGHT; i++) {
    VkSemaphoreCreateInfo semaphore_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    VK_CALL(vkCreateSemaphore(vk3d.device, &semaphore_info, nullptr,
                              &vk3d.image_available[i]));
    VK_CALL(vkCreateSemaphore(vk3d.device, &semaphore_info, nullptr,
                              &vk3d.render_finished[i]));
    VK_CALL(vkCreateFence(vk3d.device, &fence_info, nullptr,
                          &vk3d.frames_in_flight[i]));
  }

  VkCommandBufferAllocateInfo cmd_alloc = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vk3d.command_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = VK3D_MAX_FRAMES_IN_FLIGHT
  };
  VK_CALL(vkAllocateCommandBuffers(vk3d.device, &cmd_alloc,
                                   vk3d.command_buffers));
  return TRUE;
}

static void vk3d_destroy_frame_resources(void)
{
  if (vk3d.device == VK_NULL_HANDLE) {
    return;
  }

  vkDeviceWaitIdle(vk3d.device);

  for (uint32_t i = 0; i < VK3D_MAX_FRAMES_IN_FLIGHT; i++) {
    if (vk3d.image_available[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(vk3d.device, vk3d.image_available[i], nullptr);
      vk3d.image_available[i] = VK_NULL_HANDLE;
    }
    if (vk3d.render_finished[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(vk3d.device, vk3d.render_finished[i], nullptr);
      vk3d.render_finished[i] = VK_NULL_HANDLE;
    }
    if (vk3d.frames_in_flight[i] != VK_NULL_HANDLE) {
      vkDestroyFence(vk3d.device, vk3d.frames_in_flight[i], nullptr);
      vk3d.frames_in_flight[i] = VK_NULL_HANDLE;
    }
  }

  if (vk3d.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(vk3d.device, vk3d.pipeline, nullptr);
    vk3d.pipeline = VK_NULL_HANDLE;
  }
  if (vk3d.pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(vk3d.device, vk3d.pipeline_layout, nullptr);
    vk3d.pipeline_layout = VK_NULL_HANDLE;
  }
  if (vk3d.descriptor_pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(vk3d.device, vk3d.descriptor_pool, nullptr);
    vk3d.descriptor_pool = VK_NULL_HANDLE;
  }
  if (vk3d.descriptor_layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(vk3d.device, vk3d.descriptor_layout, nullptr);
    vk3d.descriptor_layout = VK_NULL_HANDLE;
  }
  if (vk3d.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(vk3d.device, vk3d.sampler, nullptr);
    vk3d.sampler = VK_NULL_HANDLE;
  }
  vk3d_destroy_overlay();
  if (vk3d.staging_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(vk3d.device, vk3d.staging_buffer, nullptr);
    vk3d.staging_buffer = VK_NULL_HANDLE;
  }
  if (vk3d.staging_memory != VK_NULL_HANDLE) {
    vkFreeMemory(vk3d.device, vk3d.staging_memory, nullptr);
    vk3d.staging_memory = VK_NULL_HANDLE;
  }
  if (vk3d.vertex_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(vk3d.device, vk3d.vertex_buffer, nullptr);
    vk3d.vertex_buffer = VK_NULL_HANDLE;
  }
  if (vk3d.vertex_buffer_memory != VK_NULL_HANDLE) {
    vkFreeMemory(vk3d.device, vk3d.vertex_buffer_memory, nullptr);
    vk3d.vertex_buffer_memory = VK_NULL_HANDLE;
  }
  if (vk3d.vertex_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(vk3d.device, vk3d.vertex_shader, nullptr);
    vk3d.vertex_shader = VK_NULL_HANDLE;
  }
  if (vk3d.fragment_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(vk3d.device, vk3d.fragment_shader, nullptr);
    vk3d.fragment_shader = VK_NULL_HANDLE;
  }
  if (vk3d.command_pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(vk3d.device, vk3d.command_pool, nullptr);
    vk3d.command_pool = VK_NULL_HANDLE;
  }
}

/**********************************************************************//**
  Make sure the staging buffer can hold a width x height RGBA image.
**************************************************************************/
static bool vk3d_ensure_staging(VkDeviceSize size)
{
  if (vk3d.staging_size >= size) {
    return TRUE;
  }

  if (vk3d.staging_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(vk3d.device, vk3d.staging_buffer, nullptr);
    vk3d.staging_buffer = VK_NULL_HANDLE;
  }
  if (vk3d.staging_memory != VK_NULL_HANDLE) {
    vkFreeMemory(vk3d.device, vk3d.staging_memory, nullptr);
    vk3d.staging_memory = VK_NULL_HANDLE;
  }

  if (!vk3d_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk3d.staging_buffer, &vk3d.staging_memory)) {
    return FALSE;
  }

  vk3d.staging_size = (VkDeviceSize) MAX(size, 16);
  return TRUE;
}

/**********************************************************************//**
  Upload the 2D widget layer into the overlay texture.
**************************************************************************/
static bool vk3d_upload_overlay(const SDL_Surface *gui)
{
  VkDeviceSize size = (VkDeviceSize) gui->w * gui->h * 4;
  uint32_t width = (uint32_t) gui->w;
  uint32_t height = (uint32_t) gui->h;

  if (width == 0 || height == 0) {
    return TRUE;
  }

  if (vk3d.overlay_image != VK_NULL_HANDLE
      && (vk3d.overlay_width != width || vk3d.overlay_height != height)) {
    vkDeviceWaitIdle(vk3d.device);
    vk3d_destroy_overlay();
  }

  if (vk3d.overlay_image == VK_NULL_HANDLE) {
    if (!vk3d_create_overlay(width, height)) {
      return FALSE;
    }
    vk3d_update_descriptor();
  }

  if (!vk3d_ensure_staging(size)) {
    return FALSE;
  }

  {
    void *data = nullptr;
    VK_CALL(vkMapMemory(vk3d.device, vk3d.staging_memory, 0, size, 0, &data));
    if (gui->pitch == (int) (width * 4)) {
      memcpy(data, gui->pixels, (size_t) size);
    } else {
      for (int y = 0; y < height; y++) {
        memcpy((char *) data + (size_t) y * width * 4,
               (const char *) gui->pixels + (size_t) y * gui->pitch,
               (size_t) width * 4);
      }
    }
    vkUnmapMemory(vk3d.device, vk3d.staging_memory);
  }

  vk3d.overlay_dirty = FALSE;
  return TRUE;
}

/**********************************************************************//**
  Record the command buffer for one frame.
**************************************************************************/
static bool vk3d_record(uint32_t image_index, uint32_t frame)
{
  VkCommandBuffer cmd = vk3d.command_buffers[frame];
  VkClearColorValue clear_color = { { vk3d_sky_rgba[0], vk3d_sky_rgba[1],
                                      vk3d_sky_rgba[2], vk3d_sky_rgba[3] } };
  VkClearValue clear = { .color = clear_color };
  VkRenderPassBeginInfo rp_begin = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = vk3d.render_pass,
    .framebuffer = vk3d.framebuffers[image_index],
    .renderArea = { { 0, 0 }, vk3d.swapchain_extent },
    .clearValueCount = 1,
    .pClearValues = &clear
  };

  vkResetCommandBuffer(cmd, 0);

  VkCommandBufferBeginInfo begin = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
  };
  VK_CALL(vkBeginCommandBuffer(cmd, &begin));

  if (vk3d.overlay_image != VK_NULL_HANDLE) {
    VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = vk3d.overlay_layout,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = vk3d.overlay_image,
      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };
    VkPipelineStageFlags src_stage;

    if (vk3d.overlay_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
      barrier.srcAccessMask = 0;
      src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    } else {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {
      .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      .imageExtent = { vk3d.overlay_width, vk3d.overlay_height, 1 }
    };
    vkCmdCopyBufferToImage(cmd, vk3d.staging_buffer, vk3d.overlay_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    vk3d.overlay_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

  /* M2+: the lit voxel map scene is drawn here, before the GUI layer. */
  vk3d_map_record(cmd);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk3d.pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vk3d.pipeline_layout, 0, 1,
                          &vk3d.descriptor_set, 0, nullptr);
  {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vk3d.vertex_buffer, &offset);
  }
  vkCmdSetViewport(cmd, 0, 1, &(VkViewport) {
    .x = 0.0f, .y = 0.0f,
    .width = (float) vk3d.swapchain_extent.width,
    .height = (float) vk3d.swapchain_extent.height,
    .minDepth = 0.0f, .maxDepth = 1.0f
  });
  vkCmdSetScissor(cmd, 0, 1, &(VkRect2D) { .offset = { 0, 0 },
                                           .extent = vk3d.swapchain_extent });
  vkCmdDraw(cmd, 3, 1, 0, 0);

  vkCmdEndRenderPass(cmd);
  VK_CALL(vkEndCommandBuffer(cmd));
  return TRUE;
}

/**********************************************************************//**
  Public: is the Vulkan path live?
**************************************************************************/
bool vk3d_is_active(void)
{
  return vk3d.active;
}

/**********************************************************************//**
  Public: bring up Vulkan on an SDL_WINDOW_VULKAN window.
**************************************************************************/
bool vk3d_init(SDL_Window *window, int width, int height)
{
  memset(&vk3d, 0, sizeof(vk3d));
  vk3d.window = window;
  vk3d.width = width;
  vk3d.height = height;

  if (!vk3d_create_instance()) {
    goto fail;
  }

  if (!SDL_Vulkan_CreateSurface(window, vk3d.instance, nullptr,
                                &vk3d.surface)) {
    log_error("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
    goto fail;
  }

  if (!vk3d_pick_physical_device()) {
    goto fail;
  }
  if (!vk3d_surface_supported()) {
    log_error("GPU does not expose the Vulkan swapchain extension.");
    goto fail;
  }
  if (!vk3d_create_device()) {
    goto fail;
  }

  /* The widget layer is an SDL_PIXELFORMAT_RGBA8888 surface; pick the
   * Vulkan format with the matching byte order. */
  vk3d.overlay_format = vk3d_sdl_surface_format();

  if (!vk3d_create_swapchain()) {
    goto fail;
  }
  if (!vk3d_create_frame_resources()) {
    goto fail;
  }
  if (!vk3d_create_pipeline()) {
    goto fail;
  }

  vk3d.active = TRUE;
  return TRUE;

 fail:
  vk3d_shutdown();
  return FALSE;
}

/**********************************************************************//**
  Public: tear everything down.
**************************************************************************/
void vk3d_shutdown(void)
{
  vk3d_destroy_swapchain();
  vk3d_destroy_frame_resources();

  if (vk3d.device != VK_NULL_HANDLE) {
    vkDestroyDevice(vk3d.device, nullptr);
    vk3d.device = VK_NULL_HANDLE;
  }
  if (vk3d.surface != VK_NULL_HANDLE && vk3d.instance != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(vk3d.instance, vk3d.surface, nullptr);
    vk3d.surface = VK_NULL_HANDLE;
  }
  if (vk3d.instance != VK_NULL_HANDLE) {
    vkDestroyInstance(vk3d.instance, nullptr);
    vk3d.instance = VK_NULL_HANDLE;
  }

  SDL_Vulkan_UnloadLibrary();
  memset(&vk3d, 0, sizeof(vk3d));
}

/**********************************************************************//**
  Public: window resized.
**************************************************************************/
void vk3d_resize(int width, int height)
{
  if (!vk3d.active || width <= 0 || height <= 0) {
    return;
  }

  if (width == vk3d.width && height == vk3d.height) {
    return;
  }

  vk3d.width = width;
  vk3d.height = height;

  vkDeviceWaitIdle(vk3d.device);
  vk3d_destroy_swapchain();
  /* The pipeline bakes a viewport extent; recreate it too. */
  if (vk3d.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(vk3d.device, vk3d.pipeline, nullptr);
    vk3d.pipeline = VK_NULL_HANDLE;
  }
  if (!vk3d_create_swapchain() || !vk3d_create_pipeline()) {
    log_error("Failed to rebuild the Vulkan swapchain after a resize.");
    vk3d.active = FALSE;
  }
}

/**********************************************************************//**
  Public: the 2D layer changed and has to be re-uploaded.
**************************************************************************/
void vk3d_gui_dirty(void)
{
  vk3d.overlay_dirty = TRUE;
}

/**********************************************************************//**
  Public: draw and present one frame.
**************************************************************************/
bool vk3d_present(const SDL_Surface *gui)
{
  uint32_t image_index = 0;
  VkResult result;
  uint32_t frame;
  VkSubmitInfo submit;
  VkPresentInfoKHR present;

  if (!vk3d.active) {
    return FALSE;
  }

  frame = vk3d.current_frame;
  vkWaitForFences(vk3d.device, 1, &vk3d.frames_in_flight[frame], VK_TRUE,
                  UINT64_MAX);

  result = vkAcquireNextImageKHR(vk3d.device, vk3d.swapchain, UINT64_MAX,
                                 vk3d.image_available[frame], VK_NULL_HANDLE,
                                 &image_index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    int w = vk3d.width, h = vk3d.height;
    vk3d.width = 0;
    vk3d.height = 0;
    vk3d_resize(w, h);
    return TRUE;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return vk3d_fail("vkAcquireNextImageKHR", result);
  }

  if (gui != nullptr) {
    /* The widget layer is only presented when it changed, so a full
     * re-upload here is cheap in practice. M4 replaces it with dirty
     * rectangle uploads. */
    if (!vk3d_upload_overlay(gui)) {
      return FALSE;
    }
  }

  if (!vk3d_record(image_index, frame)) {
    return FALSE;
  }

  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  submit = (VkSubmitInfo) {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &vk3d.image_available[frame],
    .pWaitDstStageMask = &wait_stage,
    .commandBufferCount = 1,
    .pCommandBuffers = &vk3d.command_buffers[frame],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &vk3d.render_finished[frame]
  };

  VK_CALL(vkResetFences(vk3d.device, 1, &vk3d.frames_in_flight[frame]));
  VK_CALL(vkQueueSubmit(vk3d.queue, 1, &submit, vk3d.frames_in_flight[frame]));

  present = (VkPresentInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &vk3d.render_finished[frame],
    .swapchainCount = 1,
    .pSwapchains = &vk3d.swapchain,
    .pImageIndices = &image_index
  };

  result = vkQueuePresentKHR(vk3d.queue, &present);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    int w = vk3d.width, h = vk3d.height;
    vk3d.width = 0;
    vk3d.height = 0;
    vk3d_resize(w, h);
  } else if (result != VK_SUCCESS) {
    return vk3d_fail("vkQueuePresentKHR", result);
  }

  vk3d.current_frame = (frame + 1) % VK3D_MAX_FRAMES_IN_FLIGHT;
  return TRUE;
}
