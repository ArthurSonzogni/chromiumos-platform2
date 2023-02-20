// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_FRAME_BUILDER_H_
#define LIBIPP_IPP_FRAME_BUILDER_H_

#include <cstdint>
#include <list>
#include <vector>

#include "frame.h"
#include "ipp_frame.h"

namespace ipp {

struct GroupAsTNVs {
  GroupTag tag;
  std::list<TagNameValue> content;
};

// forward declarations
class Frame;

// Build a content of the frame from the given object.
std::vector<GroupAsTNVs> PreprocessFrame(const Frame& frame);

// Returns the current frame size in bytes. Call it after
// BuildFrameFromPackage(...) to get the size of the output buffer.
std::size_t GetFrameLength(const Frame& frame,
                           const std::vector<GroupAsTNVs>& tnvs);

// Write data to given buffer (use the method above to learn about required
// size of the buffer).
void WriteFrameToBuffer(const Frame& frame,
                        const std::vector<GroupAsTNVs>& tnvs,
                        uint8_t* ptr);

}  // namespace ipp

#endif  //  LIBIPP_IPP_FRAME_BUILDER_H_
