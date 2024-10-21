// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/dlc_model_loader.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/functional/callback.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/types/expected.h>
#include <base/uuid.h>

#include "odml/utils/dlc_client_helper.h"

namespace embedding_model {

namespace {

constexpr char kLoadDlcStatusHistogramName[] =
    "OnDeviceModel.Embedding.LoadDlcStatus";

constexpr char kMlDlcPrefix[] = "ml-dlc-";
constexpr char kModelDescriptor[] = "model.json";

constexpr char kModelTypeKey[] = "model_type";
constexpr char kModelVersionKey[] = "model_version";
constexpr char kTfliteInfoKey[] = "tflite_info";
constexpr char kTflitePathKey[] = "tflite_path";
constexpr char kBuiltinSpmKey[] = "builtin_spm";
constexpr char kSpmPathKey[] = "spm_path";
constexpr char kDelegateKey[] = "delegate";

constexpr char kEmbeddingTflite[] = "embedding_tflite";

std::optional<EmbeddingTfliteModelInfo> ParseTfliteModelInfo(
    const base::Value::Dict& tflite_info_dict,
    const base::FilePath& dlc_root,
    const raw_ref<MetricsLibraryInterface> metrics) {
  EmbeddingTfliteModelInfo tflite_info;

  const std::string* tflite_path = tflite_info_dict.FindString(kTflitePathKey);
  if (!tflite_path) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kNoTflitePath);
    LOG(ERROR) << "No tflite model path in tflite embedding model DLC "
               << dlc_root;
    return std::nullopt;
  }
  tflite_info.tflite_path = dlc_root.Append(*tflite_path).value();

  std::optional<bool> builtin_spm = tflite_info_dict.FindBool(kBuiltinSpmKey);
  if (!builtin_spm.has_value()) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kNoBuiltinSpm);
    LOG(ERROR) << "No indication on whether spm tokenizer is built-in in "
                  "tflite embedding model DLC "
               << dlc_root;
    return std::nullopt;
  }
  tflite_info.builtin_spm = *builtin_spm;

  if (!tflite_info.builtin_spm) {
    const std::string* spm_path = tflite_info_dict.FindString(kSpmPathKey);
    if (!spm_path) {
      metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                             DlcModelLoader::LoadDlcHistogram::kNoSpmPath);
      LOG(ERROR) << "No spm tokenizer model path when external tokenizer is "
                    "specified for tflite embedding model DLC "
                 << dlc_root;
      return std::nullopt;
    }
    tflite_info.spm_path = dlc_root.Append(*spm_path).value();
  }

  const std::string* delegate = tflite_info_dict.FindString(kDelegateKey);
  if (!delegate) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kNoDelegate);
    LOG(ERROR) << "No tflite delegate specified for tflite embedding model DLC "
               << dlc_root;
    return std::nullopt;
  }
  tflite_info.delegate = *delegate;

  return tflite_info;
}

std::optional<ModelInfo> ParseModelInfo(
    const base::Value::Dict& model_dict,
    const base::FilePath& dlc_root,
    const raw_ref<MetricsLibraryInterface> metrics) {
  const std::string* model_type = model_dict.FindString(kModelTypeKey);
  if (!model_type) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kNoModelType);
    LOG(ERROR) << "Failed to read model type from embedding model DLC "
               << dlc_root;
    return std::nullopt;
  }

  const std::string* model_version = model_dict.FindString(kModelVersionKey);
  if (!model_version) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kNoModelVersion);
    LOG(ERROR) << "No model version specified for embedding model DLC "
               << dlc_root;
    return std::nullopt;
  }

  if (*model_type == kEmbeddingTflite) {
    const base::Value::Dict* tflite_info_dict =
        model_dict.FindDict(kTfliteInfoKey);
    if (!tflite_info_dict) {
      metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                             DlcModelLoader::LoadDlcHistogram::kNoTfliteInfo);
      LOG(ERROR) << "No tflite info for tflite embedding model DLC "
                 << dlc_root;
      return std::nullopt;
    }

    std::optional<EmbeddingTfliteModelInfo> tflite_info =
        ParseTfliteModelInfo(*tflite_info_dict, dlc_root, metrics);
    if (!tflite_info.has_value()) {
      // No need to log, as ParseTfliteModelInfo() already logs on failure.
      return std::nullopt;
    }
    ModelInfo model_info;
    model_info.model_type = *model_type;
    model_info.model_version = *model_version;
    model_info.type_specific_info = std::move(*tflite_info);
    return model_info;
  }

  metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                         DlcModelLoader::LoadDlcHistogram::kUnknownModelType);
  LOG(ERROR) << "Unknown model type " << model_type
             << " for embedding model DLC " << dlc_root;
  return std::nullopt;
}

