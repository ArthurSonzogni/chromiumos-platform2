// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/utils/dlc_client_helper.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/types/expected.h>
#include <ml_core/dlc/dlc_client.h>

namespace odml {

std::shared_ptr<DlcClientPtr> CreateDlcClient(const std::string& dlc_id,
                                              DlcInstallCallback callback,
                                              DlcProgressCallback progress) {
  std::shared_ptr<DlcClientPtr> shared_dlc_client =
      std::make_shared<DlcClientPtr>(nullptr);

  // Bind the lifetime of the dlc_client to the end of install callback.
  callback = std::move(callback).Then(base::BindOnce(
      [](std::shared_ptr<DlcClientPtr> dlc_client) {}, shared_dlc_client));

  std::pair<DlcInstallCallback, DlcInstallCallback> split =
      base::SplitOnceCallback(std::move(callback));

  DlcClientPtr dlc_client = cros::DlcClient::Create(
      dlc_id,
      base::BindOnce(
          [](DlcInstallCallback callback, const base::FilePath& path) {
            std::move(callback).Run(path);
          },
          std::move(split.first)),
      base::BindOnce(
          [](DlcInstallCallback callback, const std::string& path) {
            std::move(callback).Run(base::unexpected(path));
          },
          std::move(split.second)),
      progress);
  (*shared_dlc_client) = std::move(dlc_client);
  return shared_dlc_client;
}

}  // namespace odml
