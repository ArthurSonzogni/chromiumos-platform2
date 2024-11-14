// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/platform_model_loader_chromeos.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/json/json_reader.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_number_conversions.h>
#include <base/types/expected.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <metrics/metrics_library.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/mojom/on_device_model.mojom-shared.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/on_device_model/on_device_model_service.h"
#include "odml/on_device_model/public/cpp/text_safety_assets.h"
#include "odml/utils/dlc_client_helper.h"

// The structure of the base model package:
//
// 6c2d5dc9-32c3-4642-9ea3-3dc9cdf3854d:
//   model.json
//   weights.bin
//
// The model.json content:
// {
//     "name": "My base model",
//     "version": "1.0.0.0",
//     "max_tokens": 1024,
//     "adaptation_ranks": [32],
//     "model_path": "model.pb",
//     "weight_path": "weights.bin",
//     "sp_model_path": "spm.model"
// }
//
// The structure of the adaptation layer model package:
//
// 75079ea6-c55a-44df-acce-7ac4cc861ee1:
//   model.json
//   weights.bin
//
// The model.json content:
// {
//     "name": "My adaptation model",
//     "version": "2.0.0.1",
//     "model_path": "model.pb",
//     "weight_path": "weights.bin",
//     "base_model": {
//         "uuid": "6c2d5dc9-32c3-4642-9ea3-3dc9cdf3854d",
//         "version": "1.0.0.0"
//     }
// }

namespace {
constexpr char kMlDlcPrefix[] = "ml-dlc-";
constexpr char kModelDescriptor[] = "model.json";
constexpr char kBaseModelKey[] = "base_model";
constexpr char kTsModelKey[] = "ts_model";
constexpr char kUuidKey[] = "uuid";
constexpr char kMaxTokensKey[] = "max_tokens";
constexpr char kAdaptationRanksKey[] = "adaptation_ranks";
constexpr char kWeightPathKey[] = "weight_path";
constexpr char kTsDataPathKey[] = "ts_data_path";
constexpr char kTsSpModelPathKey[] = "ts_sp_model_path";
constexpr char kVersionKey[] = "version";
constexpr char kBackendTypeKey[] = "backend_type";
constexpr char kSpModelPathKey[] = "sp_model_path";
constexpr int kDefaultMaxTokens = 1024;
constexpr char kLoadStatusHistogramName[] =
    "OnDeviceModel.LoadPlatformModelStatus";

constexpr char kNotInAVerifiedState[] = "is not in a verified state.";

constexpr double kFinishProgress = 1.0;
// The DLC download progress will consume the 50% of the progress bar.
constexpr double kDlcProgressRatio = 0.5;
// Remaining 1% for the model loading time.
constexpr double kBaseModelProgressRatio = 0.49;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class LoadStatus {
  kFirstLoadSuccess = 0,
  kLoadExistingSuccess = 1,
  kInvalidUuid = 2,
  kNoDlcClient = 3,
  kInstallDlcFail = 4,
  kReadModelDescriptorFail = 5,
  kParseModelDescriptorFail = 6,
  kInvalidModelDescriptor = 7,
  kInvalidBaseModelDescriptor = 8,
  kLoadModelFail = 9,
  kLoadBaseModelFail = 10,
  kInvalidModelVersion = 11,
  kMaxValue = kInvalidModelVersion,
};

class BaseModelProgressObserver
    : public on_device_model::mojom::PlatformModelProgressObserver {
 public:
  explicit BaseModelProgressObserver(
      base::RepeatingCallback<void(double progress)> callback)
      : callback_(std::move(callback)) {}
  ~BaseModelProgressObserver() override = default;

  // Creates the pending remote.
  mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
  BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  // The progress of the adaptation model.
  void Progress(double progress) override {
    callback_.Run(kDlcProgressRatio + progress * kBaseModelProgressRatio);
  }

 private:
  mojo::Receiver<on_device_model::mojom::PlatformModelProgressObserver>
      receiver_{this};
  base::RepeatingCallback<void(double progress)> callback_;
};

std::optional<on_device_model::mojom::ModelBackendType> BackendTypeFromString(
    const std::string& backend) {
  if (backend == "gpu") {
    return on_device_model::mojom::ModelBackendType::kGpu;
  }
  if (backend == "apu") {
    return on_device_model::mojom::ModelBackendType::kApu;
  }
  return std::nullopt;
}

}  // namespace

