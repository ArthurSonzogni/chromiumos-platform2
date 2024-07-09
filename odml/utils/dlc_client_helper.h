// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_UTILS_DLC_CLIENT_HELPER_H_
#define ODML_UTILS_DLC_CLIENT_HELPER_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/types/expected.h>
#include <ml_core/dlc/dlc_client.h>

namespace odml {

using DlcClientPtr = std::unique_ptr<cros::DlcClient>;
using DlcInstallCallback =
    base::OnceCallback<void(base::expected<base::FilePath, std::string>)>;
using DlcProgressCallback = base::RepeatingCallback<void(double)>;

std::shared_ptr<DlcClientPtr> CreateDlcClient(
    const std::string& dlc_id,
    DlcInstallCallback callback,
    DlcProgressCallback progress = base::NullCallback());

}  // namespace odml

#endif  // ODML_UTILS_DLC_CLIENT_HELPER_H_
