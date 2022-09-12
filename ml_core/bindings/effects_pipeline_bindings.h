// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is copied directly from
// chromeos/ml/effects_pipeline/effects_pipeline_bindings.h
// and SHOULD NOT BE MODIFIED HERE. It will get delivered with the
// libcros_ml_core_internal.so bundle once that is set up.
//
// TODO(b/232703203): Make sure this file comes across with the lib.

#ifndef ML_CORE_BINDINGS_EFFECTS_PIPELINE_BINDINGS_H_
#define ML_CORE_BINDINGS_EFFECTS_PIPELINE_BINDINGS_H_

#include <stdint.h>
#include "ml_core/mojo/effects_pipeline.mojom.h"

namespace cros {

// EffectsConfig is intended to be extended and used by the
// EffectsLibrary to build effects that would like more configurable
// options. Needs to be kept in sync with g3 version found in
// chromeos/ml/effects_pipeline/effects_pipeline.h
struct BRILLO_EXPORT EffectsConfig {
  // Name of the effect. Used to identify which effect object to instantiate
  mojom::CameraEffect effect = mojom::CameraEffect::NONE;

  // The scale where the input image is blurred. If specified, the value must be
  // greater than 0.05. If not specified, the blur is at the resolution of the
  // input mask
  float blur_scale = 0.25;

  // Number of blur samples in one direction. Approximately how many pixels in
  // each direction to include to create the blur
  uint8_t blur_samples = 4;

  // Select which GPU API to use to perform the segmentation inference
  mojom::GpuApi segmentation_gpu_api = mojom::GpuApi::OPENGL;

  // Maximum number of frames allowed in flight.
  int graph_max_frames_in_flight = 2;

  inline bool operator==(const EffectsConfig& rhs) const {
    return effect == rhs.effect && blur_samples == rhs.blur_samples &&
           blur_scale == rhs.blur_scale &&
           segmentation_gpu_api == rhs.segmentation_gpu_api &&
           graph_max_frames_in_flight == rhs.graph_max_frames_in_flight;
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
                                                        const uint8_t* data,
                                                        uint32_t frame_width,
                                                        uint32_t frame_height,
                                                        uint32_t stride);

void* cros_ml_effects_CreateEffectsPipeline();
typedef decltype(&cros_ml_effects_CreateEffectsPipeline)
    cros_ml_effects_CreateEffectsPipelineFn;

void cros_ml_effects_DeleteEffectsPipeline(void* pipeline);
typedef decltype(&cros_ml_effects_DeleteEffectsPipeline)
    cros_ml_effects_DeleteEffectsPipelineFn;

bool cros_ml_effects_ProcessFrame(void* pipeline,
                                  int64_t timestamp,
                                  const uint8_t* frame_data,
                                  uint32_t frame_width,
                                  uint32_t frame_height,
                                  uint32_t stride);
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
