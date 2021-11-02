// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_UTILITY_SYNCHRONIZED_H_
#define LIBHWSEC_FOUNDATION_UTILITY_SYNCHRONIZED_H_

#include <utility>

#include <base/synchronization/lock.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"

namespace hwsec_foundation {
namespace utility {

template <class T>
class SynchronizedHandle;

// Wrapper that can provide synchronized access to the underlying class object.
// The Lock() method returns a SynchronizedHandle which acquires a lock to
// ensure exclusive access of the object. Note that the underlying
// implementation use locks, so you should be aware of when/how to use this as
// if you are using ordinary locks to prevent deadlock.
//
// Example usage:
//   Synchronized<std::vector<int>> v;
//   v.Lock()->push_back(1);
//
//   SynchronizedHandle<std::vector<int>> handle = v.Lock();
//   int original_size = handle->size();
//   handle->push_back(2);
//   assert(handle->size() == original_size + 1);
template <class T>
class HWSEC_FOUNDATION_EXPORT Synchronized {
 public:
  template <typename... Args>
  explicit Synchronized(Args&&... args) : data_(std::forward<Args>(args)...) {}

  Synchronized(const Synchronized&) = delete;
  Synchronized& operator=(const Synchronized&) = delete;

  ~Synchronized() {}

  // Returns a SynchronizedHandle which acquires a lock to ensure exclusive
  // access of the object. The lock is released after the SynchronizedHandle is
  // out of scope. If there is already a SynchronizedHandle instance generated
  // by this method, this method blocks until that handle is out of scope.
  SynchronizedHandle<T> Lock() { return SynchronizedHandle(this); }

 private:
  T data_;
  base::Lock lock_;

  friend class SynchronizedHandle<T>;
};

// Returned by the Lock() method of Synchronized. Provides exclusive
// access of the object, and derefs into it.
template <class T>
class HWSEC_FOUNDATION_EXPORT SynchronizedHandle {
 public:
  SynchronizedHandle(const SynchronizedHandle&) = delete;

  SynchronizedHandle& operator=(const SynchronizedHandle&) = delete;

  ~SynchronizedHandle() {}

  T* operator->() { return &synchronized_->data_; }

 private:
  explicit SynchronizedHandle(Synchronized<T>* synchronized)
      : synchronized_(synchronized), auto_lock_(synchronized->lock_) {}

  Synchronized<T>* synchronized_;
  base::AutoLock auto_lock_;

  friend class Synchronized<T>;
};

}  // namespace utility
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_UTILITY_SYNCHRONIZED_H_
