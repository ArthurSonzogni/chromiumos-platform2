// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_INSTALLER_H_
#define DLCSERVICE_INSTALLER_H_

#include <string>

#include <base/functional/callback.h>
#include <brillo/errors/error.h>

#include "dlcservice/types.h"

namespace dlcservice {

class InstallerInterface {
 public:
  using InstallSuccessCallback = base::OnceCallback<void()>;
  using InstallFailureCallback = base::OnceCallback<void(brillo::Error*)>;

  struct InstallArgs {
    DlcId id;
    std::string url;
    bool scaled = false;
    bool force_ota = false;
  };

  InstallerInterface() = default;
  virtual ~InstallerInterface() = default;

  virtual void Install(const InstallArgs& install_args,
                       InstallSuccessCallback success_callback,
                       InstallFailureCallback failure_callback) = 0;
};

class Installer : public InstallerInterface {
 public:
  Installer() = default;
  ~Installer() override = default;

  Installer(const Installer&) = delete;
  Installer& operator=(const Installer&) = delete;

  void Install(const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback) override;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_INSTALLER_H_
