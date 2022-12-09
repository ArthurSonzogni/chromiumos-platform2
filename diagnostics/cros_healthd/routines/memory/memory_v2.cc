// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory/memory_v2.h"

#include <utility>

#include <base/check.h>
#include <base/notreached.h>

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

MemoryRoutineV2::MemoryRoutineV2(Context* context) : context_(context) {
  DCHECK(context_);
}

MemoryRoutineV2::~MemoryRoutineV2() = default;

void MemoryRoutineV2::OnStart() {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
