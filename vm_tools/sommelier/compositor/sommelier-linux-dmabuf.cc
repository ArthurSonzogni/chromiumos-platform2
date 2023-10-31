// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include <xf86drm.h>

#include "../sommelier.h"  // NOLINT(build/include_directory)
#include "../virtualization/linux-headers/virtgpu_drm.h"  // NOLINT(build/include_directory)
#include "sommelier-dmabuf-sync.h"   // NOLINT(build/include_directory)
#include "sommelier-formats.h"       // NOLINT(build/include_directory)
#include "sommelier-linux-dmabuf.h"  // NOLINT(build/include_directory)

#include "linux-dmabuf-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)

struct sl_host_buffer* sl_linux_dmabuf_create_host_buffer(
    struct sl_context* ctx,
    struct wl_client* client,
    struct wl_buffer* buffer_proxy,
    uint32_t buffer_id,
    const struct sl_linux_dmabuf_host_buffer_create_info* create_info) {
  struct sl_host_buffer* host_buffer =
      sl_create_host_buffer(ctx, client, buffer_id, buffer_proxy,
                            create_info->width, create_info->height, true);

  if (create_info->is_virtgpu_buffer) {
    host_buffer->sync_point = sl_sync_point_create(create_info->dmabuf_fd);
    host_buffer->sync_point->sync = sl_dmabuf_sync;
    host_buffer->shm_format =
        sl_shm_format_from_drm_format(create_info->format);

    // Create our DRM PRIME mmap container
    // This is simply a container that records necessary information
    // to map the DRM buffer through the GBM API's.
    // The GBM API's may need to perform a rather heavy copy of the
    // buffer into memory accessible by the CPU to perform the mapping
    // operation.
    // For this reason, the GBM mapping API's will not be used until we
    // are absolutely certain that the buffers contents need to be
    // accessed. This will be done through a call to sl_mmap_begin_access.
    //
    // We are also checking for a single plane format as this container
    // is currently only defined for single plane format buffers.

    if (sl_shm_format_num_planes(host_buffer->shm_format) == 1) {
      host_buffer->shm_mmap = sl_drm_prime_mmap_create(
          ctx->gbm, create_info->dmabuf_fd,
          sl_shm_format_bpp(host_buffer->shm_format),
          sl_shm_format_num_planes(host_buffer->shm_format),
          create_info->stride, create_info->width, create_info->height,
          create_info->format);

      // The buffer_resource must be set appropriately here or else
      // we will not perform the appropriate release at the end of
      // sl_host_surface_commit (see the end of that function for details).
      //
      // This release should only be done IF we successfully perform
      // the xshape interjection, as the host compositor will be using
      // a different buffer. For non shaped windows or fallbacks due
      // to map failure, where the buffer is relayed onto the host,
      // we should not release the buffer. That is the responsibility
      // of the host. The fallback path/non shape path takes care of this
      // by setting the host_surface contents_shm_mmap to nullptr.
      host_buffer->shm_mmap->buffer_resource = host_buffer->resource;
    }
  } else if (create_info->dmabuf_fd >= 0) {
    close(create_info->dmabuf_fd);
  }

  return host_buffer;
}

bool sl_linux_dmabuf_fixup_plane0_params(gbm_device* gbm,
                                         int32_t fd,
                                         uint32_t* out_stride,
                                         uint32_t* out_modifier_hi,
                                         uint32_t* out_modifier_lo) {
  /* silently perform fixups for virtgpu classic resources that were created
   * with implicit modifiers (resolved to explicit modifier in host minigbm)
   * and may have different stride for host buffer and shadow/guest buffer.
   * For context, see: crbug.com/892242 and b/230510320.
   */
  int drm_fd = gbm_device_get_fd(gbm);
  struct drm_prime_handle prime_handle;
  int ret;
  bool is_virtgpu_buffer = false;

  // First imports the prime fd to a gem handle. This will fail if this
  // function was not passed a prime handle that can be imported by the drm
  // device given to sommelier.
  memset(&prime_handle, 0, sizeof(prime_handle));
  prime_handle.fd = fd;
  ret = drmIoctl(drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime_handle);
  if (!ret) {
    struct drm_virtgpu_resource_info_cros info_arg;
    struct drm_gem_close gem_close;

    // Then attempts to get resource information. This will fail silently if
    // the drm device passed to sommelier is not a virtio-gpu device.
    memset(&info_arg, 0, sizeof(info_arg));
    info_arg.bo_handle = prime_handle.handle;
    info_arg.type = VIRTGPU_RESOURCE_INFO_TYPE_EXTENDED;
    ret = drmIoctl(drm_fd, DRM_IOCTL_VIRTGPU_RESOURCE_INFO_CROS, &info_arg);
    // Correct stride if we are able to get proper resource info.
    if (!ret) {
      is_virtgpu_buffer = true;
      if (info_arg.stride) {
        *out_stride = info_arg.stride;
        *out_modifier_hi = info_arg.format_modifier >> 32;
        *out_modifier_lo = info_arg.format_modifier & 0xFFFFFFFF;
      }
    }

    // Always close the handle we imported.
    memset(&gem_close, 0, sizeof(gem_close));
    gem_close.handle = prime_handle.handle;
    drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
  }

  return is_virtgpu_buffer;
}
