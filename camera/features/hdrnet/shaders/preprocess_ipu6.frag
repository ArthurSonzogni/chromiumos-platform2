#version 310 es

precision highp float;

layout(binding = 0) uniform highp sampler2D uInputYTexture;
layout(binding = 1) uniform highp sampler2D uInputUvTexture;
layout(binding = 2) uniform highp sampler2D uInverseGammaLutTexture;
layout(binding = 3) uniform highp sampler2D uInverseGtmLutTexture;

layout(location = 0) in highp vec2 vTexCoord;
layout(location = 0) out highp vec4 outColor;

// Intel's GL ES implementation always samples the YUV image with narrow range
// color space and it's crushing the shadow areas on the images. Before we
// have a fix in the mesa, sample and covert the YUV image to RGB ourselves.
vec3 sample_input_as_rgb() {
  float y = texture2D(uInputYTexture, vTexCoord).r;
  float u = texture2D(uInputUvTexture, vTexCoord).r;
  float v = texture2D(uInputUvTexture, vTexCoord).g;

  return clamp(vec3(
    y + 1.4017 * (v - 0.5),
    y - 0.3437 * (u - 0.5) - 0.7142 * (v - 0.5),
    y + 1.7722 * (u - 0.5)
  ), 0.0, 1.0);
}

float max_rgb(vec3 v) {
  return max(max(v.r, v.g), v.b);
}

void main() {
  vec3 rgb = sample_input_as_rgb();

  // Apply inverse Gamma.
  vec3 gamma_inversed_rgb = vec3(
      texture2D(uInverseGammaLutTexture, vec2(rgb.r, 0.0)).r,
      texture2D(uInverseGammaLutTexture, vec2(rgb.g, 0.0)).r,
      texture2D(uInverseGammaLutTexture, vec2(rgb.b, 0.0)).r);

  // Apply inverse GTM.
  float max_value = max_rgb(gamma_inversed_rgb);
  float gain = texture2D(uInverseGtmLutTexture, vec2(max_value, 0.0)).r;
  outColor = vec4(gamma_inversed_rgb / gain, 1.0);
}
