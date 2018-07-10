// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_MAITRED_SERVICE_IMPL_H_
#define VM_TOOLS_MAITRED_SERVICE_IMPL_H_

#include <memory>
#include <utility>

#include <base/callback.h>
#include <base/macros.h>
#include <grpc++/grpc++.h>

#include "vm_guest.grpc.pb.h"  // NOLINT(build/include)
#include "vm_tools/maitred/init.h"

namespace vm_tools {
namespace maitred {

// Actually implements the maitred service.
class ServiceImpl final : public vm_tools::Maitred::Service {
 public:
  explicit ServiceImpl(std::unique_ptr<Init> init);
  ~ServiceImpl() override = default;

  void set_shutdown_cb(base::Callback<bool(void)> cb) {
    shutdown_cb_ = std::move(cb);
  }

  // Maitred::Service overrides.
  grpc::Status ConfigureNetwork(grpc::ServerContext* ctx,
                                const vm_tools::NetworkConfigRequest* request,
                                vm_tools::EmptyMessage* response) override;
  grpc::Status Shutdown(grpc::ServerContext* ctx,
                        const vm_tools::EmptyMessage* request,
                        vm_tools::EmptyMessage* response) override;
  grpc::Status LaunchProcess(
      grpc::ServerContext* ctx,
      const vm_tools::LaunchProcessRequest* request,
      vm_tools::LaunchProcessResponse* response) override;

  grpc::Status Mount(grpc::ServerContext* ctx,
                     const vm_tools::MountRequest* request,
                     vm_tools::MountResponse* response) override;

  grpc::Status StartTermina(grpc::ServerContext* ctx,
                            const vm_tools::StartTerminaRequest* request,
                            vm_tools::StartTerminaResponse* response) override;

 private:
  std::unique_ptr<Init> init_;

  // Callback used for shutting down the gRPC server.  Called when handling a
  // Shutdown RPC.
  base::Callback<bool(void)> shutdown_cb_;

  DISALLOW_COPY_AND_ASSIGN(ServiceImpl);
};

}  // namespace maitred
}  // namespace vm_tools

#endif  // VM_TOOLS_MAITRED_SERVICE_IMPL_H_