namespace on_device_model {

// static
template <typename ReceiverType>
bool ChromeosPlatformModelLoader::PlatformModelRefTraits<ReceiverType>::IsNull(
    const PointerType& ptr) {
  return !ptr && !ptr.get() && !ptr.get()->cur_model().get();
}

// static
template <typename ReceiverType>
ReceiverType* ChromeosPlatformModelLoader::PlatformModelRefTraits<
    ReceiverType>::GetRawPointer(PointerType* ptr) {
  return ptr->get()->cur_model().get();
}

ChromeosPlatformModelLoader::ChromeosPlatformModelLoader(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<OnDeviceModelService> service)
    : metrics_(metrics), service_(service) {}

ChromeosPlatformModelLoader::~ChromeosPlatformModelLoader() = default;

bool ChromeosPlatformModelLoader::ReplyModelAlreadyLoaded(
    const base::Uuid& uuid) {
  auto it = platform_models_.find(uuid);
  if (it == platform_models_.end()) {
    return false;
  }

  if (!it->second.platform_model && !it->second.ts_platform_model) {
    return false;
  }

  PlatformModelRecord& record = it->second;

  UpdateProgress(uuid, kFinishProgress);

  std::vector<PendingLoad> pending_loads = std::move(record.pending_loads);
  record.pending_loads.clear();

  for (auto& pending_load : pending_loads) {
    if (pending_load.pending) {
      receivers_.Add(base::WrapRefCounted(record.platform_model.get()),
                     std::move(pending_load.pending));
    }
    if (pending_load.ts_pending) {
      ts_receivers_.Add(base::WrapRefCounted(record.ts_platform_model.get()),
                        std::move(pending_load.ts_pending));
    }
    if (!pending_load.callback.is_null()) {
      std::move(pending_load.callback).Run(mojom::LoadModelResult::kSuccess);
    }
  }

  return true;
}

void ChromeosPlatformModelLoader::ReplyError(const base::Uuid& uuid,
                                             mojom::LoadModelResult result) {
  CHECK(result != mojom::LoadModelResult::kSuccess);

  auto it = platform_models_.find(uuid);
  if (it == platform_models_.end()) {
    return;
  }

  PlatformModelRecord& record = it->second;

  std::vector<PendingLoad> pending_loads = std::move(record.pending_loads);
  record.pending_loads.clear();

  for (auto& pending_load : pending_loads) {
    std::move(pending_load.callback).Run(result);
  }
}

void ChromeosPlatformModelLoader::LoadModelWithUuid(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::OnDeviceModel> pending,
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    LoadModelCallback callback) {
  if (!uuid.is_valid()) {
    LOG(ERROR) << "Invalid model UUID";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName, LoadStatus::kInvalidUuid);
    std::move(callback).Run(mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  platform_models_[uuid].pending_loads.push_back(PendingLoad{
      .pending = std::move(pending),
      .progress_observer = mojo::Remote<mojom::PlatformModelProgressObserver>(
          std::move(progress_observer)),
      .callback = std::move(callback),
  });

  LoadUuid(uuid);
}

void ChromeosPlatformModelLoader::LoadTextSafetyModelWithUuid(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::TextSafetyModel> pending,
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    LoadModelCallback callback) {
  if (!uuid.is_valid()) {
    LOG(ERROR) << "Invalid model UUID";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName, LoadStatus::kInvalidUuid);
    return;
  }

  platform_models_[uuid].pending_loads.push_back(PendingLoad{
      .ts_pending = std::move(pending),
      .progress_observer = mojo::Remote<mojom::PlatformModelProgressObserver>(
          std::move(progress_observer)),
      .callback = std::move(callback),
  });

  LoadUuid(uuid);
}

void ChromeosPlatformModelLoader::LoadUuid(const base::Uuid& uuid) {
  if (ReplyModelAlreadyLoaded(uuid)) {
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kLoadExistingSuccess);
    return;
  }

  PlatformModelRecord& record = platform_models_[uuid];
  if (record.pending_loads.size() > 1) {
    // Someone else is already loading the model.
    return;
  }
  record.progress = 0;

