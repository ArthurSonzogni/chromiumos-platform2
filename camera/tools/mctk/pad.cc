/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/pad.h"

#include <linux/media.h>
#include <linux/types.h>

#include "tools/mctk/debug.h"
#include "tools/mctk/link.h"

V4lMcLink* V4lMcPad::LinkBySinkIds(__u32 entity, __u16 index) {
  for (auto link : links_) {
    if (link->desc_.sink.entity == entity && link->desc_.sink.index == index)
      return link;
  }

  return nullptr;
}
