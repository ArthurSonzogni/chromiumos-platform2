// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_ADAPTOR_H_
#define DLP_DLP_ADAPTOR_H_

#include <memory>
#include <vector>

#include <brillo/dbus/async_event_sequencer.h>
#include "dlp/org.chromium.Dlp.h"
#include "dlp/proto_bindings/dlp_service.pb.h"

namespace brillo {
namespace dbus_utils {
class DBusObject;
}
}  // namespace brillo

namespace dlp {

class DlpAdaptor : public org::chromium::DlpAdaptor,
                   public org::chromium::DlpInterface {
 public:
  explicit DlpAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object);
  DlpAdaptor(const DlpAdaptor&) = delete;
  DlpAdaptor& operator=(const DlpAdaptor&) = delete;
  virtual ~DlpAdaptor();

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          completion_callback);

  // org::chromium::DlpInterface: (see org.chromium.Dlp.xml).
  std::vector<uint8_t> SetDlpFilesPolicy(
      const std::vector<uint8_t>& request_blob) override;

 private:
  std::vector<DlpFilesRule> policy_rules_;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace dlp

#endif  // DLP_DLP_ADAPTOR_H_
