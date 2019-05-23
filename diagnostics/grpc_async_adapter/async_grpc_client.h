// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_GRPC_ASYNC_ADAPTER_ASYNC_GRPC_CLIENT_H_
#define DIAGNOSTICS_GRPC_ASYNC_ADAPTER_ASYNC_GRPC_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/sequenced_task_runner.h>
#include <grpcpp/grpcpp.h>

#include "diagnostics/grpc_async_adapter/grpc_completion_queue_dispatcher.h"

namespace diagnostics {
namespace internal {

// Base class for a gRPC client that supports sending RPCs to an endpoint and
// posting a task on a task runner when the response has been received. This
// base class is not specific to a Stub or Service.
class AsyncGrpcClientBase {
 public:
  // Type of the callback which will be called when an RPC response is
  // available.
  template <typename ResponseType>
  using ReplyCallback =
      base::Callback<void(std::unique_ptr<ResponseType> response)>;

  explicit AsyncGrpcClientBase(
      scoped_refptr<base::SequencedTaskRunner> task_runner);
  virtual ~AsyncGrpcClientBase();

  // Shuts down this client. This instance may only be destroyed after
  // |on_shutdown| has been called.
  void Shutdown(const base::Closure& on_shutdown);

 protected:
  GrpcCompletionQueueDispatcher* dispatcher() { return &dispatcher_; }

  static std::shared_ptr<grpc::Channel> CreateGrpcChannel(
      const std::string& target_uri);

 private:
  grpc::CompletionQueue completion_queue_;
  GrpcCompletionQueueDispatcher dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(AsyncGrpcClientBase);
};

}  // namespace internal

// A gRPC client that is specific to |ServiceType|.
// Example usage:
//   AsyncGrpcClient<Foo> client(base::ThreadTaskRunnerHandle::Get(),
//                               "unix:/path/to/socket");
//   client.CallRpc(&FooStub::AsyncDoSomething,
//                  something_request,
//                  do_something_callback);
//   client.CallRpc(&FooStub::AsyncDoOtherThing,
//                  other_thing_request,
//                  do_other_thing_callback);
//   client.Shutdown(on_shutdown_callback);
//   // Important: Make sure |client| is not destroyed before
//   // |on_shutdown_callback| is called.
// The callbacks (e.g. |do_something_callback| in the example) have the
// following form:
//   void DoSomethingCallback(std::unique_ptr<DoSomethingResponse> response);
template <typename ServiceType>
class AsyncGrpcClient final : public internal::AsyncGrpcClientBase {
 public:
  AsyncGrpcClient(scoped_refptr<base::SequencedTaskRunner> task_runner,
                  const std::string& target_uri)
      : AsyncGrpcClientBase(task_runner) {
    stub_ = ServiceType::NewStub(CreateGrpcChannel(target_uri));
  }

  ~AsyncGrpcClient() override = default;

  // A function pointer on a gRPC Stub class to send an RPC.
  template <typename AsyncServiceStub,
            typename RequestType,
            typename ResponseType>
  using AsyncRequestFnPtr =
      std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>> (
          AsyncServiceStub::*)(grpc::ClientContext* context,
                               const RequestType& request,
                               grpc::CompletionQueue* cq);

  // Call RPC represented by |async_rpc_start|. Pass |request| as the
  // request. Call |on_reply_callback| on the task runner passed to the
  // constructor when a response is available.
  template <typename AsyncServiceStub,
            typename RequestType,
            typename ResponseType>
  void CallRpc(AsyncRequestFnPtr<AsyncServiceStub, RequestType, ResponseType>
                   async_rpc_start,
               const RequestType& request,
               ReplyCallback<ResponseType> on_reply_callback) {
    std::unique_ptr<RpcState<ResponseType>> rpc_state =
        std::make_unique<RpcState<ResponseType>>();
    RpcState<ResponseType>* rpc_state_unowned = rpc_state.get();

    std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>> rpc =
        (stub_.get()->*async_rpc_start)(&rpc_state_unowned->context, request,
                                        dispatcher()->completion_queue());
    dispatcher()->RegisterTag(
        rpc_state_unowned->tag(),
        base::Bind(&AsyncGrpcClient::OnReplyReceived<ResponseType>,
                   base::Passed(&rpc_state), on_reply_callback));
    // Accessing |rpc_state_unowned| is safe, because the RpcState will remain
    // alive (owned by the |dispatcher()|) at least until the corresponding tag
    // becomes available through the gRPC CompletionQueue, which can not happen
    // before |Finish| is called.
    rpc->Finish(rpc_state_unowned->response.get(), &rpc_state_unowned->status,
                rpc_state_unowned->tag());
  }

 private:
  // Holds memory for the response and the grpc Status.
  template <typename ResponseType>
  struct RpcState {
    const void* tag() const { return this; }
    void* tag() { return this; }

    grpc::Status status;
    grpc::ClientContext context;
    std::unique_ptr<ResponseType> response = std::make_unique<ResponseType>();
  };

  template <typename ResponseType>
  static void OnReplyReceived(
      std::unique_ptr<RpcState<ResponseType>> rpc_state,
      const ReplyCallback<ResponseType>& on_reply_callback,
      bool ok) {
    // gRPC CompletionQueue::Next documentation says that |ok| should always
    // be true for client-side |Finish|.
    CHECK(ok);
    if (rpc_state->status.error_code() != grpc::StatusCode::OK) {
      VLOG(1) << "Outgoing RPC failed with error_code="
              << rpc_state->status.error_code() << ", error_message='"
              << rpc_state->status.error_message() << "', error_details='"
              << rpc_state->status.error_details() << "'";
      rpc_state->response.reset();
    }
    on_reply_callback.Run(std::move(rpc_state->response));
  }

  std::unique_ptr<typename ServiceType::Stub> stub_;

  DISALLOW_COPY_AND_ASSIGN(AsyncGrpcClient);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_GRPC_ASYNC_ADAPTER_ASYNC_GRPC_CLIENT_H_
