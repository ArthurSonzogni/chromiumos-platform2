// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/effects_pipeline.h"

#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/scoped_native_library.h>

namespace {

constexpr char kLibraryPath[] = "libcros_ml_core_internal.so";

class EffectsPipelineImpl : public cros::EffectsPipeline {
 public:
  virtual ~EffectsPipelineImpl() {
    if (pipeline_ && delete_fn_) {
      delete_fn_(pipeline_);
    }
  }

  virtual bool ProcessFrame(int64_t timestamp,
                            const uint8_t* frame_data,
                            uint32_t frame_width,
                            uint32_t frame_height,
                            uint32_t stride) {
    frames_started_ = true;
    return process_frame_fn_(pipeline_, timestamp, frame_data, frame_width,
                             frame_height, stride);
  }

  virtual bool Wait() { return wait_fn_(pipeline_); }

  virtual bool SetRenderedImageObserver(
      std::unique_ptr<cros::ProcessedFrameObserver> observer) {
    if (!frames_started_) {
      rendered_image_observer_ = std::move(observer);
      return true;
    }
    return false;
  }

  // TODO(b:237964122) Consider converting effects_config to a protobuf
  virtual void SetEffect(cros::EffectsConfig* effects_config,
                         void (*callback)(bool)) {
    set_effect_fn_(pipeline_, effects_config, callback);
  }

 protected:
  EffectsPipelineImpl() {}
  bool Initialize() {
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
    set_effect_fn_ = reinterpret_cast<cros_ml_effects_SetEffectFn>(
        library_->GetFunctionPointer("cros_ml_effects_SetEffect"));

    bool load_ok = (create_fn_ != nullptr) && (delete_fn_ != nullptr) &&
                   (process_frame_fn_ != nullptr) && (wait_fn_ != nullptr) &&
                   (set_rendered_image_observer_fn_ != nullptr) &&
                   (set_effect_fn_ != nullptr);

    if (!load_ok) {
      LOG(ERROR) << "create_fn_" << create_fn_;
      LOG(ERROR) << "delete_fn_" << delete_fn_;
      LOG(ERROR) << "process_frame_fn_" << process_frame_fn_;
      LOG(ERROR) << "wait_fn_" << wait_fn_;
      LOG(ERROR) << "set_rendered_image_observer_fn_"
                 << set_rendered_image_observer_fn_;
      LOG(ERROR) << "set_effect_fn_" << set_effect_fn_;
      LOG(ERROR) << "Pipeline cannot load the expected functions";
      return false;
    }

    pipeline_ = create_fn_();
    LOG(INFO) << "Pipeline created";
    set_rendered_image_observer_fn_(
        pipeline_, this, &EffectsPipelineImpl::RenderedImageFrameHandler);

    return true;
  }

 private:
  static void RenderedImageFrameHandler(void* handler,
                                        int64_t timestamp,
                                        const uint8_t* frame_data,
                                        uint32_t frame_width,
                                        uint32_t frame_height,
                                        uint32_t stride) {
    EffectsPipelineImpl* pipeline = static_cast<EffectsPipelineImpl*>(handler);
    if (pipeline->rendered_image_observer_) {
      pipeline->rendered_image_observer_->OnFrameProcessed(
          timestamp, frame_data, frame_width, frame_height, stride);
    }
  }

  std::optional<base::ScopedNativeLibrary> library_;
  cros_ml_effects_CreateEffectsPipelineFn create_fn_ = nullptr;
  cros_ml_effects_DeleteEffectsPipelineFn delete_fn_ = nullptr;
  cros_ml_effects_ProcessFrameFn process_frame_fn_ = nullptr;
  cros_ml_effects_WaitFn wait_fn_ = nullptr;
  cros_ml_effects_SetRenderedImageObserverFn set_rendered_image_observer_fn_ =
      nullptr;
  cros_ml_effects_SetEffectFn set_effect_fn_ = nullptr;
  void* pipeline_ = nullptr;
  bool frames_started_ = false;

  std::unique_ptr<cros::ProcessedFrameObserver> rendered_image_observer_;

  friend class EffectsPipeline;
};

}  // namespace

namespace cros {

std::unique_ptr<EffectsPipeline> EffectsPipeline::Create() {
  auto pipeline =
      std::unique_ptr<EffectsPipelineImpl>(new EffectsPipelineImpl());
  if (!pipeline->Initialize()) {
    return nullptr;
  }
  return pipeline;
}

}  // namespace cros
