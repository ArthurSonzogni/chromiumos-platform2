// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_DBUS_DBUS_ADAPTOR_H_
#define REGMON_DBUS_DBUS_ADAPTOR_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "regmon/proto/policy_violation.pb.h"
#include "regmon/regmon/regmon_service.h"

// Must be located after all proto declarations
#include "dbus_adaptors/org.chromium.Regmond.h"

namespace regmon {

class DBusAdaptor : public org::chromium::RegmondAdaptor,
                    public org::chromium::RegmondInterface {
 public:
  DBusAdaptor(scoped_refptr<dbus::Bus> bus,
              std::unique_ptr<RegmonService> regmon);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // Forward org::chromium::RegmondInterface
  void RecordPolicyViolation(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          RecordPolicyViolationResponse>> out_response,
      const RecordPolicyViolationRequest& in_request) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  std::unique_ptr<RegmonService> regmon_;
};
}  // namespace regmon

#endif  // REGMON_DBUS_DBUS_ADAPTOR_H_
