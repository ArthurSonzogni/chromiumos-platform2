// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Needs to be manually kept in sync with
// //chromeos/ml/effects_pipeline/effects_pipeline_bindings.h

#ifndef ML_CORE_BINDINGS_EFFECTS_PIPELINE_BINDINGS_H_
#define ML_CORE_BINDINGS_EFFECTS_PIPELINE_BINDINGS_H_

#include <stdint.h>

#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#include "ml_core/mojo/effects_pipeline.mojom.h"

namespace cros {

// EffectsConfig is intended to be extended and used by the
// EffectsLibrary to build effects that would like more configurable
// options. Needs to be kept in sync with g3 version found in
// chromeos/ml/effects_pipeline/effects_pipeline.h
struct BRILLO_EXPORT EffectsConfig {
  // Whether portrait relighting should be enabled.
  bool relight_enabled = false;
  // Whether background blur should be enabled
  bool blur_enabled = false;
  // Whether background replace should be enabled
  bool replace_enabled = false;

  bool HasEnabledEffects() const {
    return blur_enabled || relight_enabled || replace_enabled;
  }

  // How much blur to apply for the background blur effect.
  mojom::BlurLevel blur_level = mojom::BlurLevel::kMedium;

  // Select which GPU API to use to perform the segmentation inference
  mojom::GpuApi segmentation_gpu_api = mojom::GpuApi::kOpenGL;

  // Select which GPU API to use to perform the relighting inference
  mojom::GpuApi relighting_gpu_api = mojom::GpuApi::kOpenGL;

  // Maximum number of frames allowed in flight.
  int graph_max_frames_in_flight = 2;

  // Enable mediapipe profiling.
  // Must be built with --define DRISHTI_PROFILING=1
  bool enable_profiling = false;

  // Run models to position light automatically.
  bool enable_auto_light_pos = true;

  // Wait for rendering to complete in the mediapipe graph.
  bool wait_on_render = false;

  inline bool operator==(const EffectsConfig& rhs) const {
    return blur_level == rhs.blur_level &&
           segmentation_gpu_api == rhs.segmentation_gpu_api &&
           graph_max_frames_in_flight == rhs.graph_max_frames_in_flight &&
           enable_profiling == rhs.enable_profiling &&
           relight_enabled == rhs.relight_enabled &&
           blur_enabled == rhs.blur_enabled &&
           replace_enabled == rhs.replace_enabled &&
           relighting_gpu_api == rhs.relighting_gpu_api &&
           enable_profiling == rhs.enable_profiling &&
           enable_auto_light_pos == rhs.enable_auto_light_pos &&
           wait_on_render == rhs.wait_on_render;
  }

  inline bool operator!=(const EffectsConfig& rhs) const {
    return !(*this == rhs);
  }
};
}  // namespace cros

// This is the set of C bindings exported for the library
extern "C" {
typedef void (*cros_ml_effects_OnFrameProcessedHandler)(void* handler,
                                                        int64_t timestamp,
                                                        GLuint frame_texture,
                                                        uint32_t frame_width,
                                                        uint32_t frame_height);

void* cros_ml_effects_CreateEffectsPipeline(EGLContext share_context,
                                            const char* caching_dir);
typedef decltype(&cros_ml_effects_CreateEffectsPipeline)
    cros_ml_effects_CreateEffectsPipelineFn;

void cros_ml_effects_DeleteEffectsPipeline(void* pipeline);
typedef decltype(&cros_ml_effects_DeleteEffectsPipeline)
    cros_ml_effects_DeleteEffectsPipelineFn;

bool cros_ml_effects_ProcessFrame(void* pipeline,
                                  int64_t timestamp,
                                  GLuint frame_texture,
                                  uint32_t frame_width,
                                  uint32_t frame_height);
typedef decltype(&cros_ml_effects_ProcessFrame) cros_ml_effects_ProcessFrameFn;

bool cros_ml_effects_Wait(void* pipeline);
typedef decltype(&cros_ml_effects_Wait) cros_ml_effects_WaitFn;

bool cros_ml_effects_SetSegmentationMaskObserver(
    void* pipeline,
    void* observer,
    cros_ml_effects_OnFrameProcessedHandler frame_handler_fn);
typedef decltype(&cros_ml_effects_SetSegmentationMaskObserver)
    cros_ml_effects_SetSegmentationMaskObserverFn;

bool cros_ml_effects_SetRenderedImageObserver(
    void* pipeline,
    void* observer,
    cros_ml_effects_OnFrameProcessedHandler frame_handler_fn);
typedef decltype(&cros_ml_effects_SetRenderedImageObserver)
    cros_ml_effects_SetRenderedImageObserverFn;

void cros_ml_effects_SetEffect(void* pipeline,
                               cros::EffectsConfig* effects_config,
                               void (*callback)(bool));
typedef decltype(&cros_ml_effects_SetEffect) cros_ml_effects_SetEffectFn;
}

#endif  // ML_CORE_BINDINGS_EFFECTS_PIPELINE_BINDINGS_H_
