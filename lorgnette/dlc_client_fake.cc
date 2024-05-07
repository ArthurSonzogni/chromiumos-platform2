// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "lorgnette/dlc_client_fake.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <chromeos/constants/lorgnette_dlc.h>
#include <dbus/bus.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

namespace lorgnette {

void DlcClientFake::InstallDlc(const std::set<std::string>& dlc_ids) {
  for (const std::string& id : dlc_ids) {
    OnDlcSuccess(id);
  }
}

void DlcClientFake::OnDlcSuccess(const std::string& dlc_id) {
  if (success_cb_) {
    success_cb_.Run(dlc_id, path_.Append(dlc_id));
  }
}

void DlcClientFake::SetCallbacks(
    base::RepeatingCallback<void(const std::string&, const base::FilePath&)>
        success_cb,
    base::RepeatingCallback<void(const std::string&, const std::string&)>
        failure_cb) {
  success_cb_ = std::move(success_cb);
  failure_cb_ = std::move(failure_cb);
}
}  // namespace lorgnette
