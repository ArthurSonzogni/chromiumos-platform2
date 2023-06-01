// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/effects_pipeline.h"

#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/scoped_native_library.h>
#include <session_manager/dbus-proxies.h>

#include "ml_core/opencl_caching/constants.h"

namespace {

using org::chromium::SessionManagerInterfaceProxy;

constexpr char kLibraryName[] = "libcros_ml_core_internal.so";

class EffectsPipelineImpl : public cros::EffectsPipeline {
 public:
  ~EffectsPipelineImpl() override {
    if (pipeline_ && delete_fn_) {
      delete_fn_(pipeline_);
    }
  }

  bool ProcessFrame(int64_t timestamp,
                    GLuint frame_texture,
                    uint32_t frame_width,
                    uint32_t frame_height) override {
    frames_started_ = true;
    return process_frame_fn_(pipeline_, timestamp, frame_texture, frame_width,
                             frame_height);
  }

  bool Wait() override { return wait_fn_(pipeline_); }

  bool SetRenderedImageObserver(
      std::unique_ptr<cros::ProcessedFrameObserver> observer) override {
    if (!frames_started_) {
      rendered_image_observer_ = std::move(observer);
      return true;
    }
    return false;
  }

  // TODO(b:237964122) Consider converting effects_config to a protobuf
  void SetEffect(cros::EffectsConfig* effects_config,
                 void (*callback)(bool)) override {
    set_effect_fn_(pipeline_, effects_config, callback);
  }

 protected:
  EffectsPipelineImpl() {}
  bool Initialize(const base::FilePath& dlc_root_path,
                  EGLContext share_context,
                  const base::FilePath& caching_dir_override) {
#ifdef USE_LOCAL_ML_CORE_INTERNAL
    // TODO(jmpollock) this should be /usr/local/lib on arm.
    base::FilePath lib_path =
        base::FilePath("/usr/local/lib64").Append(kLibraryName);
#else
    base::FilePath lib_path = dlc_root_path.Append(kLibraryName);
#endif
    base::NativeLibraryOptions native_library_options;
    base::NativeLibraryLoadError load_error;
    native_library_options.prefer_own_symbols = true;
    library_.emplace(base::LoadNativeLibraryWithOptions(
        lib_path, native_library_options, &load_error));

    if (!library_->is_valid()) {
      LOG(ERROR) << "Pipeline library load error: " << load_error.ToString();
      return false;
    }

    LOG(INFO) << "Loading pipeline library from: " << lib_path;

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
    set_log_observer_fn_ = reinterpret_cast<cros_ml_effects_SetLogObserverFn>(
        library_->GetFunctionPointer("cros_ml_effects_SetLogObserver"));

    bool load_ok = (create_fn_ != nullptr) && (delete_fn_ != nullptr) &&
                   (process_frame_fn_ != nullptr) && (wait_fn_ != nullptr) &&
                   (set_rendered_image_observer_fn_ != nullptr) &&
                   (set_effect_fn_ != nullptr) &&
                   (set_log_observer_fn_ != nullptr);

    if (!load_ok) {
      LOG(ERROR) << "create_fn_" << create_fn_;
      LOG(ERROR) << "delete_fn_" << delete_fn_;
      LOG(ERROR) << "process_frame_fn_" << process_frame_fn_;
      LOG(ERROR) << "wait_fn_" << wait_fn_;
      LOG(ERROR) << "set_rendered_image_observer_fn_"
                 << set_rendered_image_observer_fn_;
      LOG(ERROR) << "set_effect_fn_" << set_effect_fn_;
      LOG(ERROR) << "set_log_observer_fn_" << set_log_observer_fn_;
      LOG(ERROR) << "Pipeline cannot load the expected functions";
      return false;
    }

    std::string cache_dir(caching_dir_override.empty()
                              ? cros::kOpenCLCachingDir
                              : caching_dir_override.value());
    pipeline_ = create_fn_(share_context, cache_dir.c_str());
    LOG(INFO) << "Pipeline created, cache_dir: " << cache_dir;
    set_rendered_image_observer_fn_(
        pipeline_, this, &EffectsPipelineImpl::RenderedImageFrameHandler);
    set_log_observer_fn_(pipeline_, &EffectsPipelineImpl::OnLogMessage);

    return true;
  }

 private:
  static void RenderedImageFrameHandler(void* handler,
                                        int64_t timestamp,
                                        GLuint frame_texture,
                                        uint32_t frame_width,
                                        uint32_t frame_height) {
    EffectsPipelineImpl* pipeline = static_cast<EffectsPipelineImpl*>(handler);
    if (pipeline->rendered_image_observer_) {
      pipeline->rendered_image_observer_->OnFrameProcessed(
          timestamp, frame_texture, frame_width, frame_height);
    }
  }

  static void OnLogMessage(cros_ml_effects_LogSeverity severity,
                           const char* msg,
                           size_t len) {
    switch (severity) {
      default:
        [[fallthrough]];
      case cros_ml_effects_LogSeverity_Info:
        LOG(INFO) << std::string(msg, len);
        break;
      case cros_ml_effects_LogSeverity_Warning:
        LOG(WARNING) << std::string(msg, len);
        break;
      case cros_ml_effects_LogSeverity_Error:
        LOG(ERROR) << std::string(msg, len);
        break;
      case cros_ml_effects_LogSeverity_Fatal:
        LOG(FATAL) << std::string(msg, len);
        break;
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
  cros_ml_effects_SetLogObserverFn set_log_observer_fn_ = nullptr;
  void* pipeline_ = nullptr;
  bool frames_started_ = false;

  std::unique_ptr<cros::ProcessedFrameObserver> rendered_image_observer_;

  friend class EffectsPipeline;
};

}  // namespace

namespace cros {

std::unique_ptr<EffectsPipeline> EffectsPipeline::Create(
    const base::FilePath& dlc_root_path,
    EGLContext share_context,
    const base::FilePath& caching_dir_override) {
  auto pipeline =
      std::unique_ptr<EffectsPipelineImpl>(new EffectsPipelineImpl());
  if (!pipeline->Initialize(dlc_root_path, share_context,
                            caching_dir_override)) {
    return nullptr;
  }
  return pipeline;
}

}  // namespace cros
