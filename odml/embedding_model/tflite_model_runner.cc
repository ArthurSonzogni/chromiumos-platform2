// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/tflite_model_runner.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tensorflow/lite/context.h>
#include <tensorflow/lite/delegates/gpu/delegate.h>
#include <tensorflow/lite/delegates/utils/experimental/stable_delegate/delegate_loader.h>
#include <tensorflow/lite/delegates/utils/experimental/stable_delegate/tflite_settings_json_parser.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/logger.h>

void RegisterSelectedOps(::tflite::MutableOpResolver* resolver);

// Version with Weak linker attribute doing nothing: if someone links this
// library with another definition of this function (presumably to actually
// register custom ops), that version will be used instead.
void ABSL_ATTRIBUTE_WEAK
RegisterSelectedOps(::tflite::MutableOpResolver* resolver) {}

namespace embedding_model {

namespace {

int ComputeSizeFromDims(const TfLiteIntArray& dims) {
  int result = 1;
  for (int i = 0; i < dims.size; ++i) {
    result *= dims.data[i];
  }
  return result;
}

using FormatForEmbeddingFunction = std::optional<std::string> (*)(
    const std::string&,
    const std::string&,
    const std::unordered_map<std::string, std::string>&);

constexpr char kClusteringTaskType[] = "clustering";
constexpr char kContentKey[] = "content";

constexpr char kDelegateCpu[] = "cpu";
constexpr char kDelegateGpuOpenCl[] = "gpu-opencl";
constexpr char kDelegateMediatekNeuron[] = "mediatek-neuron";

constexpr char kTfliteRunnerLoadStatusHistogramName[] =
    "OnDeviceModel.Embedding.TfliteRunnerLoadStatus";
constexpr char kTfliteRunnerRunStatusHistogramName[] =
    "OnDeviceModel.Embedding.TfliteRunnerRunStatus";

constexpr char kMediatekNeuronDelegatePath[] =
    "/usr/lib64/libtensorflowlite_mtk_neuron_delegate.so";

}  // namespace

TfliteModelRunner::TfliteModelRunner(
    ModelInfo&& model_info,
    std::unique_ptr<Tokenizer> tokenizer,
    const raw_ref<odml::OdmlShimLoader> shim_loader,
    const raw_ref<MetricsLibraryInterface> metrics)
    : input_node_(-1),
      output_node_(-1),
      delegate_type_(DelegateType::kDelegateTypeNotSet),
      shim_loader_(shim_loader),
      model_info_(std::move(model_info)),
      tflite_info_(std::get_if<struct EmbeddingTfliteModelInfo>(
          &model_info_.type_specific_info)),
      tokenizer_(std::move(tokenizer)),
      metrics_(metrics) {
  CHECK(tflite_info_);
}

void TfliteModelRunner::Load(base::PassKey<ModelHolder> passkey,
                             LoadCallback callback) {
  CHECK(tokenizer_);
  CHECK(!tokenizer_->IsLoaded());

  if (tflite_info_->delegate == kDelegateCpu || tflite_info_->delegate == "") {
    delegate_type_ = DelegateType::kDelegateTypeCpu;
  } else if (tflite_info_->delegate == kDelegateGpuOpenCl) {
    delegate_type_ = DelegateType::kDelegateTypeGpuOpenCl;
  } else if (tflite_info_->delegate == kDelegateMediatekNeuron) {
    delegate_type_ = DelegateType::kDelegateTypeMediatekNeuron;
  } else {
    LOG(ERROR) << "Unsupported delegate option for TfliteModelRunner: "
               << tflite_info_->delegate;
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kUnsupportedDelegate);
    std::move(callback).Run(false);
    return;
  }

  model_.reset();
  interpreter_.reset();

  callback =
      base::BindOnce(&TfliteModelRunner::LoadFinishWrapper,
                     base::Unretained(this), passkey, std::move(callback));

