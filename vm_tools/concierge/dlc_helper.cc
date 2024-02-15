// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/dlc_helper.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/errors/error.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

// Needs to be included after dlcservice.pb.h
#include <dlcservice/dbus-proxies.h>  // NOLINT (build/include_alpha)

namespace vm_tools::concierge {

DlcHelper::DlcHelper(
    std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface> handle)
    : dlcservice_handle_(std::move(handle)) {}

DlcHelper::DlcHelper(const scoped_refptr<dbus::Bus>& bus)
    : DlcHelper(
          std::make_unique<org::chromium::DlcServiceInterfaceProxy>(bus)) {}

DlcHelper::~DlcHelper() = default;

base::expected<base::FilePath, std::string> DlcHelper::GetRootPath(
    const std::string& dlc_id) {
  dlcservice::DlcState state;
  brillo::ErrorPtr error;
  if (!dlcservice_handle_->GetDlcState(dlc_id, &state, &error)) {
    if (error) {
      return base::unexpected(
          base::StrCat({"Error calling dlcservice (code=", error->GetCode(),
                        "): ", error->GetMessage()}));
    }
    return base::unexpected("Error calling dlcservice: unknown");
  }

  if (state.state() != dlcservice::DlcState_State_INSTALLED) {
    return base::unexpected(
        base::StrCat({dlc_id, " was not installed, its state is: ",
                      base::NumberToString(state.state()),
                      " with last error: ", state.last_error_code()}));
  }

  if (state.root_path().empty()) {
    return base::unexpected("Root path was empty");
  }

  return base::ok(base::FilePath(state.root_path()));
}

}  // namespace vm_tools::concierge
