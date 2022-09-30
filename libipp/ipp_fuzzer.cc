// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libipp/attribute.h"
#include "libipp/frame.h"

#include <cstdint>
#include <vector>

void BrowseCollection(ipp::Collection* coll) {
  const std::vector<ipp::Attribute*> attrs = coll->GetAllAttributes();
  for (ipp::Attribute* attr : attrs) {
    if (attr->Tag() == ipp::ValueTag::collection) {
      const size_t n = attr->Size();
      for (size_t i = 0; i < n; ++i) {
        BrowseCollection(attr->GetCollection(i));
      }
    }
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Parse the input data.
  ipp::ParsingResults log;
  ipp::Frame frame(data, size, &log);
  // Browse the obtained frame.
  for (ipp::GroupTag gt : ipp::kGroupTags) {
    std::vector<ipp::Collection*> colls = frame.GetGroups(gt);
    for (ipp::Collection* coll : colls) {
      BrowseCollection(coll);
    }
  }

  return 0;
}