  if (record.dlc_path.has_value() && base::PathExists(*record.dlc_path)) {
    UpdateProgress(uuid, kDlcProgressRatio);
    LoadModelFromDlcPath(uuid, *record.dlc_path);
    return;
  }

  std::string uuid_str = uuid.AsLowercaseString();
  std::string dlc_id = kMlDlcPrefix + uuid_str;

  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      dlc_id,
      base::BindOnce(&ChromeosPlatformModelLoader::OnInstallDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), uuid),
      base::BindRepeating(&ChromeosPlatformModelLoader::OnDlcProgress,
                          weak_ptr_factory_.GetWeakPtr(), uuid));
  (*dlc_client)->InstallDlc();
}

void ChromeosPlatformModelLoader::GetModelState(
    const base::Uuid& uuid, GetModelStateCallback callback) {
  if (!uuid.is_valid()) {
    std::move(callback).Run(mojom::PlatformModelState::kInvalidUuid);
    return;
  }

  auto it = platform_models_.find(uuid);
  if (it != platform_models_.end() && it->second.platform_model) {
    std::move(callback).Run(mojom::PlatformModelState::kInstalledOnDisk);
    return;
  }

  std::string uuid_str = uuid.AsLowercaseString();
  std::string dlc_id = kMlDlcPrefix + uuid_str;

  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      dlc_id, base::BindOnce(
                  &ChromeosPlatformModelLoader::GetModelStateFromDlcState,
                  weak_ptr_factory_.GetWeakPtr(), uuid, std::move(callback)));
  (*dlc_client)->InstallVerifiedDlcOnly();
}

void ChromeosPlatformModelLoader::GetModelStateFromDlcState(
    const base::Uuid& uuid,
    GetModelStateCallback callback,
    base::expected<base::FilePath, std::string> result) {
  if (!result.has_value()) {
    mojom::PlatformModelState error =
        mojom::PlatformModelState::kInvalidDlcPackage;
    if (result.error().find(kNotInAVerifiedState) != std::string::npos) {
      error = mojom::PlatformModelState::kInvalidDlcVerifiedState;
    }
    std::move(callback).Run(error);
    return;
  }

  const base::FilePath& dlc_root = result.value();
  base::FilePath model_desc = dlc_root.Append(kModelDescriptor);
  std::string model_json;

  if (!base::ReadFileToString(model_desc, &model_json)) {
    std::move(callback).Run(mojom::PlatformModelState::kInvalidModelFormat);
    return;
  }

  std::optional<base::Value::Dict> model_dict =
      base::JSONReader::ReadDict(model_json);
  if (!model_dict) {
    std::move(callback).Run(mojom::PlatformModelState::kInvalidModelDescriptor);
    return;
  }

  const base::Value::Dict* base_model = model_dict->FindDict(kBaseModelKey);

  if (base_model) {
    // This is an adaptation layer model. We need to load the base model first.
    const std::string* base_uuid = base_model->FindString(kUuidKey);
    if (!base_uuid) {
      std::move(callback).Run(
          mojom::PlatformModelState::kInvalidBaseModelDescriptor);
      return;
    }

    base::Uuid base_model_uuid = base::Uuid::ParseLowercase(*base_uuid);

    GetModelState(base_model_uuid, std::move(callback));
    return;
  }

  std::move(callback).Run(mojom::PlatformModelState::kInstalledOnDisk);
}

void ChromeosPlatformModelLoader::OnInstallDlcComplete(
    const base::Uuid& uuid,
    base::expected<base::FilePath, std::string> result) {
  if (!result.has_value()) {
    LOG(ERROR) << "Failed to install ML DLC: " << result.error();
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kInstallDlcFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }
  LoadModelFromDlcPath(uuid, result.value());
}

