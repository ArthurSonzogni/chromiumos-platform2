// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_VPD_CACHED_H_
#define RUNTIME_PROBE_FUNCTIONS_VPD_CACHED_H_

#include <memory>
#include <string>

#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Read cached VPD information from sysfs.
//
// System usually boots with VPD cached, we read the cached version to avoid
// the delay of accessing the flashrom. If VPD data changed after boot, this
// function will not reflect that.
//
// In this first implementation, only one argument will be taken, that is the
// key in the RO_VPD area to read.

class VPDCached : public PrivilegedProbeFunction {
 public:
  NAME_PROBE_FUNCTION("vpd_cached");

  // Define a parser for this function.
  //
  // @args dict_value: a JSON dictionary to parse arguments from.
  //
  // @return pointer to new `VPDCached` instance on success, nullptr otherwise.
  template <typename T>
  static auto FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    PARSE_ARGUMENT(vpd_name);
    PARSE_END();
  }

 private:
  DataType EvalImpl() const override;

  std::string vpd_name_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_VPD_CACHED_H_
