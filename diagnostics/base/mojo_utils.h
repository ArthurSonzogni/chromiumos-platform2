// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BASE_MOJO_UTILS_H_
#define DIAGNOSTICS_BASE_MOJO_UTILS_H_

#include <string_view>

#include <base/memory/shared_memory_mapping.h>
#include <brillo/brillo_export.h>
#include <mojo/public/cpp/system/handle.h>

namespace diagnostics {

// Allows to get access to the buffer in read only shared memory. It converts
// mojo::Handle to base::ReadOnlySharedMemoryRegion.
//
// |handle| must be a valid mojo handle of the non-empty buffer in the shared
// memory.
//
// Returns invalid |base::ReadOnlySharedMemoryMapping| if error.
BRILLO_EXPORT base::ReadOnlySharedMemoryMapping
GetReadOnlySharedMemoryMappingFromMojoHandle(mojo::ScopedHandle handle);

// Allocates buffer in shared memory, copies |content| to the buffer and
// converts shared buffer handle into |mojo::ScopedHandle|.
//
// Allocated shared memory is read only for another process.
//
// Returns invalid |mojo::ScopedHandle| if error happened or |content| is empty.
BRILLO_EXPORT mojo::ScopedHandle CreateReadOnlySharedMemoryRegionMojoHandle(
    std::string_view content);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_BASE_MOJO_UTILS_H_