void ChromeosPlatformModelLoader::LoadModelFromDlcPath(
    const base::Uuid& uuid, const base::FilePath& dlc_root) {
  base::FilePath model_desc = dlc_root.Append(kModelDescriptor);
  std::string model_json;

  if (!base::ReadFileToString(model_desc, &model_json)) {
    LOG(ERROR) << "Failed to read model descriptor file";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kReadModelDescriptorFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  std::optional<base::Value::Dict> model_dict =
      base::JSONReader::ReadDict(model_json);

  if (!model_dict) {
    LOG(ERROR) << "Failed to parse model descriptor file";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kParseModelDescriptorFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  // Cache dlc path after confirming the dict can be read and parsed.
  platform_models_[uuid].dlc_path = dlc_root;

  const std::string* version = model_dict->FindString(kVersionKey);
  if (!version) {
    LOG(ERROR) << "Failed to read model version from model descriptor file";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kInvalidModelDescriptor);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  const std::string* ts_data = model_dict->FindString(kTsDataPathKey);
  const std::string* ts_sp_model = model_dict->FindString(kTsSpModelPathKey);
  if (ts_data && ts_sp_model) {
    // This is a TS model.
    TextSafetyLoaderParams params;
    params.ts_paths.emplace();

    params.ts_paths->data = dlc_root.Append(base::FilePath(*ts_data));
    params.ts_paths->sp_model = dlc_root.Append(base::FilePath(*ts_sp_model));
    auto platform_model =
        base::MakeRefCounted<PlatformModel<mojom::TextSafetyModel>>();
    service_->LoadTextSafetyModel(
        LoadTextSafetyParams(params),
        platform_model->cur_model().BindNewPipeAndPassReceiver());
    FinishLoadTsModel(uuid, *version, std::move(platform_model),
                      mojom::LoadModelResult::kSuccess);
    return;
  }

  const std::string* weight_path = model_dict->FindString(kWeightPathKey);
  if (!weight_path) {
    LOG(ERROR) << "Failed to read model data from model descriptor file";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kInvalidModelDescriptor);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  const base::Value::Dict* base_model = model_dict->FindDict(kBaseModelKey);

  if (base_model) {
    // This is an adaptation layer model. We need to load the base model first.
    const std::string* base_uuid = base_model->FindString(kUuidKey);
    const std::string* base_version = base_model->FindString(kVersionKey);
    if (!base_uuid || !base_version) {
      LOG(ERROR) << "Failed to read base model data from model descriptor file";
      metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                              LoadStatus::kInvalidBaseModelDescriptor);
      ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
      return;
    }

    PlatformModelRecord& record = platform_models_[uuid];
    base::Uuid base_model_uuid = base::Uuid::ParseLowercase(*base_uuid);

    auto progress_observer = std::make_unique<BaseModelProgressObserver>(
        base::BindRepeating(&ChromeosPlatformModelLoader::UpdateProgress,
                            weak_ptr_factory_.GetWeakPtr(), uuid));
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> pending_remote =
        progress_observer->BindRemote();
    record.base_model_observer = std::move(progress_observer);

    auto platform_model =
        base::MakeRefCounted<PlatformModel<mojom::OnDeviceModel>>();
    mojo::PendingReceiver<mojom::OnDeviceModel> pending =
        platform_model->base_model().BindNewPipeAndPassReceiver();
    LoadModelWithUuid(
        base_model_uuid, std::move(pending), std::move(pending_remote),
        base::BindOnce(
            &ChromeosPlatformModelLoader::LoadAdaptationPlatformModel,
            weak_ptr_factory_.GetWeakPtr(), base_model_uuid, *base_version,
            uuid, dlc_root, *version, *weight_path, std::move(platform_model)));

    return;
  }

  const base::Value::Dict* ts_model = model_dict->FindDict(kTsModelKey);
  if (ts_model) {
    // We need to load the TS model.
    const std::string* ts_uuid = ts_model->FindString(kUuidKey);
    const std::string* ts_version = ts_model->FindString(kVersionKey);
    if (!ts_uuid || !ts_version) {
      LOG(ERROR) << "Failed to read ts model data from model descriptor file";
      metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                              LoadStatus::kInvalidBaseModelDescriptor);
      ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
      return;
    }

    PlatformModelRecord& record = platform_models_[uuid];
    base::Uuid ts_model_uuid = base::Uuid::ParseLowercase(*ts_uuid);

    auto progress_observer = std::make_unique<BaseModelProgressObserver>(
        base::BindRepeating(&ChromeosPlatformModelLoader::UpdateProgress,
                            weak_ptr_factory_.GetWeakPtr(), uuid));
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> pending_remote =
        progress_observer->BindRemote();
    record.base_model_observer = std::move(progress_observer);
    auto platform_model =
        base::MakeRefCounted<PlatformModel<mojom::OnDeviceModel>>();
    LoadTextSafetyModelWithUuid(
        ts_model_uuid, platform_model->ts_model().BindNewPipeAndPassReceiver(),
        std::move(pending_remote),
        base::BindOnce(&ChromeosPlatformModelLoader::LoadBasePlatformModel,
                       weak_ptr_factory_.GetWeakPtr(), std::move(model_dict),
                       uuid, dlc_root, *version, *weight_path,
                       std::move(platform_model)));
    return;
  }

  auto platform_model =
      base::MakeRefCounted<PlatformModel<mojom::OnDeviceModel>>();
  LoadBasePlatformModel(model_dict, uuid, dlc_root, *version, *weight_path,
                        std::move(platform_model),
                        mojom::LoadModelResult::kSuccess);
}

