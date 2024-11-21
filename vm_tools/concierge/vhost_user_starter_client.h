// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VHOST_USER_STARTER_CLIENT_H_
#define VM_TOOLS_CONCIERGE_VHOST_USER_STARTER_CLIENT_H_

#include <string>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/sequence_checker.h>
#include <brillo/errors/error.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <vhost_user_starter/proto_bindings/vhost_user_starter.pb.h>
#include <vm_tools/vhost_user_starter/dbus-proxies.h>

#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {

// Provides a proxy connection to the vhost_user_starter dbus service.
class VhostUserStarterClient final {
 public:
  explicit VhostUserStarterClient(scoped_refptr<dbus::Bus> bus);
  VhostUserStarterClient(const VhostUserStarterClient&) = delete;
  VhostUserStarterClient& operator=(const VhostUserStarterClient&) = delete;

  ~VhostUserStarterClient() = default;

  // Pass socket to vhost_user fs
  void StartVhostUserFs(const std::vector<base::ScopedFD>& in_socket,
                        const SharedDataParam& param);
  int GetStartedDeviceCnt() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return started_device_count;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  org::chromium::VhostUserStarterProxy vhost_user_starter_proxy_;

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);

  int started_device_count GUARDED_BY_CONTEXT(sequence_checker_) = 0;

  void StartVhostUserFsSuccessCallback(
      const vm_tools::vhost_user_starter::StartVhostUserFsResponse& response);
  void StartVhostUserFsErrorCallback(brillo::Error* error);

  base::WeakPtrFactory<VhostUserStarterClient> weak_factory_
      GUARDED_BY_CONTEXT(sequence_checker_);
  // weak_factory_ should be the last field of class.
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VHOST_USER_STARTER_CLIENT_H_
