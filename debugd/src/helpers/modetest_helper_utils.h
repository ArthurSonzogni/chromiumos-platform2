// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_HELPERS_MODETEST_HELPER_UTILS_H_
#define DEBUGD_SRC_HELPERS_MODETEST_HELPER_UTILS_H_

#include <string>

namespace debugd {
namespace modetest_helper_utils {

// EDIDFilter will scrub the serial number from the EDID property of `modetest`
// output.
class EDIDFilter {
 public:
  EDIDFilter();
  // Call ProcessLine for each line of `modetest`. ProcessLine may modify the
  // line in place when it finds an EDID serial number.
  void ProcessLine(std::string& line);

 private:
  bool saw_edid_property_;
  bool saw_value_;
};
}  // namespace modetest_helper_utils
}  // namespace debugd

#endif  // DEBUGD_SRC_HELPERS_MODETEST_HELPER_UTILS_H_
