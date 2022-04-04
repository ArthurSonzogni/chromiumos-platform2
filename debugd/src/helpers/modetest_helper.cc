// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>

#include <debugd/src/helpers/modetest_helper_utils.h>

int main(int argc, char** argv) {
  debugd::modetest_helper_utils::EDIDFilter edid_filter;
  std::string line;
  while (std::getline(std::cin, line)) {
    edid_filter.ProcessLine(line);
    std::cout << line << std::endl;
  }
  return 0;
}