  if (shim_loader_->IsShimReady()) {
    OnShimFinishLoading(passkey, std::move(callback), true);
    return;
  }

  shim_loader_->EnsureShimReady(
      base::BindOnce(&TfliteModelRunner::OnShimFinishLoading,
                     base::Unretained(this), passkey, std::move(callback)));
}

void TfliteModelRunner::OnShimFinishLoading(base::PassKey<ModelHolder> passkey,
                                            LoadCallback callback,
                                            bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to load the odml-shim";
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kNoOdmlShim);
    std::move(callback).Run(false);
    return;
  }
  base::OnceCallback<void(bool)> cb =
      base::BindOnce(&TfliteModelRunner::OnTokenizerLoadFinish,
                     base::Unretained(this), std::move(callback));
  tokenizer_->Load(passkey, tflite_info_->spm_path, std::move(cb));
}

void TfliteModelRunner::OnTokenizerLoadFinish(LoadCallback callback,
                                              bool success) {
  CHECK(!model_);
  if (!success) {
    LOG(ERROR) << "Failed to load the tokenizer " << tflite_info_->spm_path;
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kFailedToLoadTokenizer);
    std::move(callback).Run(false);
    return;
  }
  CHECK(tokenizer_->IsLoaded());

  model_ =
      tflite::FlatBufferModel::BuildFromFile(tflite_info_->tflite_path.c_str());

  if (!model_) {
    LOG(ERROR) << "Failed to load FlatBufferModel "
               << tflite_info_->tflite_path;
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kFailedToLoadFlatBufferModel);
    std::move(callback).Run(false);
    return;
  }

  tflite::ops::builtin::BuiltinOpResolver resolver;

  if (delegate_type_ == DelegateType::kDelegateTypeMediatekNeuron) {
    TfLiteRegistration reg = {nullptr, nullptr, nullptr, nullptr};
    resolver.AddCustom("cros-mtk-pre-compile", &reg);
    RegisterSelectedOps(&resolver);
  }

  std::unique_ptr<tflite::Interpreter> interpreter;
  tflite::InterpreterBuilder builder(*model_, resolver);

  const TfLiteStatus resolve_status = builder(&interpreter);
  if (resolve_status != kTfLiteOk || !interpreter) {
    LOG(ERROR) << "Could not resolve model ops.";
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kCantResolveModelOps);
    std::move(callback).Run(false);
    return;
  }

  // A delegate can be applied with 2 methods, the first is through
  // InterpreterBuilder::AddDelegate() and the second is through
  // Interpreter::ModifyGraphWithDelegate(). The official tensorflow
  // documentation did not specify which method is the recommended way. The
  // advantage of AddDelegate() is that it encapsulates the whole interaction
  // between delegate and interpreter so if there's any future change to the
  // current experimental stable delegate, it's more likely that AddDelegate()
  // will capture these changes, while the downside is that AddDelegate() does
  // not accept transfer of ownership of delegate, and therefore needs more
  // complicated ownership lifecycle management. Currently we select the second
  // method for the ease of ownership management, but should there be changes to
  // the way stable delegate interacts with interpreter, we'll need to apply the
  // revelant changes.
  if (delegate_type_ == DelegateType::kDelegateTypeGpuOpenCl) {
    // Apply GPU delegate
    TfLiteGpuDelegateOptionsV2 options(TfLiteGpuDelegateOptionsV2Default());
    options.experimental_flags |= TFLITE_GPU_EXPERIMENTAL_FLAGS_CL_ONLY;
    TfliteModelRunner::TfLiteDelegatePtr delegate =
        TfliteModelRunner::TfLiteDelegatePtr(
            TfLiteGpuDelegateV2Create(&options), TfLiteGpuDelegateV2Delete);
    if (!delegate) {
      LOG(ERROR) << "GPU requested but not available.";
      metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                              LoadResultHistogram::kNoGpuOpenClDelegate);
      std::move(callback).Run(false);
      return;
    }
    if (interpreter->ModifyGraphWithDelegate(std::move(delegate)) !=
        kTfLiteOk) {
      LOG(ERROR) << "Could not use GPU delegate.";
      metrics_->SendEnumToUMA(
          kTfliteRunnerLoadStatusHistogramName,
          LoadResultHistogram::kGpuOpenClDelegateModifyFailed);
      std::move(callback).Run(false);
      return;
    }
  } else if (delegate_type_ == DelegateType::kDelegateTypeMediatekNeuron) {
    tflite::delegates::utils::TfLiteSettingsJsonParser parser;
    auto opts = parser.Parse(tflite_info_->delegate_config_path);
    if (!opts) {
      LOG(ERROR)
          << "Failed to parse Mediatek Neuron Tflite delegate settings file.";
      metrics_->SendEnumToUMA(
          kTfliteRunnerLoadStatusHistogramName,
          LoadResultHistogram::kMediatekNeuronDelegateSettingParseFailed);
      std::move(callback).Run(false);
      return;
    }

    const TfLiteStableDelegate* stable_delegate =
        tflite::delegates::utils::LoadDelegateFromSharedLibrary(
            kMediatekNeuronDelegatePath);
    if (!stable_delegate) {
      LOG(ERROR) << "Unable to load Mediatek Neuron Tflite delegate.";
      metrics_->SendEnumToUMA(
          kTfliteRunnerLoadStatusHistogramName,
          LoadResultHistogram::kMediatekNeuronDelegateLoadFailed);
      std::move(callback).Run(false);
      return;
    }
    TfliteModelRunner::TfLiteDelegatePtr delegate =
        TfliteModelRunner::TfLiteDelegatePtr(
            stable_delegate->delegate_plugin->create(opts),
            stable_delegate->delegate_plugin->destroy);
    if (!delegate) {
      LOG(ERROR) << "Failed to create Mediatek Neuron Tflite delegate.";
      metrics_->SendEnumToUMA(
          kTfliteRunnerLoadStatusHistogramName,
          LoadResultHistogram::kMediatekNeuronDelegateCreateFailed);
      std::move(callback).Run(false);
      return;
    }
    if (interpreter->ModifyGraphWithDelegate(std::move(delegate)) !=
        kTfLiteOk) {
      LOG(ERROR) << "Could not use Mediatek Neuron delegate.";
      metrics_->SendEnumToUMA(
          kTfliteRunnerLoadStatusHistogramName,
          LoadResultHistogram::kMediatekNeuronDelegateModifyFailed);
      std::move(callback).Run(false);
      return;
    }
  }

  // Allocate memory for tensors.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    LOG(ERROR) << "Could not allocate tensors.";
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kCantAllocateTensors);
    std::move(callback).Run(false);
    return;
  }

  if (interpreter->inputs().size() != 1) {
    LOG(ERROR) << "Unexpected multiple inputs in embedding model tflite.";
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kIncorrectInputSize);
    std::move(callback).Run(false);
    return;
  }
  input_node_ = interpreter->inputs()[0];
  if (interpreter->outputs().size() != 1) {
    LOG(ERROR) << "Unexpected multiple outputs in embedding model tflite.";
    metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                            LoadResultHistogram::kIncorrectOutputSize);
    std::move(callback).Run(false);
    return;
  }
  output_node_ = interpreter->outputs()[0];

  interpreter_ = std::move(interpreter);
  metrics_->SendEnumToUMA(kTfliteRunnerLoadStatusHistogramName,
                          LoadResultHistogram::kSuccess);
  LOG(INFO) << "Model loaded " << tflite_info_->tflite_path;
  std::move(callback).Run(true);
}