void ChromeosPlatformModelLoader::OnDlcProgress(const base::Uuid& uuid,
                                                double progress) {
  UpdateProgress(uuid, progress * kDlcProgressRatio);
}

void ChromeosPlatformModelLoader::UpdateProgress(const base::Uuid& uuid,
                                                 double progress) {
  PlatformModelRecord& record = platform_models_[uuid];
  if (record.progress >= progress) {
    return;
  }
  record.progress = progress;

  for (const auto& pending_load : record.pending_loads) {
    if (pending_load.progress_observer) {
      pending_load.progress_observer->Progress(progress);
    }
  }
}

void ChromeosPlatformModelLoader::FinishLoadModel(
    const base::Uuid& uuid,
    const std::string& version,
    scoped_refptr<PlatformModel<mojom::OnDeviceModel>> model,
    mojom::LoadModelResult result) {
  if (result != mojom::LoadModelResult::kSuccess) {
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kLoadModelFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  CHECK(model);
  model->version() = version;
  platform_models_[uuid].platform_model = model->AsWeakPtr();

  CHECK(ReplyModelAlreadyLoaded(uuid));
  metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                          LoadStatus::kFirstLoadSuccess);
}

void ChromeosPlatformModelLoader::FinishLoadTsModel(
    const base::Uuid& uuid,
    const std::string& version,
    scoped_refptr<PlatformModel<mojom::TextSafetyModel>> ts_model,
    mojom::LoadModelResult result) {
  if (result != mojom::LoadModelResult::kSuccess) {
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kLoadModelFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  CHECK(ts_model);
  ts_model->version() = version;
  platform_models_[uuid].ts_platform_model = ts_model->AsWeakPtr();

  CHECK(ReplyModelAlreadyLoaded(uuid));
  metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                          LoadStatus::kFirstLoadSuccess);
}

