// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_
#define LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_

#include <concepts>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <absl/base/attributes.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/stringprintf.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/single_thread_task_runner.h>
#include <base/task/task_runner.h>
#include <base/threading/thread.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec/middleware/function_name.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/middleware/middleware_owner.h"
#include "libhwsec/middleware/subclass_helper.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

// Middleware can be shared by multiple frontends.
// Converts asynchronous and synchronous calls to the backend.
// And doing some generic error handling, for example: communication error and
// auto reload key & session.
//
// Note: The middleware can maintain a standalone thread, or use the same task
// runner as the caller side.
//
// Note2: The move-only function parameters would not be copied, the other kinds
// of function parameters would be copied due to base::BindOnce.

namespace hwsec {

class Middleware {
 public:
  explicit Middleware(MiddlewareDerivative middleware_derivative)
      : middleware_derivative_(middleware_derivative) {}

  MiddlewareDerivative Derive() const { return middleware_derivative_; }

  // Call the synchronous backend function synchronously.
  template <auto Func, typename... Args>
    requires(SyncBackendMethod<decltype(Func)>)
  auto CallSync(Args&&... args) const {
    // Calling sync backend function.
    auto task = base::BindOnce(
        &Middleware::DoSyncBackendCall<Func, decltype(ForwardParameter(
                                                 std::declval<Args>()))...>,
        middleware_derivative_.middleware,
        ForwardParameter(std::forward<Args>(args))...);
    return RunBlockingTask(std::move(task));
  }

  // Call the asynchronous backend function synchronously.
  template <auto Func, typename... Args>
    requires(AsyncBackendMethod<decltype(Func)>)
  auto CallSync(Args&&... args) const {
    // Calling async backend function.
    using hwsec_foundation::status::MakeStatus;
    using Result = SubClassResult<decltype(Func)>;
    using Callback = SubClassCallback<decltype(Func)>;

    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);

    Result result =
        MakeStatus<TPMError>("Unknown error", TPMRetryAction::kNoRetry);
    Callback callback =
        base::BindOnce([](Result* result_ptr,
                          Result value) { *result_ptr = std::move(value); },
                       &result)
            .Then(base::BindOnce(&base::WaitableEvent::Signal,
                                 base::Unretained(&event)));

    base::OnceClosure task = base::BindOnce(
        &Middleware::DoAsyncBackendCall<Func, decltype(ForwardParameter(
                                                  std::declval<Args>()))...>,
        middleware_derivative_.middleware, std::move(callback),
        ForwardParameter(std::forward<Args>(args))...);

    middleware_derivative_.task_runner->PostTask(FROM_HERE, std::move(task));
    event.Wait();
    return result;
  }

  // Call the backend function asynchronously.
  template <auto Func, typename Callback, typename... Args>
    requires(BackendMethod<decltype(Func)>)
  void CallAsync(Callback callback, Args&&... args) const {
    CHECK(middleware_derivative_.task_runner);

    SubClassCallback<decltype(Func)> reply = std::move(callback);
    reply = base::BindPostTask(GetReplyRunner(), std::move(reply));
    base::OnceClosure task = base::BindOnce(
        &Middleware::CallAsyncInternal<Func, decltype(ForwardParameter(
                                                 std::declval<Args>()))...>,
        middleware_derivative_.middleware, std::move(reply),
        ForwardParameter(std::forward<Args>(args))...);
    middleware_derivative_.task_runner->PostTask(FROM_HERE, std::move(task));
  }

  // Run a blocking task without return value in the middleware.
  void RunBlockingTask(base::OnceCallback<void()> task) const {
    if (middleware_derivative_.thread_id == base::PlatformThread::CurrentId()) {
      return std::move(task).Run();
    }

    CHECK(middleware_derivative_.task_runner);

    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);

