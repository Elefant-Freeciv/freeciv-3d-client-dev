#version 450
/*
 * overlay.vert - fullscreen triangle carrying the 2D GUI layer UVs.
 *
 * One triangle covers the whole viewport; the viewport/scissor state
 * clips it. UV v is flipped because the SDL widget surface has its origin
 * in the top left corner while Vulkan's clip space Y grows downwards.
 */

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

void main()
{
  out_uv = in_uv;
  gl_Position = vec4(in_position, 0.0, 1.0);
}
