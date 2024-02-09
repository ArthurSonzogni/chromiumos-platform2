// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/dlcservice_chromeos.h"

#include <base/logging.h>
#include <brillo/errors/error.h>
#include <chromeos/constants/imageloader.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
// NOLINTNEXTLINE(build/include_alpha) "dbus-proxies.h" needs "dlcservice.pb.h"
#include <dlcservice/dbus-proxies.h>
#include <libdlcservice/utils.h>

#include "update_engine/cros/dbus_connection.h"

using std::string;
using std::vector;

namespace chromeos_update_engine {

namespace {
org::chromium::DlcServiceInterfaceProxy GetDlcServiceProxy() {
  return {DBusConnection::Get()->GetDBus()};
}
}  // namespace

std::unique_ptr<DlcServiceInterface> CreateDlcService() {
  return std::make_unique<DlcServiceChromeOS>();
}

std::unique_ptr<DlcUtilsInterface> CreateDlcUtils() {
  return std::make_unique<DlcUtilsChromeOS>();
}

bool DlcServiceChromeOS::GetDlcsToUpdate(vector<string>* dlc_ids) {
  if (!dlc_ids)
    return false;
  dlc_ids->clear();

  brillo::ErrorPtr err;
  if (!GetDlcServiceProxy().GetDlcsToUpdate(dlc_ids, &err)) {
    LOG(ERROR) << "dlcservice failed to return DLCs that need to be updated. "
               << "ErrorCode=" << err->GetCode()
               << ", ErrMsg=" << err->GetMessage();
    dlc_ids->clear();
    return false;
  }
  return true;
}

bool DlcServiceChromeOS::InstallCompleted(const vector<string>& dlc_ids) {
  brillo::ErrorPtr err;
  if (!GetDlcServiceProxy().InstallCompleted(dlc_ids, &err)) {
    LOG(ERROR) << "dlcservice failed to complete install. ErrCode="
               << err->GetCode() << ", ErrMsg=" << err->GetMessage();
    return false;
  }
  return true;
}

bool DlcServiceChromeOS::UpdateCompleted(const vector<string>& dlc_ids) {
  brillo::ErrorPtr err;
  if (!GetDlcServiceProxy().UpdateCompleted(dlc_ids, &err)) {
    LOG(ERROR) << "dlcservice failed to complete updated. ErrCode="
               << err->GetCode() << ", ErrMsg=" << err->GetMessage();
    return false;
  }
  return true;
}

std::shared_ptr<imageloader::Manifest> DlcUtilsChromeOS::GetDlcManifest(
    const std::string& id, const base::FilePath& dlc_manifest_path) {
  return utils_.GetDlcManifest(id, dlc_manifest_path);
}

}  // namespace chromeos_update_engine