std::optional<ModelInfo> BuildModelInfo(
    const base::FilePath& dlc_root,
    const raw_ref<MetricsLibraryInterface> metrics) {
  base::FilePath model_desc = dlc_root.Append(kModelDescriptor);
  std::string model_json;

  if (!base::ReadFileToString(model_desc, &model_json)) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kReadJsonFailed);
    LOG(ERROR) << "Failed to read model descriptor file for embedding model: "
               << dlc_root;
    return std::nullopt;
  }

  std::optional<base::Value::Dict> model_dict =
      base::JSONReader::ReadDict(model_json);

  if (!model_dict) {
    metrics->SendEnumToUMA(kLoadDlcStatusHistogramName,
                           DlcModelLoader::LoadDlcHistogram::kParseJsonFailed);
    LOG(ERROR) << "Failed to parse model descriptor file for DLC " << dlc_root;
    return std::nullopt;
  }

  return ParseModelInfo(*model_dict, dlc_root, metrics);
}

}  // namespace

DlcModelLoader::DlcModelLoader(const raw_ref<MetricsLibraryInterface> metrics)
    : metrics_(metrics) {}

void DlcModelLoader::LoadDlcWithUuid(const base::Uuid& uuid,
                                     LoadCallback callback) {
  if (!uuid.is_valid()) {
    LOG(ERROR) << "Invalid UUID for loading embedding model.";
    metrics_->SendEnumToUMA(kLoadDlcStatusHistogramName,
                            LoadDlcHistogram::kInvalidUuid);
    std::move(callback).Run(std::nullopt);
    return;
  }

  DlcLoadingState& state = loading_state_[uuid];

  if (state.model_info.has_value()) {
    std::move(callback).Run(*state.model_info);
    return;
  }

  state.pending_callbacks.push(base::BindOnce(
      &DlcModelLoader::ResolveInstallResult, weak_ptr_factory_.GetWeakPtr(),
      uuid, std::move(callback)));

  if (state.install_launched) {
    return;
  }

  std::string uuid_str = uuid.AsLowercaseString();
  std::string dlc_id = kMlDlcPrefix + uuid_str;

  state.install_launched = true;
  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      dlc_id,
      base::BindOnce(&DlcModelLoader::OnInstallDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), uuid),
      base::DoNothing());
  (*dlc_client)->InstallDlc();
}

void DlcModelLoader::OnInstallDlcComplete(
    const base::Uuid& uuid,
    base::expected<base::FilePath, std::string> result) {
  auto itr = loading_state_.find(uuid);
  CHECK(itr != loading_state_.end());
  DlcLoadingState& state = itr->second;
  // Note: We need to set install_launched false here because if any of the
  // callbacks attempts a retry, we expect another retry to be launched.
  state.install_launched = false;
  if (!result.has_value()) {
    metrics_->SendEnumToUMA(kLoadDlcStatusHistogramName,
                            LoadDlcHistogram::kInstallFailed);
    LOG(ERROR) << "Failed to install embedding model ML DLC: "
               << result.error();
  } else {
    state.model_info = BuildModelInfo(*result, metrics_);
  }

  // Note: There are several considerations that's involved in this loop:
  // - Should be robust against insertion/deletion to loading_state_ during
  // callback, thus the iterator should be considered invalid after callback
  // ran.
  // - Should be robust against addition of new callbacks during callback.
  //   We consider new additions to pending_callbacks to be a separate loading
  //   attempt and thus should be resolved by future results instead of
  //   current results.
  std::queue<base::OnceCallback<void()>> current_callbacks;
  current_callbacks.swap(state.pending_callbacks);
  while (!current_callbacks.empty()) {
    base::OnceCallback<void()> cb = std::move(current_callbacks.front());
    current_callbacks.pop();
    std::move(cb).Run();
  }
}

void DlcModelLoader::ResolveInstallResult(const base::Uuid& uuid,
                                          LoadCallback callback) {
  auto itr = loading_state_.find(uuid);
  CHECK(itr != loading_state_.end());
  DlcLoadingState& state = itr->second;

  if (!state.model_info.has_value()) {
    LOG(ERROR) << "Failed to load embedding model info for DLC: " << uuid;
    std::move(callback).Run(std::nullopt);
    return;
  }

  metrics_->SendEnumToUMA(kLoadDlcStatusHistogramName,
                          LoadDlcHistogram::kSuccess);
  std::move(callback).Run(*state.model_info);
}

}  // namespace embedding_model
