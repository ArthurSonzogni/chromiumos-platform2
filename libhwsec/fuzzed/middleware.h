// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_MIDDLEWARE_H_
#define LIBHWSEC_FUZZED_MIDDLEWARE_H_

#include <type_traits>

#include <base/task/task_runner.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/fuzzed/basic_objects.h"
#include "libhwsec/middleware/middleware_derivative.h"

namespace hwsec {

template <>
struct FuzzedObject<MiddlewareDerivative> {
  MiddlewareDerivative operator()(FuzzedDataProvider& provider) const {
    return MiddlewareDerivative{
        .task_runner = base::SequencedTaskRunnerHandle::IsSet()
                           ? base::SequencedTaskRunnerHandle::Get()
                           : nullptr,
        .thread_id = base::PlatformThread::CurrentId(),
        .middleware = nullptr,
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_MIDDLEWARE_H_
