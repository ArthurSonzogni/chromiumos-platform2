// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "lorgnette/dlc_client_fake.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <dbus/bus.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

namespace lorgnette {

void DlcClientFake::InstallDlc() {
  OnDlcSuccess();
}

void DlcClientFake::OnDlcSuccess() {
  if (success_cb_) {
    std::move(success_cb_).Run(path_);
  }
}

void DlcClientFake::SetCallbacks(
    base::OnceCallback<void(const base::FilePath&)> success_cb,
    base::OnceCallback<void(const std::string&)> failure_cb) {
  success_cb_ = std::move(success_cb);
  failure_cb_ = std::move(failure_cb);
}
}  // namespace lorgnette
