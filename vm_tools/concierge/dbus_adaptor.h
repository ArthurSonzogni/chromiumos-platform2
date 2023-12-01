// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_DBUS_ADAPTOR_H_
#define VM_TOOLS_CONCIERGE_DBUS_ADAPTOR_H_

#include <brillo/dbus/dbus_object.h>
#include <memory>

#include "base/functional/callback_forward.h"
#include "dbus/bus.h"
#include "vm_concierge/concierge_service.pb.h"

#include "vm_tools/concierge/dbus_adaptors/org.chromium.VmConcierge.h"

namespace vm_tools::concierge {

// Helper class to manage concierge's externally-visible D-Bus API in a
// Scoped way.
class DbusAdaptor : public org::chromium::VmConciergeAdaptor {
 public:
  // Make concierge's API available to external callers on |bus|. Invokes RPC
  // methods of |interface| on |bus|'s origin thread. Invokes |on_created| with
  // a handle to the DbusAdaptor, if setup succeeds, or with nullptr, if
  // setup fails.
  static void Create(
      scoped_refptr<dbus::Bus> bus,
      org::chromium::VmConciergeInterface* interface,
      base::OnceCallback<void(std::unique_ptr<DbusAdaptor>)> on_created);

  ~DbusAdaptor();

 private:
  DbusAdaptor(scoped_refptr<dbus::Bus> bus,
              org::chromium::VmConciergeInterface* interface);

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_DBUS_ADAPTOR_H_
