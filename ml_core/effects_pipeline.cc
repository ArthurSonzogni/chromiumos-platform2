// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/effects_pipeline.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>

namespace {

constexpr char kLibraryPath[] = "libcros_ml_core_internal.so";

}  // namespace

namespace cros {

std::unique_ptr<EffectsPipeline> EffectsPipeline::Create() {
  auto pipeline = std::unique_ptr<EffectsPipeline>(new EffectsPipeline());
  if (!pipeline->Initialize()) {
    return nullptr;
  }
  return pipeline;
}

EffectsPipeline::EffectsPipeline() {}

bool EffectsPipeline::Initialize() {
  base::NativeLibraryOptions native_library_options;
  base::NativeLibraryLoadError load_error;
  native_library_options.prefer_own_symbols = true;
  library_.emplace(base::LoadNativeLibraryWithOptions(
      base::FilePath(kLibraryPath), native_library_options, &load_error));

  if (!library_->is_valid()) {
    LOG(ERROR) << "Pipeline library load error: " << load_error.ToString();
    return false;
  }

  create_fn_ = reinterpret_cast<cros_ml_effects_CreateEffectsPipelineFn>(
      library_->GetFunctionPointer("cros_ml_effects_CreateEffectsPipeline"));
  delete_fn_ = reinterpret_cast<cros_ml_effects_DeleteEffectsPipelineFn>(
      library_->GetFunctionPointer("cros_ml_effects_DeleteEffectsPipeline"));
  process_frame_fn_ = reinterpret_cast<cros_ml_effects_ProcessFrameFn>(
      library_->GetFunctionPointer("cros_ml_effects_ProcessFrame"));
  wait_fn_ = reinterpret_cast<cros_ml_effects_WaitFn>(
      library_->GetFunctionPointer("cros_ml_effects_Wait"));
  set_rendered_image_observer_fn_ =
      reinterpret_cast<cros_ml_effects_SetRenderedImageObserverFn>(
          library_->GetFunctionPointer(
              "cros_ml_effects_SetRenderedImageObserver"));

  bool load_ok = (create_fn_ != nullptr) && (delete_fn_ != nullptr) &&
                 (process_frame_fn_ != nullptr) && (wait_fn_ != nullptr) &&
                 (set_rendered_image_observer_fn_ != nullptr);

  if (!load_ok) {
    LOG(ERROR) << "create_fn_" << create_fn_;
    LOG(ERROR) << "delete_fn_" << delete_fn_;
    LOG(ERROR) << "process_frame_fn_" << process_frame_fn_;
    LOG(ERROR) << "wait_fn_" << wait_fn_;
    LOG(ERROR) << "set_rendered_image_observer_fn_"
               << set_rendered_image_observer_fn_;
    LOG(ERROR) << "Pipeline cannot load the expected functions";
    return false;
  }

  pipeline_ = create_fn_();
  LOG(INFO) << "Pipeline created";
  set_rendered_image_observer_fn_(pipeline_, this,
                                  &EffectsPipeline::RenderedImageFrameHandler);

  return true;
}

EffectsPipeline::~EffectsPipeline() {
  if (pipeline_ && delete_fn_) {
    delete_fn_(pipeline_);
  }
}

bool EffectsPipeline::ProcessFrame(int64_t timestamp,
                                   const uint8_t* frame_data,
                                   uint32_t frame_width,
                                   uint32_t frame_height,
                                   uint32_t stride) {
  frames_started_ = true;
  return process_frame_fn_(pipeline_, timestamp, frame_data, frame_width,
                           frame_height, stride);
}

bool EffectsPipeline::Wait() {
  return wait_fn_(pipeline_);
}

bool EffectsPipeline::SetRenderedImageObserver(
    std::unique_ptr<ProcessedFrameObserver> observer) {
  if (!frames_started_) {
    rendered_image_observer_ = std::move(observer);
    return true;
  }
  return false;
}

void EffectsPipeline::RenderedImageFrameHandler(void* handler,
                                                int64_t timestamp,
                                                const uint8_t* frame_data,
                                                uint32_t frame_width,
                                                uint32_t frame_height,
                                                uint32_t stride) {
  EffectsPipeline* pipeline = static_cast<EffectsPipeline*>(handler);
  if (pipeline->rendered_image_observer_) {
    pipeline->rendered_image_observer_->OnFrameProcessed(
        timestamp, frame_data, frame_width, frame_height, stride);
  }
}

}  // namespace cros
