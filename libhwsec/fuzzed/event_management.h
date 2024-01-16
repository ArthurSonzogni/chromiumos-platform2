// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_EVENT_MANAGEMENT_H_
#define LIBHWSEC_FUZZED_EVENT_MANAGEMENT_H_

#include <string>
#include <type_traits>

#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/backend/event_management.h"
#include "libhwsec/fuzzed/basic_objects.h"
#include "libhwsec/fuzzed/middleware.h"
#include "libhwsec/structures/event.h"

namespace hwsec {

template <>
struct FuzzedObject<ScopedEvent> {
  ScopedEvent operator()(FuzzedDataProvider& provider) const {
    return ScopedEvent(FuzzedObject<std::string>()(provider),
                       FuzzedObject<MiddlewareDerivative>()(provider));
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_EVENT_MANAGEMENT_H_