void TfliteModelRunner::LoadFinishWrapper(base::PassKey<ModelHolder> passkey,
                                          LoadCallback callback,
                                          bool success) {
  if (!success) {
    // Failed, need to cleanup.
    if (tokenizer_->IsLoaded()) {
      tokenizer_->Unload(passkey);
    }
    interpreter_.reset();
    model_.reset();
  }

  std::move(callback).Run(success);
}

void TfliteModelRunner::Unload(base::PassKey<ModelHolder> passkey,
                               UnloadCallback callback) {
  CHECK(tokenizer_);
  CHECK(tokenizer_->IsLoaded());
  CHECK(model_);
  CHECK(interpreter_);
  tokenizer_->Unload(passkey);
  interpreter_.reset();
  model_.reset();
  std::move(callback).Run();
}

std::string TfliteModelRunner::GetModelVersion() {
  return model_info_.model_version;
}

void TfliteModelRunner::Run(base::PassKey<ModelHolder> passkey,
                            mojom::GenerateEmbeddingRequestPtr request,
                            RunCallback callback) {
  if (!tokenizer_ || !tokenizer_->IsLoaded() || !interpreter_) {
    LOG(ERROR) << "TfliteModelRunner::Run() called while not loaded.";
    metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                            RunResultHistogram::kNotLoaded);
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  const TfLiteIntArray& input_dims = *interpreter_->tensor(input_node_)->dims;
  const TfLiteIntArray& output_dims = *interpreter_->tensor(output_node_)->dims;

  int input_size = ComputeSizeFromDims(input_dims);
  int output_size = ComputeSizeFromDims(output_dims);

  auto format_for_embedding_fn =
      shim_loader_->Get<FormatForEmbeddingFunction>("FormatForEmbedding");
  if (!format_for_embedding_fn) {
    LOG(ERROR) << "No FormatForEmbedding in odml-shim.";
    metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                            RunResultHistogram::kNoFormatFunctionInShim);
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  std::unordered_map<std::string, std::string> format_params;
  format_params.insert(make_pair(kContentKey, std::move(request->content)));
  std::optional<std::string> input_str = format_for_embedding_fn(
      model_info_.model_version, kClusteringTaskType, format_params);
  if (!input_str) {
    LOG(ERROR) << "Failed to format input for embedding.";
    metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                            RunResultHistogram::kFormatFailed);
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  // Tokenize
  std::optional<std::vector<int>> token_ids =
      tokenizer_->Tokenize(passkey, std::move(*input_str));
  if (!token_ids.has_value()) {
    // Tokenizing failed.
    LOG(ERROR) << "Failed to tokenize input for embedding.";
    metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                            RunResultHistogram::kTokenizeFailed);
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  if (token_ids->size() > input_size) {
    if (request->truncate_input) {
      token_ids->resize(input_size, 0);
    } else {
      metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                              RunResultHistogram::kTooLong);
      std::move(callback).Run(
          mojom::OnDeviceEmbeddingModelInferenceError::kTooLong,
          std::vector<float>());
      return;
    }
  } else if (token_ids->size() < input_size) {
    token_ids->resize(input_size, 0);
  }

  // Populate input
  int* input_ptr = interpreter_->typed_tensor<int>(input_node_);
  for (int i = 0; i < input_size; i++) {
    input_ptr[i] = (*token_ids)[i];
  }

  // Run the embedding model.
  auto ret = interpreter_->Invoke();
  if (ret != kTfLiteOk) {
    LOG(ERROR) << "Tflite graph Invoke() failed unexpectedly.";
    metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                            RunResultHistogram::kInvokeFailed);
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  // Extract the output.
  std::vector<float> output;
  float* output_ptr = interpreter_->typed_tensor<float>(output_node_);
  for (int i = 0; i < output_size; i++) {
    output.push_back(output_ptr[i]);
  }

  metrics_->SendEnumToUMA(kTfliteRunnerRunStatusHistogramName,
                          RunResultHistogram::kSuccess);
  std::move(callback).Run(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                          output);
}

}  // namespace embedding_model
