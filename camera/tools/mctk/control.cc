/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/mctk/control.h"

#include <linux/videodev2.h>

bool V4lMcControl::IsReadOnly() {
  return (this->desc_.flags & V4L2_CTRL_FLAG_READ_ONLY);
}
