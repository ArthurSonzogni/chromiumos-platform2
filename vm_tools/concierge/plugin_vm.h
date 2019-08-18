// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_PLUGIN_VM_H_
#define VM_TOOLS_CONCIERGE_PLUGIN_VM_H_

#include <stdint.h>

#include <deque>
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <arc/network/mac_address_generator.h>
#include <arc/network/subnet.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <brillo/process.h>
#include <dbus/exported_object.h>
#include <vm_concierge/proto_bindings/service.pb.h>

#include "vm_tools/common/vm_id.h"
#include "vm_tools/concierge/plugin_vm_usb.h"
#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/vm_interface.h"

namespace vm_tools {
namespace concierge {

class PluginVm final : public VmInterface, base::MessageLoopForIO::Watcher {
 public:
  static std::unique_ptr<PluginVm> Create(
      const VmId id,
      uint32_t cpus,
      std::vector<std::string> params,
      arc_networkd::MacAddress mac_addr,
      std::unique_ptr<arc_networkd::SubnetAddress> ipv4_addr,
      uint32_t ipv4_netmask,
      uint32_t ipv4_gateway,
      base::FilePath stateful_dir,
      base::FilePath iso_dir,
      base::FilePath root_dir,
      base::FilePath runtime_dir,
      std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
      dbus::ObjectProxy* vmplugin_service_proxy);
  ~PluginVm() override;

  // VmInterface overrides.
  bool Shutdown() override;
  VmInterface::Info GetInfo() override;
  // Currently only implemented for termina, returns "Not implemented".
  bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) override;
  bool AttachUsbDevice(uint8_t bus,
                       uint8_t addr,
                       uint16_t vid,
                       uint16_t pid,
                       int fd,
                       UsbControlResponse* response) override;
  bool DetachUsbDevice(uint8_t port, UsbControlResponse* response) override;
  bool ListUsbDevice(std::vector<UsbDevice>* devices) override;
  void HandleSuspendImminent() override {}
  void HandleSuspendDone() override {}
  bool SetResolvConfig(const std::vector<std::string>& nameservers,
                       const std::vector<std::string>& search_domains) override;
  bool SetTime(std::string* failure_reason) override { return true; }
  void SetTremplinStarted() override { NOTREACHED(); }
  void VmToolsStateChanged(bool running) override;

  // base::MessageLoopForIO::Watcher overrides
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  static bool WriteResolvConf(const base::FilePath& parent_dir,
                              const std::vector<std::string>& nameservers,
                              const std::vector<std::string>& search_domains);

  static base::ScopedFD CreateUnixSocket(const base::FilePath& path, int type);

  // Adjusts the amount of CPU the Plugin VM processes are allowed to use.
  static bool SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state);

  // The 9p server managed by seneschal that provides access to shared files for
  // this VM.  Returns 0 if there is no seneschal server associated with this
  // VM.
  uint32_t seneschal_server_handle() const {
    if (seneschal_server_proxy_) {
      return seneschal_server_proxy_->handle();
    }

    return 0;
  }

 private:
  PluginVm(const VmId id,
           arc_networkd::MacAddress mac_addr,
           std::unique_ptr<arc_networkd::SubnetAddress> ipv4_addr,
           uint32_t ipv4_netmask,
           uint32_t ipv4_gateway,
           std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
           dbus::ObjectProxy* vmplugin_service_proxy,
           base::FilePath iso_dir,
           base::FilePath root_dir,
           base::FilePath runtime_dir);
  bool Start(uint32_t cpus,
             std::vector<std::string> params,
             base::FilePath stateful_dir);
  bool CreateUsbListeningSocket();
  void HandleUsbControlResponse();

  // Attempt to stop VM.
  bool StopVm();

  // This VM ID. It is used to communicate with the dispatcher to request
  // VM state changes.
  const VmId id_;

  // Specifies directory holding ISO images that can be attached to the VM.
  base::FilePath iso_dir_;

  // Allows to build skeleton of root file system for the plugin.
  // Individual directories, such as /etc, are mounted plugin jail.
  base::ScopedTempDir root_dir_;

  // Runtime directory for the crosvm instance. It is shared with dispatcher
  // and mounted as /run/pvm in plugin jail.
  base::ScopedTempDir runtime_dir_;

  // Handle to the VM process.
  brillo::ProcessImpl process_;

  // Network configuration.
  arc_networkd::MacAddress mac_addr_;
  std::unique_ptr<arc_networkd::SubnetAddress> ipv4_addr_;
  uint32_t netmask_;
  uint32_t gateway_;

  // Proxy to the server providing shared directory access for this VM.
  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy_;

  // Proxy to the dispatcher service.  Not owned.
  dbus::ObjectProxy* vmplugin_service_proxy_;

  // List of USB devices attached to VM (vid, pid, handle)
  using UsbDeviceInfo = std::tuple<uint16_t, uint16_t, uint32_t>;
  std::list<UsbDeviceInfo> usb_devices_;

  // Monotonically increasing handle (port) number for USB devices passed
  // to the Plugin VM.
  uint32_t usb_last_handle_;

  // Outstanding control requests waiting to be transmitted to plugin.
  std::deque<std::pair<UsbCtrlRequest, base::ScopedFD>> usb_req_waiting_xmit_;

  // Outstanding control requests waiting response from plugin.
  std::list<UsbCtrlRequest> usb_req_waiting_response_;

  // File descriptors to pass USB devices over to plugin.
  base::ScopedFD usb_listen_fd_;
  base::ScopedFD usb_vm_fd_;
  base::MessageLoopForIO::FileDescriptorWatcher usb_fd_watcher_;

  DISALLOW_COPY_AND_ASSIGN(PluginVm);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_PLUGIN_VM_H_
