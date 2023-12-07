// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_VM_CONCIERGE_CLIENT_H_
#define PATCHPANEL_VM_CONCIERGE_CLIENT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <queue>
#include <string>

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <dbus/vm_concierge/dbus-constants.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace patchpanel {
// Concierge DBus interface client for controlling guest VM after it started.
//
// The guest VMs are identified by its cid (virtual socket context id). To
// control a VM using VmConciergeClient, the VM has to first be registered using
// RegisterVm BEFORE the VM starts. VmConciergeClient is then immediately ready
// to take requests, which are queued until VmConciergeClient receives
// VmStartedSignal for this VM. The VM will be removed when VmStoppingSignal is
// received for this VM.
class VmConciergeClient {
 public:
  explicit VmConciergeClient(scoped_refptr<dbus::Bus> bus);
  VmConciergeClient(const VmConciergeClient&) = delete;
  VmConciergeClient& operator=(const VmConciergeClient&) = delete;

  ~VmConciergeClient();

  // VmId is the identifier concierge dbus uses for a VM after it started.
  struct VmId {
    std::string owner_id;
    std::string vm_name;
  };

  // Registers VM by cid. Client then anticipates VmStartedSignal for this cid.
  //
  // Returns true if the cid is not known to VmConciergeClient yet, false if it
  // has already been registered.
  bool RegisterVm(int64_t vm_cid);

  // Callback after response for TapAttachRequest is received.
  //
  // |bus_num|: PCI bus number of on guest VM on success, nullopt on failure.
  using AttachTapCallback =
      base::OnceCallback<void(std::optional<uint32_t> bus_num)>;
  // Attaches a tap device, handles response by |callback|.
  //
  // Returns true if the Request cannot be made, for example if the VM is not
  // registered, or already shutdown.
  bool AttachTapDevice(int64_t vm_cid,
                       const std::string& tap_name,
                       AttachTapCallback callback);

  // Callback after response for TapDetachRequest is received.
  //
  // |success|: true on detach success, false on failure.
  using DetachTapCallback = base::OnceCallback<void(bool success)>;

  // Detaches a tap device, handles response by |callback|.
  //
  // Returns true if the Request cannot be made, for example if the VM is not
  // registered, or already shutdown.
  bool DetachTapDevice(int64_t vm_cid,
                       uint32_t bus_num,
                       DetachTapCallback callback);

 private:
  // DeferredRequest is a request deferred until VmId is available.
  using DeferredRequest = base::OnceCallback<void(const VmId& vm_id)>;
  // callback for VM start and ready for requests.
  void OnVmStarted(dbus::Signal*);
  // callback for VM stopping, and no longer accepting requests.
  void OnVmStopping(dbus::Signal*);

  void DoAttachTapDevice(const std::string& tap_name,
                         AttachTapCallback callback,
                         const VmId& vm_id);
  void DoDetachTapDevice(uint32_t bus_num,
                         DetachTapCallback callback,
                         const VmId& vm_id);
  FRIEND_TEST(VmConciergeClientTest, ReadyAfterVmStartedSignal);

  scoped_refptr<dbus::Bus> bus_;
  scoped_refptr<dbus::ObjectProxy> concierge_proxy_;
  std::map<int64_t, std::optional<VmId>> cid_vmid_map_;
  std::map<int64_t, std::queue<DeferredRequest>> cid_requestq_map_;
  base::WeakPtrFactory<VmConciergeClient> weak_ptr_factory_{this};
};

std::ostream& operator<<(std::ostream& os,
                         const VmConciergeClient::VmId& vm_id);

}  // namespace patchpanel

#endif  // PATCHPANEL_VM_CONCIERGE_CLIENT_H_
