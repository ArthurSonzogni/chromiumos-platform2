/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Setters for abstract models of V4L2 links.
 *
 * If the model has an fd for a kernel device set, then the setters will
 * propagate the new values to the kernel.
 *
 * Return values:
 *  - on success: true
 *  - on failure: false
 */

#include "tools/mctk/link.h"

#include <linux/media.h>
#include <linux/types.h>
#include <sys/ioctl.h>

#include "tools/mctk/debug.h"

bool V4lMcLink::SetEnable(bool enable) {
  if (desc_.flags & MEDIA_LNK_FL_INTERFACE_LINK) {
    MCTK_ERR("Tried to change an interface link.");
    return false;
  }

  if (desc_.flags & MEDIA_LNK_FL_IMMUTABLE) {
    MCTK_ERR("Tried to change an immutable link.");
    return false;
  }

  desc_.flags &= ~MEDIA_LNK_FL_ENABLED;
  if (enable)
    desc_.flags |= MEDIA_LNK_FL_ENABLED;

  /* If mc is linked to a real device, apply the update to the hardware */
  if (fd_mc_) {
    if (ioctl(*fd_mc_, MEDIA_IOC_SETUP_LINK, &desc_) < 0) {
      MCTK_PERROR("ioctl(MEDIA_IOC_SETUP_LINK)");
      return false;
    }
  }

  return true;
}
