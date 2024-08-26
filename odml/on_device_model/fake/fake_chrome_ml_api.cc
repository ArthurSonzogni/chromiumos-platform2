// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/fake/fake_chrome_ml_api.h"

#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/strings/string_number_conversions.h>

#include "odml/on_device_model/ml/chrome_ml_api.h"

namespace fake_ml {

void InitDawnProcs(const DawnProcTable& procs) {}

void SetMetricsFns(const ChromeMLMetricsFns* fns) {}

void SetFatalErrorFn(ChromeMLFatalErrorFn error_fn) {}

void SetFatalErrorNonGpuFn(ChromeMLFatalErrorFn error_fn) {}

bool GetEstimatedPerformance(ChromeMLPerformanceInfo* performance_info) {
  return false;
}

bool QueryGPUAdapter(void (*adapter_callback_fn)(WGPUAdapter adapter,
                                                 void* userdata),
                     void* userdata) {
  return false;
}

struct FakeModelInstance {
  std::string model_data_;
};

struct FakeSessionInstance {
  std::string adaptation_data_;
  std::vector<std::string> context_;
};

struct FakeTsModelInstance {
  std::string model_data_;
};

struct FakeCancelInstance {
  bool cancelled = false;
};

std::string ReadFile(PlatformFile api_file) {
  base::File file(static_cast<base::PlatformFile>(api_file));
  std::vector<uint8_t> contents;
  contents.resize(file.GetLength());
  if (!file.ReadAndCheck(0, contents)) {
    return std::string();
  }
  return std::string(contents.begin(), contents.end());
}

ChromeMLModel SessionCreateModel(const ChromeMLModelDescriptor* descriptor,
                                 uintptr_t context,
                                 ChromeMLScheduleFn schedule) {
  return reinterpret_cast<ChromeMLModel>(new FakeModelInstance{});
}

void DestroyModel(ChromeMLModel model) {
  auto* instance = reinterpret_cast<FakeModelInstance*>(model);
  delete instance;
}

ChromeMLSafetyResult ClassifyTextSafety(ChromeMLModel model,
                                        const char* text,
                                        float* scores,
                                        size_t* num_scores) {
  return ChromeMLSafetyResult::kNoClassifier;
}

ChromeMLSession CreateSession(ChromeMLModel model,
                              const ChromeMLAdaptationDescriptor* descriptor) {
  auto* instance = new FakeSessionInstance{};
  if (descriptor) {
    instance->adaptation_data_ = ReadFile(descriptor->model_data->weights_file);
  }
  return reinterpret_cast<ChromeMLSession>(instance);
}

ChromeMLSession CloneSession(ChromeMLSession session) {
  auto* instance = reinterpret_cast<FakeSessionInstance*>(session);
  return reinterpret_cast<ChromeMLSession>(new FakeSessionInstance{
      .adaptation_data_ = instance->adaptation_data_,
      .context_ = instance->context_,
  });
}

void DestroySession(ChromeMLSession session) {
  auto* instance = reinterpret_cast<FakeSessionInstance*>(session);
  delete instance;
}

bool SessionExecuteModel(ChromeMLSession session,
                         ChromeMLModel model,
                         const ChromeMLExecuteOptions* options,
                         ChromeMLCancel cancel) {
  auto* instance = reinterpret_cast<FakeSessionInstance*>(session);
  std::string text = options->prompt;
  if (options->token_offset) {
    text.erase(text.begin(), text.begin() + options->token_offset);
  }
  if (options->max_tokens && options->max_tokens < text.size()) {
    text.resize(options->max_tokens);
  }
  if (!text.empty()) {
    instance->context_.push_back(text);
  }
  if (options->context_saved_fn) {
    (*options->context_saved_fn)(static_cast<int>(text.size()));
  }

  if (!options->execution_output_fn) {
    return true;
  }

  auto OutputChunk =
      [output_fn = *options->execution_output_fn](const std::string& chunk) {
        ChromeMLExecutionOutput output = {};
        if (chunk.empty()) {
          output.status = ChromeMLExecutionStatus::kComplete;
          output_fn(&output);
          return;
        }
        output.status = ChromeMLExecutionStatus::kInProgress;
        output.text = chunk.c_str();
        output_fn(&output);
      };

  if (!instance->adaptation_data_.empty()) {
    OutputChunk("Adaptation: " + instance->adaptation_data_ + "\n");
  }
  if (!instance->context_.empty()) {
    const std::string last = instance->context_.back();
    instance->context_.pop_back();
    for (const std::string& context : instance->context_) {
      OutputChunk("Context: " + context + "\n");
    }
    OutputChunk("Input: " + last + "\n");
  }
  OutputChunk("");
  return true;
}

void SessionSizeInTokens(ChromeMLSession session,
                         const std::string& text,
                         const ChromeMLSizeInTokensFn& fn) {
  fn(text.size());
}

void SessionScore(ChromeMLSession session,
                  const std::string& text,
                  const ChromeMLScoreFn& fn) {
  fn(static_cast<float>(text[0]));
}

ChromeMLCancel CreateCancel() {
  return reinterpret_cast<ChromeMLCancel>(new FakeCancelInstance());
}

void DestroyCancel(ChromeMLCancel cancel) {
  delete reinterpret_cast<FakeCancelInstance*>(cancel);
}

void CancelExecuteModel(ChromeMLCancel cancel) {
  auto* instance = reinterpret_cast<FakeCancelInstance*>(cancel);
  instance->cancelled = true;
}

ChromeMLTSModel CreateTSModel(const ChromeMLTSModelDescriptor* descriptor) {
  auto* instance = new FakeTsModelInstance{};
  return reinterpret_cast<ChromeMLTSModel>(instance);
}

void DestroyTSModel(ChromeMLTSModel model) {
  auto* instance = reinterpret_cast<FakeTsModelInstance*>(model);
  delete instance;
}

ChromeMLSafetyResult TSModelClassifyTextSafety(ChromeMLTSModel model,
                                               const char* text,
                                               float* scores,
                                               size_t* num_scores) {
  return ChromeMLSafetyResult::kNoClassifier;
}

const ChromeMLAPI g_api = {
    .InitDawnProcs = &InitDawnProcs,
    .SetMetricsFns = &SetMetricsFns,
    .SetFatalErrorFn = &SetFatalErrorFn,
    .ClassifyTextSafety = &ClassifyTextSafety,
    .DestroyModel = &DestroyModel,
    .GetEstimatedPerformance = &GetEstimatedPerformance,
    .QueryGPUAdapter = &QueryGPUAdapter,
    .SetFatalErrorNonGpuFn = &SetFatalErrorNonGpuFn,

    .SessionCreateModel = &SessionCreateModel,
    .SessionExecuteModel = &SessionExecuteModel,
    .SessionSizeInTokens = &SessionSizeInTokens,
    .SessionScore = &SessionScore,
    .CreateSession = &CreateSession,
    .CloneSession = &CloneSession,
    .DestroySession = &DestroySession,
    .CreateCancel = &CreateCancel,
    .DestroyCancel = &DestroyCancel,
    .CancelExecuteModel = &CancelExecuteModel,
    .ts_api =
        {
            .CreateModel = &CreateTSModel,
            .DestroyModel = &DestroyTSModel,
            .ClassifyTextSafety = &TSModelClassifyTextSafety,
        },
};

const ChromeMLAPI* GetFakeMlApi() {
  return &g_api;
}

}  // namespace fake_ml
