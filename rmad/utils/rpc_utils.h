// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines a helper function `RunRpcWithInputs` that
// - Runs async RPC with a pre-defined input lists.
// - For each iteration, check the RPC return value using the provided checker:
//   - If checker returns true, stop the loop and use `success_handler` to run
//     the callback function.
//   - If checker returns false, continue the loop with the next input. If all
//     the inputs are tried, use `fail_handler` to run the callback function.

#ifndef RMAD_UTILS_RPC_UTILS_H_
#define RMAD_UTILS_RPC_UTILS_H_

#include <list>
#include <utility>

#include <base/functional/callback.h>

namespace rmad {

namespace internal {

// An RPC takes an input, and runs a callback function with its outputs.
template <typename RpcInputType, typename... RpcOutputTypes>
using RpcType = base::RepeatingCallback<void(
    RpcInputType, base::OnceCallback<void(RpcOutputTypes...)>)>;

// A checker takes the outputs of an RPC, and decides if we should accept the
// current outputs (returns true), or continue to try the next input (returns
// false).
template <typename... RpcOutputTypes>
using RpcOutputCheckerType = base::RepeatingCallback<bool(RpcOutputTypes...)>;

// A success handler runs the callback function with customized arguments
// depending on the outputs from the RPC.
template <typename CallbackFuncType, typename... RpcOutputTypes>
using SuccessHandlerType =
    base::OnceCallback<void(CallbackFuncType, RpcOutputTypes...)>;

// A fail handler runs the callback function with customized arguments when none
// of the outputs are accepted by the checker.
template <typename CallbackFuncType>
using FailHandlerType = base::OnceCallback<void(CallbackFuncType)>;

// Forward declaration.
template <typename CallbackFuncType,
          typename RpcInputType,
          typename... RpcOutputTypes>
void OnRpcCompleted(
    CallbackFuncType callback_func,
    RpcType<RpcInputType, RpcOutputTypes...> rpc,
    std::list<RpcInputType> rpc_inputs,
    RpcOutputCheckerType<RpcOutputTypes...> rpc_output_checker,
    SuccessHandlerType<CallbackFuncType, RpcOutputTypes...> succees_handler,
    FailHandlerType<CallbackFuncType> fail_handler,
    RpcOutputTypes... reply);

}  // namespace internal

template <typename CallbackFuncType,
          typename RpcInputType,
          typename... RpcOutputTypes>
void RunRpcWithInputs(
    CallbackFuncType callback_func,
    internal::RpcType<RpcInputType, RpcOutputTypes...> rpc,
    std::list<RpcInputType> rpc_inputs,
    internal::RpcOutputCheckerType<RpcOutputTypes...> rpc_output_checker,
    internal::SuccessHandlerType<CallbackFuncType, RpcOutputTypes...>
        success_handler,
    internal::FailHandlerType<CallbackFuncType> fail_handler) {
  if (!rpc_inputs.empty()) {
    // Run RPC with the first input.
    RpcInputType input = rpc_inputs.front();
    rpc_inputs.pop_front();
    rpc.Run(input, base::BindOnce(
                       &internal::OnRpcCompleted<CallbackFuncType, RpcInputType,
                                                 RpcOutputTypes...>,
                       std::move(callback_func), rpc, std::move(rpc_inputs),
                       rpc_output_checker, std::move(success_handler),
                       std::move(fail_handler)));
  } else {
    // No input left. Use the fail handler to run the callback function.
    std::move(fail_handler).Run(std::move(callback_func));
  }
}

namespace internal {

template <typename CallbackFuncType,
          typename RpcInputType,
          typename... RpcOutputTypes>
void OnRpcCompleted(
    CallbackFuncType callback_func,
    RpcType<RpcInputType, RpcOutputTypes...> rpc,
    std::list<RpcInputType> rpc_inputs,
    RpcOutputCheckerType<RpcOutputTypes...> rpc_output_checker,
    SuccessHandlerType<CallbackFuncType, RpcOutputTypes...> success_handler,
    FailHandlerType<CallbackFuncType> fail_handler,
    RpcOutputTypes... reply) {
  if (rpc_output_checker.Run(reply...)) {
    // Use the success handler to run the callback function.
    std::move(success_handler).Run(std::move(callback_func), reply...);
  } else {
    // Try the next input.
    RunRpcWithInputs(std::move(callback_func), rpc, std::move(rpc_inputs),
                     rpc_output_checker, std::move(success_handler),
                     std::move(fail_handler));
  }
}

}  // namespace internal

}  // namespace rmad

#endif  // RMAD_UTILS_RPC_UTILS_H_
