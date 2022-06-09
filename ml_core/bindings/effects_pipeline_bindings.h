// Copyright 2022 The ChromiumOS Authors.
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
}

#endif  // ML_CORE_BINDINGS_EFFECTS_PIPELINE_BINDINGS_H_
