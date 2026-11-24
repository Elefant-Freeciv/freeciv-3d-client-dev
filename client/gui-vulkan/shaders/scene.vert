#version 450
/*
 * scene.vert - voxel map terrain vertex shader (milestone M2).
 *
 * Voxel geometry is generated on the CPU from the art catalogue in
 * voxel_art.c, exactly like the merged-box VoxelBuilder of the terrain
 * studio page: position, normal and a per-box colour, bucketed so that a
 * chunk is one or few draws.
 */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(push_constant) uniform ScenePush {
  mat4 view_proj;
  vec3 camera_pos;
  vec3 sun_dir;      /* Direction the key light shines towards. */
  float time;
  vec3 sun_color;
  float fog_density;
  vec3 fog_color;
  float tile_size;
} scene;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_pos;
layout(location = 3) out float out_fog;

void main()
{
  vec3 world = in_position;

  out_world_pos = world;
  out_normal = in_normal;
  out_color = in_color;

  float dist = distance(scene.camera_pos, world);
  out_fog = 1.0 - exp(-pow(dist * scene.fog_density, 2.0));

  gl_Position = scene.view_proj * vec4(world, 1.0);
}
