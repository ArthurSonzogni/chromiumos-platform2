// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/fake/fake_chrome_ml_api.h"

#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>

#include "odml/on_device_model/ml/chrome_ml_api.h"

namespace fake_ml {
namespace {
std::string PieceToString(const ml::InputPiece& piece) {
  if (std::holds_alternative<std::string>(piece)) {
    return std::get<std::string>(piece);
  }
  switch (std::get<ml::Token>(piece)) {
    case ml::Token::kSystem:
      return "System: ";
    case ml::Token::kModel:
      return "Model: ";
    case ml::Token::kUser:
      return "User: ";
    case ml::Token::kEnd:
      return " End.";
  }
}

std::string ReadFile(PlatformFile api_file) {
  base::File file(static_cast<base::PlatformFile>(api_file));
  std::vector<uint8_t> contents;
  contents.resize(file.GetLength());
  if (!file.ReadAndCheck(0, contents)) {
    return std::string();
  }
  return std::string(contents.begin(), contents.end());
}

}  // namespace

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

bool GetCapabilities(PlatformFile file, ChromeMLCapabilities& capabilities) {
  std::string contents = ReadFile(file);
  capabilities.image_input = contents.find("image") != std::string::npos;
  capabilities.audio_input = contents.find("audio") != std::string::npos;
  return true;
}

struct FakeModelInstance {
  ml::ModelBackendType backend_type;
  ml::ModelPerformanceHint performance_hint;
  std::string model_data;
};

struct FakeSessionInstance {
  std::string adaptation_data;
  std::optional<uint32_t> adaptation_file_id;
  std::vector<std::string> context;
  bool cloned;
  bool enable_image_input;
  bool enable_audio_input;
  uint32_t top_k;
  float temperature;
};

struct FakeTsModelInstance {
  std::string model_data;
};

struct FakeCancelInstance {
  bool cancelled = false;
};

ChromeMLModel SessionCreateModel(const ChromeMLModelDescriptor* descriptor,
                                 uintptr_t context,
                                 ChromeMLScheduleFn schedule) {
  return reinterpret_cast<ChromeMLModel>(new FakeModelInstance{
      .backend_type = descriptor->backend_type,
      .performance_hint = descriptor->performance_hint,
  });
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
  auto* model_instance = reinterpret_cast<FakeModelInstance*>(model);
  auto* instance = new FakeSessionInstance{};
  if (descriptor) {
    instance->enable_image_input = descriptor->enable_image_input;
    instance->enable_audio_input = descriptor->enable_audio_input;
    instance->top_k = descriptor->top_k;
    instance->temperature = descriptor->temperature;
    if (descriptor->model_data) {
      instance->adaptation_file_id = descriptor->model_data->file_id;
      if (model_instance->backend_type == ml::ModelBackendType::kGpuBackend) {
        instance->adaptation_data =
            ReadFile(descriptor->model_data->weights_file);
      } else if (model_instance->backend_type ==
                 ml::ModelBackendType::kApuBackend) {
        base::ReadFileToString(
            base::FilePath(descriptor->model_data->model_path),
            &instance->adaptation_data);
      }
    }
  }
  return reinterpret_cast<ChromeMLSession>(instance);
}

ChromeMLSession CloneSession(ChromeMLSession session) {
  auto* instance = reinterpret_cast<FakeSessionInstance*>(session);
  return reinterpret_cast<ChromeMLSession>(new FakeSessionInstance{
      .adaptation_data = instance->adaptation_data,
      .adaptation_file_id = instance->adaptation_file_id,
      .context = instance->context,
      .cloned = true,
      .enable_image_input = instance->enable_image_input,
      .enable_audio_input = instance->enable_audio_input,
      .top_k = instance->top_k,
      .temperature = instance->temperature,
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
  std::string text;
  for (size_t i = 0; i < options->input_size; i++) {
    // SAFETY: `options->input_size` describes how big `options->input` is.
    text += UNSAFE_BUFFERS(PieceToString(options->input[i]));
  }
  if (options->token_offset) {
    text.erase(text.begin(), text.begin() + options->token_offset);
  }
  if (options->max_tokens && options->max_tokens < text.size()) {
    text.resize(options->max_tokens);
  }
  if (!text.empty()) {
    instance->context.push_back(text);
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

  if (reinterpret_cast<FakeModelInstance*>(model)->performance_hint ==
      ml::ModelPerformanceHint::kFastestInference) {
    OutputChunk("Fastest inference\n");
  }
  if (!instance->adaptation_data.empty()) {
    std::string adaptation_str = "Adaptation: " + instance->adaptation_data;
    if (instance->adaptation_file_id) {
      adaptation_str +=
          " (" + base::NumberToString(*instance->adaptation_file_id) + ")";
    }
    OutputChunk(adaptation_str + "\n");
  }

  // Only include sampling params if they're not the respective default values.
  if (instance->top_k != 1 || instance->temperature != 0) {
    OutputChunk(base::StrCat(
        {"TopK: ", base::NumberToString(instance->top_k),
         ", Temp: ", base::NumberToString(instance->temperature), "\n"}));
  }

  if (!instance->context.empty()) {
    for (const std::string& context : instance->context) {
      OutputChunk("Context: " + context + "\n");
    }
  }
  OutputChunk("");
  return true;
}

void SessionSizeInTokensInputPiece(ChromeMLSession session,
                                   ChromeMLModel model,
                                   const ml::InputPiece* input,
                                   size_t input_size,
                                   const ChromeMLSizeInTokensFn& fn) {
  std::string text;
  for (size_t i = 0; i < input_size; i++) {
    // SAFETY: `input_size` describes how big `input` is.
    text += UNSAFE_BUFFERS(PieceToString(input[i]));
  }
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
  if (*num_scores != 2) {
    *num_scores = 2;
    return ChromeMLSafetyResult::kInsufficientStorage;
  }
  bool has_unsafe = std::string(text).find("unsafe") != std::string::npos;
  // SAFETY: Follows a C-API, num_scores checked above, test-only code.
  UNSAFE_BUFFERS(scores[0]) = has_unsafe ? 0.8 : 0.2;
  bool has_reasonable =
      std::string(text).find("reasonable") != std::string::npos;
  // SAFETY: Follows a C-API, num_scores checked above, test-only code.
  UNSAFE_BUFFERS(scores[1]) = has_reasonable ? 0.2 : 0.8;
  return ChromeMLSafetyResult::kOk;
}

const ChromeMLAPI g_api = {
    .InitDawnProcs = &InitDawnProcs,
    .SetMetricsFns = &SetMetricsFns,
    .SetFatalErrorFn = &SetFatalErrorFn,
    .ClassifyTextSafety = &ClassifyTextSafety,
    .DestroyModel = &DestroyModel,
    .GetEstimatedPerformance = &GetEstimatedPerformance,
    .QueryGPUAdapter = &QueryGPUAdapter,
    .GetCapabilities = &GetCapabilities,
    .SetFatalErrorNonGpuFn = &SetFatalErrorNonGpuFn,

    .SessionCreateModel = &SessionCreateModel,
    .SessionExecuteModel = &SessionExecuteModel,
    .SessionSizeInTokensInputPiece = &SessionSizeInTokensInputPiece,
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
