// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/utils.h"

#include <string>
#include <vector>

#include <base/strings/stringprintf.h>

namespace federated {

namespace {
using ::chromeos::federated::mojom::ExamplePtr;
using ::chromeos::federated::mojom::FloatList;
using ::chromeos::federated::mojom::Int64List;
using ::chromeos::federated::mojom::ValueList;
}  // namespace

// TODO(alanlxl):  just random numbers, need a discussion
constexpr size_t kMaxStreamingExampleCount = 4000;
constexpr size_t kMinExampleCount = 1;

constexpr char kSessionStartedState[] = "started";
constexpr char kSessionStoppedState[] = "stopped";
constexpr char kUserDatabasePath[] = "/run/daemon-store/federated";
constexpr char kDatabaseFileName[] = "examples.db";

// Get the database file path with the given sanitized_username.
base::FilePath GetDatabasePath(const std::string& sanitized_username) {
  return base::FilePath(base::StringPrintf("%s/%s/%s", kUserDatabasePath,
                                           sanitized_username.c_str(),
                                           kDatabaseFileName));
}

tensorflow::Example ConvertToTensorFlowExampleProto(const ExamplePtr& example) {
  tensorflow::Example tf_example;
  auto& feature = *tf_example.mutable_features()->mutable_feature();

  for (const auto& iter : example->features->feature) {
    if (iter.second->which() == ValueList::Tag::INT64_LIST) {
      const std::vector<int64_t>& value_list =
          iter.second->get_int64_list()->value;
      *feature[iter.first].mutable_int64_list()->mutable_value() = {
          value_list.begin(), value_list.end()};
    } else if (iter.second->which() == ValueList::Tag::FLOAT_LIST) {
      const std::vector<double>& value_list =
          iter.second->get_float_list()->value;
      *feature[iter.first].mutable_float_list()->mutable_value() = {
          value_list.begin(), value_list.end()};
    } else if (iter.second->which() == ValueList::Tag::STRING_LIST) {
      const std::vector<std::string>& value_list =
          iter.second->get_string_list()->value;
      *feature[iter.first].mutable_bytes_list()->mutable_value() = {
          value_list.begin(), value_list.end()};
    }
  }
  return tf_example;
}

}  // namespace federated
