// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_CORE_DLC_DLC_LOADER_H_
#define ML_CORE_DLC_DLC_LOADER_H_

#include <memory>
#include <optional>
#include <string>

#include <brillo/daemons/daemon.h>

#include "ml_core/dlc/dlc_client.h"

namespace cros {

// Class to load a DLC, primarily designed
// around a CLI application. Create an instance of this class,
// call Run(), and after the method returns you can use DlcLoaded()
// to check if it was successful, and GetDlcRootPath() for the
// root directory of the installed DLC.
class DlcLoader : public brillo::Daemon {
 public:
  explicit DlcLoader(
      const std::string& dlc_id,
      std::optional<std::string> metrics_base_name = std::nullopt);
  ~DlcLoader() override = default;

  bool DlcLoaded();
  const base::FilePath& GetDlcRootPath();

 private:
  const std::string dlc_id_;
  const std::optional<std::string> metrics_base_name_;
  int OnEventLoopStarted() override;
  base::FilePath dlc_root_path_;
  std::unique_ptr<cros::DlcClient> dlc_client_;
};

}  // namespace cros

#endif  // ML_CORE_DLC_DLC_LOADER_H_
