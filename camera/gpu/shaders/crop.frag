#version 310 es

precision highp float;

layout(binding = 0) uniform highp sampler2D uInputTexture;
layout(location = 1) uniform vec4 uCropRegion;

layout(location = 0) in highp vec2 vTexCoord;
layout(location = 0) out highp vec4 outColor;

void main() {
  float src_x = uCropRegion.x;
  float src_y = uCropRegion.y;
  float crop_width = uCropRegion.z;
  float crop_height = uCropRegion.w;
  vec2 sample_coord = vec2(
      src_x + vTexCoord.x * crop_width,
      src_y + vTexCoord.y * crop_height);
  outColor = texture2D(uInputTexture, sample_coord);
}
