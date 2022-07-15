// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_ASYNC_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_ASYNC_FETCHER_H_

#include <list>
#include <memory>
#include <type_traits>
#include <utility>

#include <base/check.h>
#include <base/threading/thread_task_runner_handle.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/struct_ptr.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

template <typename AsyncFetcherImpl>
class AsyncFetcher;

// Interface for an async fetcher. Implement this as `AsyncFetcherImpl` and use
// `AsyncFetcher<AsyncFetcherImpl>` in fetch aggregator to fetch the result. See
// `AsyncFetcher` for detials.
template <typename T>
class AsyncFetcherInterface : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // A mojo union type contains |error| field which is a |mojom::ProbeError|.
  using ResultType = T;
  // The ptr type of ResultType.
  // Note that this is only valid for ResultType (a mojo union type) that does
  // not have a reference kind field (otherwise it should be
  // mojo::InlinedStructPtr<ResultType>), which is true for the current
  // result types.
  using ResultPtrType = mojo::StructPtr<ResultType>;
  // The callback type to get the fetch result.
  using ResultCallback = base::OnceCallback<void(ResultPtrType)>;

  virtual ~AsyncFetcherInterface() = default;

  // The derived classes should implement this for the actual fetching logic.
  // This function is guaranteed that once it is called, it won't be called
  // again until the callback is fulfilled.
  virtual void FetchImpl(ResultCallback callback) = 0;
};

// Provides a wrapper for async fetchers. This provides two useful behaviors:
// 1. Each fetch creates a new `AsyncFetcherImpl` instance. The `FetchImpl`
//    method will be invoked once. After the callback of `FetchImpl` is called,
//    the instance will be deleted asynchronously on the same thread.
// 2. Returns error when the callback is not called.
//    In some cases (e.g. mojo disconnects) the callback could be dropped
//    without being called. It will cause memory leak because we keep the upper
//    callbacks in a queue. This class handle this so even the derived classes
//    don't call the callback, the upper callbacks will still be called.
template <typename AsyncFetcherImpl>
class AsyncFetcher : public BaseFetcher {
  static_assert(
      std::is_base_of_v<
          AsyncFetcherInterface<typename AsyncFetcherImpl::ResultType>,
          AsyncFetcherImpl>,
      "AsyncFetcherImpl must be a derived class of AsyncFetcherInterface.");

 public:
  using BaseFetcher::BaseFetcher;

  // Fetches the telemetry data.
  void Fetch(typename AsyncFetcherImpl::ResultCallback callback) {
    impl_list_.push_front(std::make_unique<AsyncFetcherImpl>(context_));
    AsyncFetcherInterface<typename AsyncFetcherImpl::ResultType>* impl =
        impl_list_.begin()->get();
    impl->FetchImpl(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
        base::BindOnce(&AsyncFetcher::OnFinish, weak_factory_.GetWeakPtr(),
                       std::move(callback), impl_list_.begin()),
        AsyncFetcherImpl::ResultType::NewError(
            chromeos::cros_healthd::mojom::ProbeError::New(
                chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError,
                "The callback was dropped without being called. This may due "
                "to the underlayer service crashed or connection error "
                "ocurred."))));
  }

 private:
  // Handles the result from the derived class.
  void OnFinish(
      typename AsyncFetcherImpl::ResultCallback callback,
      typename std::list<std::unique_ptr<AsyncFetcherImpl>>::iterator it,
      typename AsyncFetcherImpl::ResultPtrType result) {
    std::move(callback).Run(std::move(result));
    // Delete the impl after the function is returned.
    base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, std::move(*it));
    impl_list_.erase(it);
  }

  // The container of the implementations of this async fetcher. Use `std::list`
  // because its iterator remains valid after modifying.
  std::list<std::unique_ptr<AsyncFetcherImpl>> impl_list_;
  // Must be the last member of the class.
  base::WeakPtrFactory<AsyncFetcher<AsyncFetcherImpl>> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_ASYNC_FETCHER_H_
