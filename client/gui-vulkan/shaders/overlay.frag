#version 450
/*
 * overlay.frag - draws the 2D widget layer over the 3D map.
 *
 * Milestone M1 blits it opaquely, which reproduces exactly what the SDL3
 * renderer path did. M4 switches the alpha channel on so that the map
 * viewport shows the 3D scene through a hole in the GUI surface.
 */

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D gui_layer;

void main()
{
  out_color = vec4(texture(gui_layer, in_uv).rgb, 1.0);
}
