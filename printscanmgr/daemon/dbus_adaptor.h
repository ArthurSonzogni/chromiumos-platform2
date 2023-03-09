// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTSCANMGR_DAEMON_DBUS_ADAPTOR_H_
#define PRINTSCANMGR_DAEMON_DBUS_ADAPTOR_H_

#include <stdint.h>

#include <string>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/errors/error.h>
#include <dbus/bus.h>

#include "printscanmgr/daemon/cups_tool.h"
#include "printscanmgr/dbus_adaptors/org.chromium.printscanmgr.h"

namespace printscanmgr {

// Implementation of org::chromium::printscanmgrInterface.
class DbusAdaptor final : public org::chromium::printscanmgrAdaptor,
                          public org::chromium::printscanmgrInterface {
 public:
  explicit DbusAdaptor(scoped_refptr<dbus::Bus> bus);
  DbusAdaptor(const DbusAdaptor&) = delete;
  DbusAdaptor& operator=(const DbusAdaptor&) = delete;
  ~DbusAdaptor() override;

  // Registers the D-Bus object and interface.
  void RegisterAsync(brillo::dbus_utils::AsyncEventSequencer::CompletionAction
                         completion_action);

  // org::chromium::printscanmgrInterface overrides:
  int32_t CupsAddAutoConfiguredPrinter(const std::string& name,
                                       const std::string& uri) override;
  int32_t CupsAddManuallyConfiguredPrinter(
      const std::string& name,
      const std::string& uri,
      const std::vector<uint8_t>& ppd_contents) override;
  bool CupsRemovePrinter(const std::string& name) override;
  std::vector<uint8_t> CupsRetrievePpd(const std::string& name) override;
  bool PrintscanDebugSetCategories(brillo::ErrorPtr* error,
                                   uint32_t categories) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  CupsTool cups_tool_;
};

}  // namespace printscanmgr

#endif  // PRINTSCANMGR_DAEMON_DBUS_ADAPTOR_H_
