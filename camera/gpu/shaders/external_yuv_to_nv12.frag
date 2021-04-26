#version 320 es
#extension GL_OES_EGL_image_external: require

precision highp float;

layout(binding = 0) uniform highp samplerExternalOES uInputP010Texture;
layout(location = 0) uniform bool uIsYPlane;
layout(location = 1) uniform float uTexelWidth;

layout(location = 0) in highp vec2 vTexCoord;
layout(location = 0) out highp vec4 outColor;

void main() {
  vec3 rgb = texture2D(uInputP010Texture, vTexCoord).rgb;
  if (uIsYPlane) {
    float y = 0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b;
    outColor = vec4(y, 0.0, 0.0, 0.0);
  } else {
    float x = vTexCoord.x * uTexelWidth;
    if (mod(x, 2.0) < 1.0) {
      float u = -0.16874 * rgb.r - 0.33126 * rgb.g + 0.5 * rgb.b + 0.5;
      outColor = vec4(u, 0.0, 0.0, 0.0);
    } else {
      float v = 0.5 * rgb.r - 0.41869 * rgb.g - 0.08131 * rgb.b + 0.5;
      outColor = vec4(v, 0.0, 0.0, 0.0);
    }
  }
}
