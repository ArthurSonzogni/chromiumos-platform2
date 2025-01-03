// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_STARTUP_LISTENER_IMPL_H_
#define VM_TOOLS_CONCIERGE_STARTUP_LISTENER_IMPL_H_

#include <stdint.h>

#include <cstdint>
#include <map>

#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/vm_host.grpc.pb.h>
#include <vm_protos/proto_bindings/vm_host.pb.h>

#include "base/functional/callback.h"

namespace vm_tools::concierge {

// Listens for VMs to announce that they are ready before signaling the
// WaitableEvent associated with that VM.
class StartupListenerImpl final : public vm_tools::StartupListener::Service {
 public:
  StartupListenerImpl() = default;
  StartupListenerImpl(const StartupListenerImpl&) = delete;
  StartupListenerImpl& operator=(const StartupListenerImpl&) = delete;

  ~StartupListenerImpl() override = default;

  // StartupListener overrides.
  grpc::Status VmReady(grpc::ServerContext* ctx,
                       const vm_tools::EmptyMessage* request,
                       vm_tools::EmptyMessage* response) override;
  grpc::Status VmInstallStatus(grpc::ServerContext* ctx,
                               const vm_tools::VmInstallState* status,
                               vm_tools::EmptyMessage* response) override;

  // Add the VM with the vsock context id |cid| to the set of VMs that have
  // been started but have not checked in as ready yet. |event_fd| will be
  // signaled when the VM is ready. It's lifetime should be owned by the client.
  void AddPendingVm(uint32_t cid, int32_t event_fd);

  // Remove the WaitableEvent associated with |cid|.
  void RemovePendingVm(uint32_t cid);

  // Set callback from concierge to signal install
  void SetInstallStateCallback(
      base::RepeatingCallback<void(VmInstallState state)> install_state_cb);

 private:
  // VMs that have been started but have not checked in as being ready yet. This
  // is a map of their cids to event fds registered in |AddPendingVm|.
  std::map<uint32_t, int32_t> pending_vms_ GUARDED_BY(vm_lock_);

  // Lock to protect |pending_vms_|.
  // TODO(b/294160898): Use sequences instead of acquiring a lock here.
  base::Lock vm_lock_;

  base::RepeatingCallback<void(VmInstallState state)> install_state_cb_;
  bool install_state_cb_set_ = false;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_STARTUP_LISTENER_IMPL_H_
