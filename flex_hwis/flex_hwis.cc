// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"

namespace flex_hwis {

FlexHwisSender::FlexHwisSender(const base::FilePath& base_path)
    : base_path_(base_path) {}

Result FlexHwisSender::CollectAndSend() {
  return Result::Sent;
}

}  // namespace flex_hwis
