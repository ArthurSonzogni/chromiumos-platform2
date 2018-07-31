// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_CONTAINER_H_
#define VM_TOOLS_CICERONE_CONTAINER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "container_guest.grpc.pb.h"  // NOLINT(build/include)

namespace vm_tools {
namespace cicerone {

class VirtualMachine;

// Represents a single container running in a VM.
class Container {
 public:
  // Linux application ID and its icon content.
  struct Icon {
    std::string desktop_file_id;
    std::string content;
  };
  // Information about a Linux package file.
  struct LinuxPackageInfo {
    std::string package_id;
    std::string license;
    std::string description;
    std::string project_url;
    uint64_t size;
    std::string summary;
  };

  // The container's name.
  std::string name() const { return name_; }

  // The container's security token.
  std::string token() const { return token_; }

  // The container's IPv4 address.
  uint32_t ipv4_address() const { return ipv4_address_; }

  // Sets the container's IPv4 address.
  void set_ipv4_address(uint32_t ipv4_address);

  Container(const std::string& name,
            const std::string& token,
            base::WeakPtr<VirtualMachine> vm);

  ~Container() = default;

  void ConnectToGarcon(const std::string& addr);

  bool LaunchContainerApplication(const std::string& desktop_file_id,
                                  std::vector<std::string> files,
                                  std::string* out_error);

  bool LaunchVshd(uint32_t port, std::string* out_error);

  bool GetDebugInformation(std::string* out);

  bool GetContainerAppIcon(std::vector<std::string> desktop_file_ids,
                           uint32_t icon_size,
                           uint32_t scale,
                           std::vector<Icon>* icons);

  bool GetLinuxPackageInfo(const std::string& file_path,
                           LinuxPackageInfo* out_pkg_info,
                           std::string* out_error);
  int InstallLinuxPackage(const std::string& file_path, std::string* out_error);

  bool IsRunning();

 private:
  std::string name_;
  std::string token_;
  uint32_t ipv4_address_;

  // The VM that owns this container.
  base::WeakPtr<VirtualMachine> vm_;

  // Stub for making RPC requests to the garcon process inside the container.
  std::unique_ptr<vm_tools::container::Garcon::Stub> garcon_stub_;

  // gRPC Channel to the garcon process inside the container, which we can
  // test for connectedness.
  std::shared_ptr<grpc::Channel> garcon_channel_;

  DISALLOW_COPY_AND_ASSIGN(Container);
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_CONTAINER_H_
