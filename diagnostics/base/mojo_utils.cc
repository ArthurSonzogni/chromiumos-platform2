// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/base/mojo_utils.h"

#include <cstring>
#include <string_view>
#include <utility>

#include <base/files/file.h>
#include <base/files/platform_file.h>
#include <base/files/scoped_file.h>
#include <base/memory/platform_shared_memory_region.h>
#include <base/memory/read_only_shared_memory_region.h>
#include <base/memory/shared_memory_mapping.h>
#include <base/unguessable_token.h>
#include <mojo/public/c/system/types.h>
#include <mojo/public/cpp/system/handle.h>
#include <mojo/public/cpp/system/platform_handle.h>

namespace diagnostics {

base::ReadOnlySharedMemoryMapping GetReadOnlySharedMemoryMappingFromMojoHandle(
    mojo::ScopedHandle handle) {
  base::ScopedPlatformFile platform_file;
  auto result = mojo::UnwrapPlatformFile(std::move(handle), &platform_file);
  if (result != MOJO_RESULT_OK) {
    return base::ReadOnlySharedMemoryMapping();
  }

  base::File file(std::move(platform_file));
  const int64_t file_size = file.GetLength();
  if (file_size <= 0) {
    return base::ReadOnlySharedMemoryMapping();
  }

  // Use of base::subtle::PlatformSharedMemoryRegion is necessary on process
  // boundaries to converting between SharedMemoryRegion and its handle (fd).
  base::ReadOnlySharedMemoryRegion shm_region =
      base::ReadOnlySharedMemoryRegion::Deserialize(
          base::subtle::PlatformSharedMemoryRegion::Take(
              base::ScopedFD(file.TakePlatformFile()),
              base::subtle::PlatformSharedMemoryRegion::Mode::kReadOnly,
              file_size, base::UnguessableToken::Create()));
  return shm_region.Map();
}

mojo::ScopedHandle CreateReadOnlySharedMemoryRegionMojoHandle(
    std::string_view content) {
  if (content.empty()) {
    return mojo::ScopedHandle();
  }
  base::MappedReadOnlyRegion region_mapping =
      base::ReadOnlySharedMemoryRegion::Create(content.length());
  base::ReadOnlySharedMemoryRegion read_only_region =
      std::move(region_mapping.region);
  base::WritableSharedMemoryMapping writable_mapping =
      std::move(region_mapping.mapping);
  if (!read_only_region.IsValid() || !writable_mapping.IsValid()) {
    return mojo::ScopedHandle();
  }
  memcpy(writable_mapping.GetMemoryAs<char>(), content.data(),
         content.length());

  // Use of base::subtle::PlatformSharedMemoryRegion is necessary on process
  // boundaries to converting between SharedMemoryRegion and its handle (fd).
  base::subtle::PlatformSharedMemoryRegion platform_shm =
      base::ReadOnlySharedMemoryRegion::TakeHandleForSerialization(
          std::move(read_only_region));
  return mojo::WrapPlatformFile(platform_shm.PassPlatformHandle().fd);
}

}  // namespace diagnostics
