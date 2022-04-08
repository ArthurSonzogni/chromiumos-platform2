// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_ASYNC_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_ASYNC_FETCHER_H_

#include <type_traits>
#include <utility>
#include <vector>

#include <base/check.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Provides a template for implementing async fetchers. This provides two useful
// behaviors:
// 1. Won't have new request before the last one finished.
//    When there is a unfinished request, all the additional requests are pushed
//    to a queue and will be fulfilled with the same result of the first
//    request.
//    With this the derived classes don't need to worry about the private states
//    being access by multiple requests, which could cause race condition. Note
//    that this assume that fetch result are the same in the short period.
// 2. Returns error when the callback is not called.
//    In some cases (e.g. mojo disconnects) the callback could be dropped
//    without being called. It will cause memory leak because we keep the upper
//    callbacks in a queue. This class handle this so even the derived classes
//    don't call the callback, the upper callbacks will still be called.
//
// |ResultType| should be a mojo union type contains |error| field which is a
// |mojom::ProbeError|.
template <typename ResultType>
class AsyncFetcher : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  // The ptr type of ResultType. It is gotten from the return type of
  // |ResultType::New()|.
  using ResultPtrType =
      typename std::invoke_result<decltype(&ResultType::New)>::type;

  // The callback type to get the fetch result.
  using ResultCallback = base::OnceCallback<void(ResultPtrType)>;

  // Fetches the telemetry data.
  void Fetch(ResultCallback callback) {
    pending_callbacks_.push_back(std::move(callback));
    // Return if there is already a pending callback. This means that the first
    // call is not yet finished. The callbacks will be fulfilled after the first
    // is finished.
    if (pending_callbacks_.size() > 1)
      return;

    FetchImpl(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
        base::BindOnce(&AsyncFetcher::OnFinish, weak_factory_.GetWeakPtr()),
        ResultType::NewError(chromeos::cros_healthd::mojom::ProbeError::New(
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError,
            "The callback was dropped without being called. This may due to "
            "the underlayer service crashed or connection error ocurred."))));
  }

 private:
  // The derived classes should implement this for the actual fetching logic.
  // This function is guaranteed that once it is called, it won't be called
  // again until the callback is fulfilled.
  virtual void FetchImpl(ResultCallback callback) = 0;

  // Handles the result from the derived class.
  void OnFinish(ResultPtrType result) {
    DCHECK(!pending_callbacks_.empty());
    std::vector<ResultCallback> callbacks;
    // Move pending_callbacks_ before calling callbacks because it may be
    // modified while running callbacks. (i.e. Calling |Fetch()| again, though
    // it unlikely happen.)
    callbacks.swap(pending_callbacks_);
    for (size_t i = 1; i < callbacks.size(); ++i) {
      std::move(callbacks[i]).Run(result.Clone());
    }
    std::move(callbacks[0]).Run(std::move(result));
  }

  // The queue for the additional callbacks.
  std::vector<ResultCallback> pending_callbacks_;
  // Must be the last member of the class.
  base::WeakPtrFactory<AsyncFetcher<ResultType>> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_ASYNC_FETCHER_H_
