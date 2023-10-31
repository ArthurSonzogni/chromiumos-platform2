/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Getters for abstract models of V4L2 entities. */

#include "tools/mctk/link.h"

#include <linux/media.h>
#include <linux/types.h>
#include <sys/ioctl.h>

#include "tools/mctk/debug.h"

bool V4lMcLink::IsDataLink() {
  return ((desc_.flags & MEDIA_LNK_FL_LINK_TYPE) == MEDIA_LNK_FL_DATA_LINK);
}

bool V4lMcLink::IsImmutable() {
  return !!(desc_.flags & MEDIA_LNK_FL_IMMUTABLE);
}

bool V4lMcLink::IsEnabled() {
  return !!(desc_.flags & MEDIA_LNK_FL_ENABLED);
}
