#version 450
/*
 * scene.frag - voxel map terrain fragment shader (milestone M2/M3).
 *
 * Lighting model copies the terrain studio page: a warm directional key
 * light, a cool sky/ground hemisphere fill and a cool rim light, followed
 * by ACES filmic tone mapping. Colours arrive in linear space; the output
 * is encoded for the (non-sRGB) swapchain format by hand so that the
 * result matches the page's sRGB output.
 */

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_world_pos;
layout(location = 3) in float in_fog;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform ScenePush {
  mat4 view_proj;
  vec3 camera_pos;
  vec3 sun_dir;
  float time;
  vec3 sun_color;
  float fog_density;
  vec3 fog_color;
  float tile_size;
} scene;

const vec3 SKY_COLOR = vec3(0.933, 0.957, 1.0);   /* #eef4ff hemisphere top */
const vec3 GROUND_COLOR = vec3(0.148, 0.106, 0.067); /* #6e5a47 lower    */

vec3 aces_tone_map(vec3 x)
{
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linear_to_srgb(vec3 c)
{
  return mix(c * 12.92,
             1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
             step(vec3(0.0031308), c));
}

void main()
{
  vec3 n = normalize(in_normal);
  vec3 sun = normalize(-scene.sun_dir);

  /* Hemisphere fill: sky above, warm soil bounce below. */
  float hemi = 0.5 + 0.5 * n.y;
  vec3 fill = mix(GROUND_COLOR, SKY_COLOR, hemi) * 0.54;

  /* Key light with a wrap term so voxel side faces never go pitch black. */
  float ndl = max(dot(n, sun), 0.0);
  float wrap = max(dot(n, sun) * 0.7 + 0.3, 0.0);
  vec3 key = scene.sun_color * mix(ndl, wrap, 0.35) * 1.12;

  /* Cool rim from the opposite side, like the page's second light. */
  vec3 rim = vec3(0.70, 0.86, 1.0)
             * pow(max(dot(n, normalize(vec3(-0.55, 0.45, -0.7))), 0.0), 2.0)
             * 0.24;

  vec3 lit = in_color * (key + fill) + rim * 0.1;
  lit = aces_tone_map(lit * 0.98);
  lit = mix(lit, scene.fog_color, clamp(in_fog, 0.0, 1.0));

  out_color = vec4(linear_to_srgb(lit), 1.0);
}
