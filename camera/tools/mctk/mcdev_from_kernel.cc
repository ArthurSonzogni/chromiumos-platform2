/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 media controller.
 * It will be populated with data from a kernel device.
 *
 * The resulting model will own the fd to the V4L2 media-ctl.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 media-ctl.
 *  - on failure: nullptr.
 */

#include "tools/mctk/mcdev.h"

#include <errno.h>
#include <linux/media.h>
#include <sys/ioctl.h>

#include <memory>
#include <utility> /* std::move */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/entity.h"

/* Load the graph from an open /dev/mediaX device and
 * build a class and its child nodes.
 */
std::unique_ptr<V4lMcDev> V4lMcDev::CreateFromKernel(int fd) {
  MCTK_ASSERT(fd >= 0);

  auto mcdev = std::make_unique<V4lMcDev>();

  /* Get the device name, etc. */
  if (ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mcdev->info_) < 0) {
    MCTK_PERROR("MEDIA_IOC_DEVICE_INFO");
    return nullptr;
  }

  /* This assumes there will never be an entity #0.
   * This has to be true, otherwise the API makes no sense.
   */
  struct media_entity_desc entity_desc;
  int ret;

  entity_desc.id = MEDIA_ENT_ID_FLAG_NEXT;
  ret = ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &entity_desc);
  while (ret >= 0) {
    /* Add entity */
    std::unique_ptr<V4lMcEntity> new_e =
        V4lMcEntity::CreateFromKernel(entity_desc, fd);

    if (!new_e) {
      MCTK_ASSERT(0);
      return nullptr;
    }

    mcdev->entities_.push_back(std::move(new_e));

    entity_desc.id |= MEDIA_ENT_ID_FLAG_NEXT;
    ret = ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &entity_desc);
  }

  /* EINVAL means we're done enumerating. */
  if (errno != EINVAL) {
    MCTK_PERROR("MEDIA_IOC_ENUM_ENTITIES");
    return nullptr;
  }

  /* Sync up all lists and pointers */
  mcdev->BuildCrosslinks();

  /* Only keep the fd around if we have set up successfully until the end.
   * This way, the caller knows whether they have relinquished ownership
   * of the fd, or need to close it themselves.
   */
  mcdev->fd_ = fd;

  return mcdev;
}
