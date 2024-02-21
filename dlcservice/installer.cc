// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/installer.h"

#include <utility>

#include "dlcservice/system_state.h"

namespace dlcservice {

void Installer::Install(const InstallArgs& install_args,
                        InstallSuccessCallback success_callback,
                        InstallFailureCallback failure_callback) {
  update_engine::InstallParams install_params;
  install_params.set_id(install_args.id);
  install_params.set_omaha_url(install_args.url);
  install_params.set_scaled(install_args.scaled);
  install_params.set_force_ota(install_args.force_ota);
  SystemState::Get()->update_engine()->InstallAsync(
      install_params, std::move(success_callback), std::move(failure_callback));
}

}  // namespace dlcservice
