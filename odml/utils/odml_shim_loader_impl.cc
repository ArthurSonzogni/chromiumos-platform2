// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/utils/odml_shim_loader_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/native_library.h>
#include <base/scoped_native_library.h>
#include <ml_core/dlc/dlc_client.h>

namespace {
constexpr char kOdmlShimDlc[] = "odml-shim";
constexpr char kOdmlShimLibraryName[] = "libodml_shim.so";
}  // namespace

namespace odml {

OdmlShimLoaderImpl::OdmlShimLoaderImpl() = default;

bool OdmlShimLoaderImpl::IsShimReady() {
  return library_.is_valid();
}

void OdmlShimLoaderImpl::EnsureShimReady(
    base::OnceCallback<void(bool)> callback) {
  using InstallCallback =
      base::OnceCallback<void(base::expected<base::FilePath, std::string>)>;
  using DlcClientPtr = std::unique_ptr<cros::DlcClient>;
  std::shared_ptr<DlcClientPtr> shared_dlc_client =
      std::make_shared<DlcClientPtr>(nullptr);
  // Bind the lifetime of the dlc_client to the end of install callback.
  InstallCallback install_cb =
      base::BindOnce(&OdmlShimLoaderImpl::OnInstallDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback))
          .Then(base::BindOnce([](std::shared_ptr<DlcClientPtr> dlc_client) {},
                               shared_dlc_client));
  auto split = base::SplitOnceCallback(std::move(install_cb));
  auto dlc_client = cros::DlcClient::Create(
      kOdmlShimDlc,
      base::BindOnce(
          [](InstallCallback callback, const base::FilePath& path) {
            std::move(callback).Run(path);
          },
          std::move(split.first)),
      base::BindOnce(
          [](InstallCallback callback, const std::string& path) {
            std::move(callback).Run(base::unexpected(path));
          },
          std::move(split.second)));
  dlc_client->InstallDlc();
  (*shared_dlc_client) = std::move(dlc_client);
}

void OdmlShimLoaderImpl::OnInstallDlcComplete(
    base::OnceCallback<void(bool)> callback,
    base::expected<base::FilePath, std::string> result) {
  if (!result.has_value()) {
    LOG(ERROR) << "Failed to install odml-shim: " << result.error();
    std::move(callback).Run(false);
    return;
  }

  const base::FilePath& dlc_root = result.value();
  base::FilePath library_name = dlc_root.Append(kOdmlShimLibraryName);

  base::NativeLibraryLoadError error;
  base::NativeLibrary library = base::LoadNativeLibrary(library_name, &error);
  if (!library) {
    LOG(ERROR) << "Error loading native library: " << error.ToString();
    std::move(callback).Run(false);
    return;
  }

  library_ = base::ScopedNativeLibrary(library);
  std::move(callback).Run(true);
}

void* OdmlShimLoaderImpl::GetFunctionPointer(const std::string& name) {
  if (!library_.is_valid()) {
    return nullptr;
  }

  return library_.GetFunctionPointer(name.c_str());
}

}  // namespace odml
