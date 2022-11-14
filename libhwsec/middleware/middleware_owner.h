// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_MIDDLEWARE_MIDDLEWARE_OWNER_H_
#define LIBHWSEC_MIDDLEWARE_MIDDLEWARE_OWNER_H_

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

#include <absl/base/attributes.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/task_runner.h>
#include <base/threading/thread.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/error/tpm_retry_handler.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/middleware/middleware_derivative.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/threading_mode.h"

#if USE_FUZZER
#include <fuzzer/FuzzedDataProvider.h>
#include "libhwsec/fuzzed/basic_objects.h"
#include "libhwsec/fuzzed/hwsec_objects.h"
#include "libhwsec/fuzzed/ifx_info.h"
#include "libhwsec/fuzzed/key_management.h"
#include "libhwsec/fuzzed/middleware.h"
#include "libhwsec/fuzzed/pinweaver.h"
#include "libhwsec/fuzzed/recovery_crypto.h"
#include "libhwsec/fuzzed/signature_sealing.h"
#endif

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

namespace hwsec {

class Middleware;

class HWSEC_EXPORT MiddlewareOwner {
 public:
  friend class Middleware;

  explicit MiddlewareOwner(ThreadingMode mode);

  // Constructor for custom backend.
  MiddlewareOwner(std::unique_ptr<Backend> custom_backend, ThreadingMode mode);

  virtual ~MiddlewareOwner();

  MiddlewareDerivative Derive();

#if USE_FUZZER
  void set_data_provider(FuzzedDataProvider* data_provider) {
    data_provider_ = data_provider;
  }
#endif

 private:
  void InitThreadingMode(ThreadingMode mode);
  void InitBackend();
  void InitWithCustomBackend(std::unique_ptr<Backend> custom_backend);
  void FiniBackend();

  std::unique_ptr<base::Thread> background_thread_;

  scoped_refptr<base::TaskRunner> task_runner_;
  std::atomic<base::PlatformThreadId> thread_id_;

#if USE_FUZZER
  FuzzedDataProvider* data_provider_ = nullptr;
#endif

  // Use thread_local to ensure the proxy and backend could only be accessed on
  // a thread.
  ABSL_CONST_INIT static inline thread_local std::unique_ptr<Proxy> proxy_;
  ABSL_CONST_INIT static inline thread_local std::unique_ptr<Backend> backend_;

  // Member variables should appear before the WeakPtrFactory, to ensure
  // that any WeakPtrs to Controller are invalidated before its members
  // variable's destructors are executed, rendering them invalid.
  base::WeakPtrFactory<MiddlewareOwner> weak_factory_{this};
};

}  // namespace hwsec

#endif  // LIBHWSEC_MIDDLEWARE_MIDDLEWARE_OWNER_H_
