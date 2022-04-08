// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_CALLBACK_BARRIER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_CALLBACK_BARRIER_H_

#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

namespace diagnostics {

// Calls a |base::OnceClosure| after all the dependent |base::OnceCallback<T>|
// are called. This is useful when tracking multiple async calls.
//
// Caveat:
//   1. This is not thread-safe.
//   2. Make sure that the |CallbackBarrier| will be destructed after all
//      dependencies are added. Otherwise, it cannot know whether there will be
//      another dependency or not.
//
// Example:
//   CallbackBarrier barrier{/*on_success*/base::BindOnce(...),
//                           /*on_error=*/base::BindOnce(...)};
//   foo->DoSomeThing(barrier.Depend(base::BindOnce(...)));
//   foo->DoOtherThing(barrier.Depend(base::BindOnce(...)));
class CallbackBarrier {
 public:
  // |on_success| is called when all the dependencies are called.
  // |on_error| is called when there is a dependency which is dropped without
  // being called.
  CallbackBarrier(base::OnceClosure on_success, base::OnceClosure on_error);
  CallbackBarrier(const CallbackBarrier&) = delete;
  const CallbackBarrier& operator=(const CallbackBarrier&) = delete;
  ~CallbackBarrier();

  // Makes a |base::OnceCallback<T>| a dependency. Returns the wrapped once
  // callback to be used.
  template <typename T>
  base::OnceCallback<T> Depend(base::OnceCallback<T> callback) {
    tracker_->IncreaseUncalledCallbackNum();
    // If the callback is dropped, |DecreaseCallbackNum| won't be called so we
    // know that there is an uncalled dependency.
    return std::move(callback).Then(
        base::BindOnce(&Tracker::DecreaseUncalledCallbackNum, tracker_));
  }

 private:
  // Tracks each dependency. When all the references are gone, it checks the
  // number of uncalled callbacks and calls the result handler (either the
  // success or the error).
  class Tracker : public base::RefCounted<Tracker> {
   public:
    Tracker(base::OnceClosure on_success, base::OnceClosure on_error);
    Tracker(const Tracker&) = delete;
    const Tracker& operator=(const Tracker&) = delete;

    // Increases the number of uncalled callbacks.
    void IncreaseUncalledCallbackNum();

    // Decreases the number of uncalled callbacks.
    void DecreaseUncalledCallbackNum();

   private:
    ~Tracker();

    // The number of the uncalled callbacks.
    uint32_t num_uncalled_callback_ = 0;
    // The success handler.
    base::OnceClosure on_success_;
    // The error handler.
    base::OnceClosure on_error_;

    friend class base::RefCounted<Tracker>;
  };

  scoped_refptr<Tracker> tracker_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_CALLBACK_BARRIER_H_
