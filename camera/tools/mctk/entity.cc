/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/entity.h"

#include <linux/types.h>
#include <unistd.h>

#include <optional>
#include <vector>

#include "tools/mctk/control.h"
#include "tools/mctk/debug.h"
#include "tools/mctk/pad.h"

V4lMcEntity::~V4lMcEntity() {
  if (fd_)
    close(*fd_);
}

V4lMcControl* V4lMcEntity::ControlById(__u32 id) {
  for (auto& control : controls_) {
    if (control->desc_.id == id)
      return control.get();
  }

  return nullptr;
}

V4lMcPad* V4lMcEntity::PadByIndex(__u16 index) {
  for (auto& pad : pads_) {
    if (pad->desc_.index == index)
      return pad.get();
  }

  return nullptr;
}
