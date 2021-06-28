#version 310 es
#extension GL_OES_EGL_image_external: require

precision highp float;

layout(binding = 0) uniform highp samplerExternalOES uInputExternalYuvTexture;

layout(location = 0) in highp vec2 vTexCoord;
layout(location = 0) out highp vec4 outColor;

void main() {
  outColor = texture2D(uInputExternalYuvTexture, vTexCoord);
}