    base::OnceClosure closure = std::move(task).Then(
        base::BindOnce(&base::WaitableEvent::Signal, base::Unretained(&event)));
    middleware_derivative_.task_runner->PostTask(FROM_HERE, std::move(closure));
    event.Wait();
    return;
  }

  // Run a blocking task with return value in the middleware.
  template <typename Result>
    requires(std::constructible_from<Result, Status>)
  Result RunBlockingTask(base::OnceCallback<Result()> task) const {
    if (middleware_derivative_.thread_id == base::PlatformThread::CurrentId()) {
      return std::move(task).Run();
    }

    CHECK(middleware_derivative_.task_runner);

    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);

    using hwsec_foundation::status::MakeStatus;
    Result result =
        MakeStatus<TPMError>("Unknown error", TPMRetryAction::kNoRetry);
    base::OnceClosure closure =
        std::move(task)
            .Then(base::BindOnce(
                [](Result* result_ptr, Result value) {
                  *result_ptr = std::move(value);
                },
                &result))
            .Then(base::BindOnce(&base::WaitableEvent::Signal,
                                 base::Unretained(&event)));
    middleware_derivative_.task_runner->PostTask(FROM_HERE, std::move(closure));
    event.Wait();
    return result;
  }

 private:
  template <typename>
  inline static constexpr bool always_false_v = false;

  // The custom parameter forwarding rules.
  template <typename T>
  static T ForwardParameter(T&& t) {
    // The rvalue should still be rvalue, because we have the ownership.
    return t;
  }

  template <typename T>
  static const T& ForwardParameter(T& t) {
    // Add const for normal reference, because we don't have the ownership.
    // base::BindOnce will copy const reference parameter when binding.
    return t;
  }

  template <typename T>
  static const T& ForwardParameter(const T& t) {
    // The const reference would still be const reference.
    // base::BindOnce will copy const reference parameter when binding.
    return t;
  }

  template <typename T>
  static const T* ForwardParameter(const T* t) {
    static_assert(always_false_v<T>, "Pointer cannot be safely forward!");
    return nullptr;
  }

  template <typename T>
  static T* ForwardParameter(T* t) {
    static_assert(always_false_v<T>, "Pointer cannot be safely forward!");
    return nullptr;
  }

  // Get the quick result that is not related to the function itself.
  template <auto Func>
    requires(BackendMethod<decltype(Func)>)
  static std::variant<SubClassResult<decltype(Func)>,
                      SubClassType<decltype(Func)>*>
  GetQuickResult(base::WeakPtr<MiddlewareOwner> middleware) {
    using hwsec_foundation::status::MakeStatus;

    if (!middleware) {
      return MakeStatus<TPMError>("No middleware", TPMRetryAction::kNoRetry);
    }

#if USE_FUZZER
    if (middleware->data_provider_) {
      return FuzzedObject<SubClassResult<decltype(Func)>>()(
          *middleware->data_provider_);
    }
#endif

    if (!middleware->GetBackend()) {
      return MakeStatus<TPMError>("No backend", TPMRetryAction::kNoRetry);
    }

    auto* sub = middleware->GetBackend()->Get<SubClassType<decltype(Func)>>();
    if (!sub) {
      return MakeStatus<TPMError>("No sub class in backend",
                                  TPMRetryAction::kNoRetry);
    }

    return sub;
  }

  // Call the synchronous backend call.
  template <auto Func, typename... Args>
    requires(SyncBackendMethod<decltype(Func)>)
  static SubClassResult<decltype(Func)> DoSyncBackendCall(
      base::WeakPtr<MiddlewareOwner> middleware, Args... args) {
    using Result = SubClassResult<decltype(Func)>;
    using Type = SubClassType<decltype(Func)>;

    std::variant<Result, Type*> quick_result = GetQuickResult<Func>(middleware);
    if (Result* result = std::get_if<Result>(&quick_result)) {
      return std::move(*result);
    }

    Type* sub = *std::get_if<Type*>(&quick_result);

    for (TPMRetryHandler retry_handler;;) {
      SubClassResult<decltype(Func)> result = (sub->*Func)(args...);

      TrackFuncResult(GetFuncName<Func>(), middleware->GetMetrics(), result);

      if (retry_handler.HandleResult(result, *middleware->GetBackend(),
                                     args...)) {
        return result;
      }
    }
  }

  // Call the asynchronous backend call.
  template <auto Func, typename... Args>
    requires(AsyncBackendMethod<decltype(Func)>)
  static void DoAsyncBackendCall(base::WeakPtr<MiddlewareOwner> middleware,
                                 SubClassCallback<decltype(Func)> callback,
                                 Args... args) {
    auto retry_handler = std::make_unique<TPMRetryHandler>();

    // Using the decay type to make sure we are not putting dangling reference
    // in the tuple.
    auto args_tuple =
        std::make_unique<std::tuple<std::decay_t<Args>...>>(std::move(args)...);

    DoAsyncBackendCallInternal<Func>(
        std::move(middleware), std::move(retry_handler), std::move(callback),
        std::move(args_tuple), std::make_index_sequence<sizeof...(Args)>());
  }

  template <auto Func, typename ArgsTuple, std::size_t... I>
    requires(AsyncBackendMethod<decltype(Func)>)
  static void DoAsyncBackendCallInternal(
      base::WeakPtr<MiddlewareOwner> middleware,
      std::unique_ptr<TPMRetryHandler> retry_handler,
      SubClassCallback<decltype(Func)> callback,
      std::unique_ptr<ArgsTuple> args,
      std::index_sequence<I...> idx_seq) {
    using Result = SubClassResult<decltype(Func)>;
    using Type = SubClassType<decltype(Func)>;
    using Callback = SubClassCallback<decltype(Func)>;

    std::variant<Result, Type*> quick_result = GetQuickResult<Func>(middleware);
    if (Result* result = std::get_if<Result>(&quick_result)) {
      std::move(callback).Run(std::move(*result));
      return;
    }

    Type* sub = *std::get_if<Type*>(&quick_result);

    // Note: The args tuple will be owned by the retry callback.
    // We will transfer the ownership of the retry callback into the backend
    // function, so the backend functions should be careful about not using the
    // args after call or drop the callback.
    ArgsTuple& args_ref = *args;

    Callback retry_callback =
        base::BindOnce(&HandleAsyncBackendCallRetry<Func, ArgsTuple, I...>,
                       std::move(middleware), std::move(retry_handler),
                       std::move(callback), std::move(args), idx_seq);

    (sub->*Func)(std::move(retry_callback), std::get<I>(args_ref)...);
  }

  template <auto Func, typename ArgsTuple, std::size_t... I>
    requires(AsyncBackendMethod<decltype(Func)>)
  static void HandleAsyncBackendCallRetry(
      base::WeakPtr<MiddlewareOwner> middleware,
      std::unique_ptr<TPMRetryHandler> retry_handler,
      SubClassCallback<decltype(Func)> callback,
      std::unique_ptr<ArgsTuple> args,
      std::index_sequence<I...> idx_seq,
      SubClassResult<decltype(Func)> result) {
    using hwsec_foundation::status::MakeStatus;

    if (!middleware) {
      std::move(callback).Run(
          MakeStatus<TPMError>("No middleware", TPMRetryAction::kNoRetry));
      return;
    }

    TrackFuncResult(GetFuncName<Func>(), middleware->GetMetrics(), result);

    if (retry_handler->HandleResult(result, *middleware->GetBackend(),
                                    std::get<I>(*args)...)) {
      std::move(callback).Run(std::move(result));
      return;
    }

    DoAsyncBackendCallInternal<Func>(
        std::move(middleware), std::move(retry_handler), std::move(callback),
        std::move(args), idx_seq);
  }

  // Calling synchronous backend function asynchronously.
  template <auto Func, typename... Args>
    requires(SyncBackendMethod<decltype(Func)>)
  static void CallAsyncInternal(base::WeakPtr<MiddlewareOwner> middleware,
                                SubClassCallback<decltype(Func)> callback,
                                Args... args) {
    std::move(callback).Run(DoSyncBackendCall<Func, Args...>(
        std::move(middleware), ForwardParameter(std::move(args))...));
  }

  // Calling asynchronous backend function asynchronously.
  template <auto Func, typename... Args>
    requires(AsyncBackendMethod<decltype(Func)>)
  static void CallAsyncInternal(base::WeakPtr<MiddlewareOwner> middleware,
                                SubClassCallback<decltype(Func)> callback,
                                Args... args) {
    Middleware::DoAsyncBackendCall<Func, Args...>(
        std::move(middleware), std::move(callback),
        ForwardParameter(std::move(args))...);
  }

  template <typename Result>
    requires(std::constructible_from<Result, Status>)
  static void TrackFuncResult(const std::string& function_name,
                              Metrics* metrics,
                              Result& result) {
    using hwsec_foundation::status::MakeStatus;

    std::string sim_name = SimplifyFuncName(function_name);

    if (metrics) {
      metrics->SendFuncResultToUMA(sim_name, result.status());
    }

    if (!result.ok()) {
      Status status = std::move(result).err_status();
      TPMRetryAction action = status->ToTPMRetryAction();
      status = MakeStatus<TPMError>(
                   base::StringPrintf("%s(%s)", sim_name.c_str(),
                                      GetTPMRetryActionName(action)),
                   action)
                   .Wrap(std::move(status));
      result = std::move(status);
    }
  }

  static scoped_refptr<base::TaskRunner> GetReplyRunner() {
    CHECK(base::SequencedTaskRunner::HasCurrentDefault());
    return base::SequencedTaskRunner::GetCurrentDefault();
  }

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_
