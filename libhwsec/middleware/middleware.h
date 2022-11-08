// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_
#define LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <absl/base/attributes.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/bind_post_task.h>
#include <base/task/task_runner.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/threading/thread.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/middleware/middleware_owner.h"
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
// Note2: The moveable function parameters would not be copied, the other kinds
// of function parameters would be copied due to base::BindOnce.

namespace hwsec {

class Middleware {
 public:
  explicit Middleware(MiddlewareDerivative middleware_derivative)
      : middleware_derivative_(middleware_derivative) {}

  MiddlewareDerivative Derive() { return middleware_derivative_; }

  template <auto Func, typename... Args>
  auto CallSync(Args&&... args) {
    auto task = base::BindOnce(
        &Middleware::CallSyncInternal<Func, decltype(ForwareParameter(
                                                std::declval<Args>()))...>,
        middleware_derivative_.middleware,
        ForwareParameter(std::forward<Args>(args))...);
    return RunBlockingTask(std::move(task));
  }

  template <auto Func, typename Callback, typename... Args>
  void CallAsync(Callback callback, Args&&... args) {
    CHECK(middleware_derivative_.task_runner);

    SubClassCallback<decltype(Func)> reply = std::move(callback);
    reply = base::BindPostTask(GetReplyRunner(), std::move(reply));
    base::OnceClosure task = base::BindOnce(
        &Middleware::CallAsyncInternal<Func, decltype(ForwareParameter(
                                                 std::declval<Args>()))...>,
        middleware_derivative_.middleware, std::move(reply),
        ForwareParameter(std::forward<Args>(args))...);
    middleware_derivative_.task_runner->PostTask(FROM_HERE, std::move(task));
  }

  template <typename Result>
  Result RunBlockingTask(base::OnceCallback<Result()> task) {
    if (middleware_derivative_.thread_id == base::PlatformThread::CurrentId()) {
      return std::move(task).Run();
    }

    CHECK(middleware_derivative_.task_runner);

    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);

    if constexpr (std::is_same_v<void, Result>) {
      base::OnceClosure closure = std::move(task).Then(base::BindOnce(
          &base::WaitableEvent::Signal, base::Unretained(&event)));
      middleware_derivative_.task_runner->PostTask(FROM_HERE,
                                                   std::move(closure));
      event.Wait();
      return;

      // NOLINTNEXTLINE(readability/braces) - b/194872701
    } else if constexpr (std::is_convertible_v<Status, Result>) {
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
      middleware_derivative_.task_runner->PostTask(FROM_HERE,
                                                   std::move(closure));
      event.Wait();
      return result;
    }
  }

 private:
  template <typename Func, typename = void>
  struct SubClassHelper {
    static_assert(sizeof(Func) == -1, "Unknown member function");
  };

  template <typename R, typename S, typename... Args>
  struct SubClassHelper<R (S::*)(Args...),
                        std::enable_if_t<std::is_convertible_v<Status, R>>> {
    using Result = R;
    using SubClass = S;
    using ArgsTuple = std::tuple<Args...>;
    using Callback = base::OnceCallback<void(R)>;
  };

  template <typename Func>
  using SubClassResult = typename SubClassHelper<Func>::Result;
  template <typename Func>
  using SubClassType = typename SubClassHelper<Func>::SubClass;
  template <typename Func>
  using SubClassCallback = typename SubClassHelper<Func>::Callback;

  // The custom parameter forwarding rules.
  template <typename T>
  static T ForwareParameter(T&& t) {
    // The rvalue should still be rvalue, because we have the ownership.
    return t;
  }

  template <typename T>
  static const T& ForwareParameter(T& t) {
    // Add const for normal reference, because we don't have the ownership.
    // base::BindOnce will copy const reference parameter when binding.
    return t;
  }

  template <typename T>
  static const T& ForwareParameter(const T& t) {
    // The const reference would still be const reference.
    // base::BindOnce will copy const reference parameter when binding.
    return t;
  }

  template <typename Arg>
  static bool ReloadKeyHandler(hwsec::Backend* backend, const Arg& key) {
    if constexpr (std::is_same_v<Arg, Key>) {
      auto* key_mgr = backend->Get<Backend::KeyManagement>();
      if (key_mgr == nullptr) {
        return false;
      }
      if (Status status = key_mgr->ReloadIfPossible(key); !status.ok()) {
        LOG(WARNING) << "Failed to reload key parameter: "
                     << status.err_status();
        return false;
      }
      return true;
    }
    return false;
  }

  static bool FlushInvalidSessions(hwsec::Backend* backend) {
    auto* session_mgr = backend->Get<Backend::SessionManagement>();
    if (session_mgr == nullptr) {
      return false;
    }
    if (Status status = session_mgr->FlushInvalidSessions(); !status.ok()) {
      LOG(WARNING) << "Failed to flush invalid sessions: " << status.status();
      return false;
    }
    return true;
  }

  template <auto Func, typename... Args>
  static SubClassResult<decltype(Func)> CallSyncInternal(
      base::WeakPtr<MiddlewareOwner> middleware, Args... args) {
    using hwsec_foundation::status::MakeStatus;

    if (!middleware) {
      return MakeStatus<TPMError>("No middleware", TPMRetryAction::kNoRetry);
    }

    if (!middleware->backend_) {
      return MakeStatus<TPMError>("No backend", TPMRetryAction::kNoRetry);
    }

    auto* sub = middleware->backend_->Get<SubClassType<decltype(Func)>>();
    if (!sub) {
      return MakeStatus<TPMError>("No sub class in backend",
                                  TPMRetryAction::kNoRetry);
    }

    for (RetryInternalData retry_data;;) {
      SubClassResult<decltype(Func)> result = (sub->*Func)(args...);
      if (result.ok()) {
        return result;
      }
      switch (result.err_status()->ToTPMRetryAction()) {
        case TPMRetryAction::kCommunication:
          RetryDelayHandler(&retry_data);
          break;
        case TPMRetryAction::kLater: {
          bool shall_retry = false;
          // Flush the invalid sessions.
          shall_retry |= FlushInvalidSessions(middleware->backend_.get());
          // fold expression with || operator.
          shall_retry |=
              (ReloadKeyHandler(middleware->backend_.get(), args) || ...);
          // Don't continue retry if all reload/flush operations failed.
          if (!shall_retry) {
            return result;
          }
          RetryDelayHandler(&retry_data);
          break;
        }
        default:
          return result;
      }
      if (retry_data.try_count <= 0) {
        return MakeStatus<TPMError>("Retry Failed", TPMRetryAction::kReboot)
            .Wrap(std::move(result).err_status());
      }

      LOG(WARNING) << "Retry libhwsec error: "
                   << std::move(result).err_status();
    }
  }

  template <auto Func, typename... Args>
  static void CallAsyncInternal(base::WeakPtr<MiddlewareOwner> middleware,
                                SubClassCallback<decltype(Func)> callback,
                                Args... args) {
    std::move(callback).Run(CallSyncInternal<Func, Args...>(
        std::move(middleware), ForwareParameter(std::move(args))...));
  }

  static scoped_refptr<base::TaskRunner> GetReplyRunner() {
    CHECK(base::SequencedTaskRunnerHandle::IsSet());
    return base::SequencedTaskRunnerHandle::Get();
  }

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_
