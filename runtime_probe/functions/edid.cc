// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/edid.h"

#include <pcrecpp.h>

#include <numeric>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>

#include "runtime_probe/utils/edid.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

namespace {

base::Value ProbeEdidPath(const base::FilePath& edid_path) {
  VLOG(2) << "Processing the node \"" << edid_path.value() << "\"";

  std::string raw_bytes;
  if (!base::ReadFileToString(edid_path, &raw_bytes))
    return base::Value(base::Value::Type::DICTIONARY);
  if (raw_bytes.length() == 0) {
    return base::Value(base::Value::Type::DICTIONARY);
  }

  base::Value res(base::Value::Type::DICTIONARY);
  auto edid =
      Edid::From(std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end()));
  if (!edid) {
    return res;
  }
  res.SetStringKey("vendor", edid->vendor);
  res.SetStringKey("product_id", base::StringPrintf("%04x", edid->product_id));
  res.SetIntKey("width", edid->width);
  res.SetIntKey("height", edid->height);
  res.SetStringKey("path", edid_path.value());
  return res;
}

}  // namespace

EdidFunction::DataType EdidFunction::EvalImpl() const {
  DataType result{};

  for (const auto& edid_pattern : edid_patterns_) {
    for (const auto& edid_path : Glob(edid_pattern)) {
      auto node_res = ProbeEdidPath(edid_path);
      if (node_res.DictEmpty())
        continue;

      result.push_back(std::move(node_res));
    }
  }

  return result;
}

}  // namespace runtime_probe
