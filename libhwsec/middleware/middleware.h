// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_
#define LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_

#include <atomic>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <absl/base/attributes.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/notreached.h>
#include <base/task/bind_post_task.h>
#include <base/task/task_runner.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/threading/thread.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"

// Middleware can be shared by multiple frontends.
// Converts asynchronous and synchronous calls to the backend.
// And doing some generic error handling, for example: communication error and
// auto reload key & session.
//
// Note: The middleware can maintain a standalone thread, or use the same task
// runner as the caller side.

namespace hwsec {

class Middleware;

class HWSEC_EXPORT MiddlewareOwner {
 public:
  friend class Middleware;

  // Constructor for an isolated thread.
  MiddlewareOwner();

  // Constructor for custom task runner and thread id.
  MiddlewareOwner(scoped_refptr<base::TaskRunner> task_runner,
                  base::PlatformThreadId thread_id = base::kInvalidThreadId);

  // Constructor for custom backend.
  MiddlewareOwner(std::unique_ptr<Backend> custom_backend,
                  scoped_refptr<base::TaskRunner> task_runner,
                  base::PlatformThreadId thread_id);

  virtual ~MiddlewareOwner();

  MiddlewareDerivative Derive();

 private:
  void InitBackend(std::unique_ptr<Backend> custom_backend);
  void FiniBackend();

  std::unique_ptr<base::Thread> background_thread_;

  scoped_refptr<base::TaskRunner> task_runner_;
  std::atomic<base::PlatformThreadId> thread_id_;

  // Use thread_local to ensure the proxy and backend could only be accessed on
  // a thread.
  ABSL_CONST_INIT static inline thread_local std::unique_ptr<Proxy> proxy_;
  ABSL_CONST_INIT static inline thread_local std::unique_ptr<Backend> backend_;

  // Member variables should appear before the WeakPtrFactory, to ensure
  // that any WeakPtrs to Controller are invalidated before its members
  // variable's destructors are executed, rendering them invalid.
  base::WeakPtrFactory<MiddlewareOwner> weak_factory_{this};
};

class Middleware {
 public:
  explicit Middleware(MiddlewareDerivative middleware_derivative)
      : middleware_derivative_(middleware_derivative) {}

  template <auto Func, typename... Args>
  auto CallSync(const Args&... args) {
    auto task = base::BindOnce(&Middleware::CallSyncInternal<Func, Args...>,
                               middleware_derivative_.middleware, args...);
    return RunBlockingTask(std::move(task));
  }

  template <auto Func, typename Callback, typename... Args>
  void CallAsync(Callback callback, const Args&... args) {
    CHECK(middleware_derivative_.task_runner);

    SubClassCallback<decltype(Func)> reply = std::move(callback);
    reply = base::BindPostTask(GetReplyRunner(), std::move(reply));
    base::OnceClosure task = base::BindOnce(
        &Middleware::CallAsyncInternal<Func, Args...>,
        middleware_derivative_.middleware, std::move(reply), args...);
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

  template <typename Arg>
  static void ReloadKeyHandler(hwsec::Backend* backend, const Arg& key) {
    if constexpr (std::is_same_v<Arg, Key>) {
      auto* key_mgr = backend->Get<Backend::KeyManagerment>();
      if (Status status = key_mgr->ReloadIfPossible(key); !status.ok()) {
        LOG(WARNING) << "Failed to reload key parameter: " << status.status();
      }
    }
  }

  template <auto Func, typename... Args>
  static SubClassResult<decltype(Func)> CallSyncInternal(
      base::WeakPtr<MiddlewareOwner> middleware, const Args&... args) {
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
      switch (result.status()->ToTPMRetryAction()) {
        case TPMRetryAction::kCommunication:
          RetryDelayHandler(&retry_data);
          break;
        case TPMRetryAction::kLater:
          // fold expression with comma operator.
          (ReloadKeyHandler(middleware->backend_.get(), args), ...);
          RetryDelayHandler(&retry_data);
          break;
        default:
          return result;
      }
      if (retry_data.try_count <= 0) {
        return MakeStatus<TPMError>("Retry Failed", TPMRetryAction::kReboot)
            .Wrap(std::move(result).status());
      }
    }
  }

  template <auto Func, typename... Args>
  static void CallAsyncInternal(base::WeakPtr<MiddlewareOwner> middleware,
                                SubClassCallback<decltype(Func)> callback,
                                const Args&... args) {
    std::move(callback).Run(std::move(middleware),
                            CallSyncInternal<Func, Args...>(args...));
  }

  static scoped_refptr<base::TaskRunner> GetReplyRunner() {
    CHECK(base::SequencedTaskRunnerHandle::IsSet());
    return base::SequencedTaskRunnerHandle::Get();
  }

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_MIDDLEWARE_H_