void ChromeosPlatformModelLoader::LoadAdaptationPlatformModel(
    const base::Uuid& base_uuid,
    const std::string& base_version,
    const base::Uuid& uuid,
    const base::FilePath& dlc_root,
    const std::string& version,
    const std::string& weight_path,
    scoped_refptr<PlatformModel<mojom::OnDeviceModel>> model,
    mojom::LoadModelResult result) {
  if (result != mojom::LoadModelResult::kSuccess) {
    LOG(ERROR) << "Failed to load base model for adaptation";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kLoadBaseModelFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  PlatformModelRecord& base_record = platform_models_[base_uuid];
  CHECK(base_record.platform_model);

  if (base_record.platform_model->version() != base_version) {
    LOG(ERROR) << "Base model version mismatch or no valid base model";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kInvalidModelVersion);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  // GPU backend sends the opened weight file to ML APIs directly. Other
  // backends send the weight file path.
  mojom::ModelBackendType backend_type =
      base_record.platform_model->backend_type();
  auto params = on_device_model::mojom::LoadAdaptationParams::New();
  if (backend_type == mojom::ModelBackendType::kGpu) {
    on_device_model::AdaptationAssetPaths adaptation_paths;
    adaptation_paths.weights = dlc_root.Append(weight_path);
    params->assets = on_device_model::LoadAdaptationAssets(adaptation_paths);
  } else {
    on_device_model::AdaptationAssets assets;
    assets.weights_path = dlc_root.Append(weight_path);
    params->assets = std::move(assets);
  }

  mojo::PendingReceiver<mojom::OnDeviceModel> pending =
      model->cur_model().BindNewPipeAndPassReceiver();
  base_record.platform_model->cur_model()->LoadAdaptation(
      std::move(params), std::move(pending),
      base::BindOnce(&ChromeosPlatformModelLoader::FinishLoadModel,
                     weak_ptr_factory_.GetWeakPtr(), uuid, version,
                     std::move(model)));
}

void ChromeosPlatformModelLoader::LoadBasePlatformModel(
    const std::optional<base::Value::Dict>& model_dict,
    const base::Uuid& uuid,
    const base::FilePath& dlc_root,
    const std::string& version,
    const std::string& weight_path,
    scoped_refptr<PlatformModel<mojom::OnDeviceModel>> model,
    mojom::LoadModelResult result) {
  if (result != mojom::LoadModelResult::kSuccess) {
    LOG(ERROR) << "Failed to load TS model of base model";
    metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                            LoadStatus::kLoadBaseModelFail);
    ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  // Default to GPU as that was the only existing backend.
  on_device_model::mojom::ModelBackendType backend_type =
      on_device_model::mojom::ModelBackendType::kGpu;
  const std::string* backend_type_str = model_dict->FindString(kBackendTypeKey);
  if (backend_type_str) {
    std::optional<on_device_model::mojom::ModelBackendType>
        parsed_backend_type = BackendTypeFromString(*backend_type_str);
    if (!parsed_backend_type.has_value()) {
      LOG(ERROR) << "Failed to recognize model backend type: "
                 << *backend_type_str;
      metrics_->SendEnumToUMA(kLoadStatusHistogramName,
                              LoadStatus::kReadModelDescriptorFail);
      ReplyError(uuid, mojom::LoadModelResult::kFailedToLoadLibrary);
      return;
    }
    backend_type = *parsed_backend_type;
  }

  std::optional<int> max_tokens = model_dict->FindInt(kMaxTokensKey);

  const base::Value::List* ada_list = model_dict->FindList(kAdaptationRanksKey);
  std::vector<uint32_t> adaptation_ranks;
  if (ada_list) {
    for (auto& ada : *ada_list) {
      std::optional<int> rank = ada.GetIfInt();
      if (rank) {
        adaptation_ranks.push_back(*rank);
      }
    }
  }

  const std::string* sp_model_path = model_dict->FindString(kSpModelPathKey);

  on_device_model::ModelAssetPaths model_paths;
  // GPU backend sends the opened weight file to ML APIs directly. Other
  // backends send the weight file path, optionally with the sentence piece
  // model path.
  if (backend_type == on_device_model::mojom::ModelBackendType::kGpu) {
    model_paths.weights = dlc_root.Append(weight_path);
  }
  on_device_model::ModelAssets model_assets =
      on_device_model::LoadModelAssets(model_paths);
  if (backend_type != on_device_model::mojom::ModelBackendType::kGpu) {
    model_assets.weights_path = dlc_root.Append(weight_path);
    if (sp_model_path) {
      model_assets.sp_model_path = dlc_root.Append(*sp_model_path);
    }
  }

  auto params = on_device_model::mojom::LoadModelParams::New();
  params->backend_type = backend_type;
  params->assets = std::move(model_assets);
  params->max_tokens = max_tokens.value_or(kDefaultMaxTokens);
  params->adaptation_ranks = adaptation_ranks;

  model->backend_type() = backend_type;
  mojom::TextSafetyModel* ts_model = nullptr;
  if (model->ts_model()) {
    ts_model = model->ts_model().get();
  }
  mojo::PendingReceiver<mojom::OnDeviceModel> pending =
      model->cur_model().BindNewPipeAndPassReceiver();
  service_->LoadModel(
      std::move(params), std::move(pending),
      base::BindOnce(&ChromeosPlatformModelLoader::FinishLoadModel,
                     weak_ptr_factory_.GetWeakPtr(), uuid, version,
                     std::move(model)),
      ts_model);
}
}  // namespace on_device_model
