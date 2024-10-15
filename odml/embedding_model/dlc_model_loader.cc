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

constexpr char kMlDlcPrefix[] = "ml-dlc-";
constexpr char kModelDescriptor[] = "model.json";

std::optional<ModelInfo> ParseModelInfo(const base::Value::Dict& model_dict,
                                        const base::FilePath& dlc_root) {
  // TODO(b/364243814): Implement this.
  return std::nullopt;
}

std::optional<ModelInfo> BuildModelInfo(const base::FilePath& dlc_root) {
  base::FilePath model_desc = dlc_root.Append(kModelDescriptor);
  std::string model_json;

  if (!base::ReadFileToString(model_desc, &model_json)) {
    LOG(ERROR) << "Failed to read model descriptor file for embedding model: "
               << dlc_root;
    return std::nullopt;
  }

  std::optional<base::Value::Dict> model_dict =
      base::JSONReader::ReadDict(model_json);

  if (!model_dict) {
    LOG(ERROR) << "Failed to parse model descriptor file for DLC " << dlc_root;
    return std::nullopt;
  }

  return ParseModelInfo(*model_dict, dlc_root);
}

}  // namespace

void DlcModelLoader::LoadDlcWithUuid(const base::Uuid& uuid,
                                     LoadCallback callback) {
  if (!uuid.is_valid()) {
    LOG(ERROR) << "Invalid UUID for loading embedding model.";
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
    LOG(ERROR) << "Failed to install embedding model ML DLC: "
               << result.error();
  } else {
    state.model_info = BuildModelInfo(*result);
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

  std::move(callback).Run(*state.model_info);
}

}  // namespace embedding_model
