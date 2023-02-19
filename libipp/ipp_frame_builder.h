// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_FRAME_BUILDER_H_
#define LIBIPP_IPP_FRAME_BUILDER_H_

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "frame.h"
#include "ipp_frame.h"

namespace ipp {

// forward declarations
class Attribute;
class Collection;
class Frame;

class FrameBuilder {
 public:
  // Constructor, both parameters must not be nullptr. |frame| is used as
  // internal buffer to store intermediate form of data to send. All spotted
  // issues are logged to |log| (by push_back()).
  FrameBuilder(FrameData* frame, std::vector<Log>* log) : frame_(frame) {}

  // Build a content of the frame from the given object.
  void BuildFrameFromPackage(const Frame* package);

  // Returns the current frame size in bytes. Call it after
  // BuildFrameFromPackage(...) to get the size of the output buffer.
  std::size_t GetFrameLength() const;

  // Write data to given buffer (use the method above to learn about required
  // size of the buffer).
  void WriteFrameToBuffer(uint8_t* ptr);

 private:
  // Copy/move/assign constructors/operators are forbidden.
  FrameBuilder(const FrameBuilder&) = delete;
  FrameBuilder(FrameBuilder&&) = delete;
  FrameBuilder& operator=(const FrameBuilder&) = delete;
  FrameBuilder& operator=(FrameBuilder&&) = delete;

  // Internal buffer.
  FrameData* frame_;
};

}  // namespace ipp

#endif  //  LIBIPP_IPP_FRAME_BUILDER_H_
