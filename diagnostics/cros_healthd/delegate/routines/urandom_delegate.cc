// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/routines/urandom_delegate.h"

#include <utility>

#include <base/files/file.h>
#include <base/memory/ptr_util.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/delegate/constants.h"

namespace diagnostics {

std::unique_ptr<UrandomDelegate> UrandomDelegate::Create() {
  base::File urandom_file(GetRootedPath(path::kUrandomPath),
                          base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!urandom_file.IsValid()) {
    return nullptr;
  }

  return base::WrapUnique(new UrandomDelegate(std::move(urandom_file)));
}

UrandomDelegate::UrandomDelegate(base::File urandom_file)
    : urandom_file_(std::move(urandom_file)) {}

UrandomDelegate::~UrandomDelegate() = default;

bool UrandomDelegate::Run() {
  return kNumBytesRead == urandom_file_.Read(/*offset=*/0,
                                             /*data=*/urandom_data_,
                                             /*size=*/kNumBytesRead);
}

}  // namespace diagnostics
