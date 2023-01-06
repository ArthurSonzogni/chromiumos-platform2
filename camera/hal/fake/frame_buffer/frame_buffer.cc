/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/frame_buffer/frame_buffer.h"

namespace cros {

FrameBuffer::ScopedMapping::ScopedMapping() = default;
FrameBuffer::ScopedMapping::~ScopedMapping() = default;

FrameBuffer::FrameBuffer() = default;
FrameBuffer::~FrameBuffer() = default;

}  // namespace cros
