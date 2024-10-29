// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_TFLITE_MODEL_RUNNER_H_
#define ODML_EMBEDDING_MODEL_TFLITE_MODEL_RUNNER_H_

#include <memory>
#include <string>

#include <metrics/metrics_library.h>
#include <tensorflow/lite/delegates/gpu/delegate.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/model.h>

#include "odml/embedding_model/model_info.h"
#include "odml/embedding_model/model_runner.h"
#include "odml/embedding_model/tokenizer.h"
#include "odml/utils/odml_shim_loader.h"

namespace embedding_model {

class TfliteModelRunner : public ModelRunner {
 public:
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class LoadResultHistogram {
    kSuccess = 0,
    kUnsupportedDelegate = 1,
    kNoOdmlShim = 2,
    kFailedToLoadTokenizer = 3,
    kFailedToLoadFlatBufferModel = 4,
    kCantResolveModelOps = 5,
    kNoGpuOpenClDelegate = 6,
    kGpuOpenClDelegateModifyFailed = 7,
    kCantAllocateTensors = 8,
    kIncorrectInputSize = 9,
    kIncorrectOutputSize = 10,
    kMediatekNeuronDelegateSettingParseFailed = 11,
    kMediatekNeuronDelegateLoadFailed = 12,
    kMediatekNeuronDelegateCreateFailed = 13,
    kMediatekNeuronDelegateModifyFailed = 14,
    kMaxValue = kMediatekNeuronDelegateModifyFailed,
  };
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class RunResultHistogram {
    kSuccess = 0,
    kNotLoaded = 1,
    kNoFormatFunctionInShim = 2,
    kFormatFailed = 3,
    kTokenizeFailed = 4,
    kTooLong = 5,
    kInvokeFailed = 6,
    kMaxValue = kInvokeFailed,
  };

  TfliteModelRunner(ModelInfo&& model_info,
                    std::unique_ptr<Tokenizer> tokenizer,
                    const raw_ref<odml::OdmlShimLoader> shim_loader,
                    const raw_ref<MetricsLibraryInterface> metrics);

  void Load(base::PassKey<ModelHolder> passkey, LoadCallback callback) override;

  void Unload(base::PassKey<ModelHolder> passkey,
              UnloadCallback callback) override;

  std::string GetModelVersion() override;

  void Run(base::PassKey<ModelHolder> passkey,
           mojom::GenerateEmbeddingRequestPtr request,
           RunCallback callback) override;

 private:
  enum class DelegateType {
    kDelegateTypeNotSet = 0,
    kDelegateTypeCpu = 1,
    kDelegateTypeGpuOpenCl = 2,
    kDelegateTypeMediatekNeuron = 3,
  };

  using TfLiteDelegatePtr =
      std::unique_ptr<TfLiteOpaqueDelegate,
                      std::function<void(TfLiteOpaqueDelegate*)>>;

  // Part of Load(), runs after shim_loader_ finishes loading.
  void OnShimFinishLoading(base::PassKey<ModelHolder> passkey,
                           LoadCallback callback,
                           bool success);

  // Part of Load(), runs after tokenizer_ Load() finish.
  void OnTokenizerLoadFinish(LoadCallback callback, bool success);

  // This is called whenever Load() finishes, it's primarily used to ensure
  // Load() does proper clean-up.
  void LoadFinishWrapper(base::PassKey<ModelHolder> passkey,
                         LoadCallback callback,
                         bool success);

  // Which node is the input/output in the tflite graph?
  int input_node_;
  int output_node_;

  // What delegates are we using? As in on which processor are we running this
  // model?
  DelegateType delegate_type_;

  // For access to the odml-shim functions, which is needed for formatting.
  const raw_ref<odml::OdmlShimLoader> shim_loader_;

  // Information on the model we're running.
  ModelInfo model_info_;
  EmbeddingTfliteModelInfo* tflite_info_;

  // Tokenizer for converting input text into input tokens.
  std::unique_ptr<Tokenizer> tokenizer_;

  // For sending metrics.
  const raw_ref<MetricsLibraryInterface> metrics_;

  // Loaded tflite model and tflite interpreter.
  std::unique_ptr<tflite::FlatBufferModel> model_;
  std::unique_ptr<tflite::Interpreter> interpreter_;
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_TFLITE_MODEL_RUNNER_H_
