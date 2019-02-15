// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DPSL_INTERNAL_DPSL_RPC_SERVER_IMPL_H_
#define DIAGNOSTICS_DPSL_INTERNAL_DPSL_RPC_SERVER_IMPL_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <base/sequence_checker_impl.h>

#include "diagnostics/dpsl/public/dpsl_rpc_server.h"
#include "diagnostics/grpc_async_adapter/async_grpc_server.h"

#include "diagnostics_processor.grpc.pb.h"  // NOLINT(build/include)
#include "diagnostics_processor.pb.h"       // NOLINT(build/include)

namespace diagnostics {

class DpslRpcHandler;

// Real implementation of the DpslRpcServer interface.
class DpslRpcServerImpl final : public DpslRpcServer {
 public:
  DpslRpcServerImpl(DpslRpcHandler* rpc_handler,
                    GrpcServerUri grpc_server_uri,
                    const std::string& grpc_server_uri_string);
  ~DpslRpcServerImpl() override;

  // Starts the gRPC server. Returns whether the startup succeeded.
  bool Init();

 private:
  using HandleMessageFromUiCallback = base::Callback<void(
      std::unique_ptr<grpc_api::HandleMessageFromUiResponse>)>;
  using HandleEcNotificationCallback = base::Callback<void(
      std::unique_ptr<grpc_api::HandleEcNotificationResponse>)>;

  // Methods corresponding to the "DiagnosticsProcessor" gRPC interface (each of
  // these methods just calls the corresponding method of |rpc_handler_|):
  void HandleMessageFromUi(
      std::unique_ptr<grpc_api::HandleMessageFromUiRequest> request,
      const HandleMessageFromUiCallback& callback);
  void HandleEcNotification(
      std::unique_ptr<grpc_api::HandleEcNotificationRequest> request,
      const HandleEcNotificationCallback& callback);

  // The method corresponding to the HandleMessageFromUi method of the
  // "DiagnosticsProcessor" gRPC interface and returning back a nullptr
  // response.
  void HandleMessageFromUiStub(
      std::unique_ptr<grpc_api::HandleMessageFromUiRequest> request,
      const HandleMessageFromUiCallback& callback);

  // Unowned.
  DpslRpcHandler* const rpc_handler_;

  AsyncGrpcServer<grpc_api::DiagnosticsProcessor::AsyncService>
      async_grpc_server_;

  base::SequenceCheckerImpl sequence_checker_;

  DISALLOW_COPY_AND_ASSIGN(DpslRpcServerImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DPSL_INTERNAL_DPSL_RPC_SERVER_IMPL_H_
