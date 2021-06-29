/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/metadata_logger.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_writer.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kKeyFrameNumber[] = "frame_number";

}  // namespace

MetadataLogger::MetadataLogger(Options options) : options_(options) {}

MetadataLogger::~MetadataLogger() {
  if (options_.auto_dump_on_destruction) {
    DumpMetadata();
  }
}

template <>
void MetadataLogger::Log(int frame_number, std::string key, uint8_t value) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  entry.SetIntKey(key, value);
}

template <>
void MetadataLogger::Log(int frame_number, std::string key, int32_t value) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  entry.SetIntKey(key, value);
}

template <>
void MetadataLogger::Log(int frame_number, std::string key, float value) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  entry.SetDoubleKey(key, value);
}

template <>
void MetadataLogger::Log(int frame_number, std::string key, int64_t value) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  // JSON does not support int64, so let's use double instead.
  entry.SetDoubleKey(key, value);
}

template <>
void MetadataLogger::Log(int frame_number, std::string key, double value) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  entry.SetDoubleKey(key, value);
}

template <>
void MetadataLogger::Log(int frame_number, std::string key, Rational value) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  entry.SetDoubleKey(key,
                     static_cast<double>(value.numerator) / value.denominator);
}

template <>
void MetadataLogger::Log(int frame_number,
                         std::string key,
                         base::span<const uint8_t> values) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  std::vector<base::Value> value_list;
  for (const auto& v : values) {
    value_list.emplace_back(static_cast<int>(v));
  }
  entry.SetKey(key, base::Value(std::move(value_list)));
}

template <>
void MetadataLogger::Log(int frame_number,
                         std::string key,
                         base::span<const int32_t> values) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  std::vector<base::Value> value_list;
  for (const auto& v : values) {
    value_list.emplace_back(v);
  }
  entry.SetKey(key, base::Value(std::move(value_list)));
}

template <>
void MetadataLogger::Log(int frame_number,
                         std::string key,
                         base::span<const float> values) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  std::vector<base::Value> value_list;
  for (const auto& v : values) {
    value_list.emplace_back(static_cast<double>(v));
  }
  entry.SetKey(key, base::Value(std::move(value_list)));
}

template <>
void MetadataLogger::Log(int frame_number,
                         std::string key,
                         base::span<const int64_t> values) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  std::vector<base::Value> value_list;
  for (const auto& v : values) {
    value_list.emplace_back(static_cast<double>(v));
  }
  entry.SetKey(key, base::Value(std::move(value_list)));
}

template <>
void MetadataLogger::Log(int frame_number,
                         std::string key,
                         base::span<const double> values) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  std::vector<base::Value> value_list;
  for (const auto& v : values) {
    value_list.emplace_back(v);
  }
  entry.SetKey(key, base::Value(std::move(value_list)));
}

template <>
void MetadataLogger::Log(int frame_number,
                         std::string key,
                         base::span<const Rational> values) {
  base::Value& entry = GetOrCreateEntry(frame_number);
  std::vector<base::Value> value_list;
  for (const auto& v : values) {
    value_list.emplace_back(static_cast<double>(v.numerator) / v.denominator);
  }
  entry.SetKey(key, base::Value(std::move(value_list)));
}

bool MetadataLogger::DumpMetadata() {
  std::vector<base::Value> metadata_to_dump;
  for (const auto& entry : frame_metadata_) {
    metadata_to_dump.emplace_back(entry.second.Clone());
  }
  std::string json_string;
  if (!base::JSONWriter::WriteWithOptions(
          base::Value(metadata_to_dump), base::JSONWriter::OPTIONS_PRETTY_PRINT,
          &json_string)) {
    LOGF(WARNING) << "Can't jsonify frame metadata";
    return false;
  }
  if (!base::WriteFile(options_.dump_path, json_string)) {
    LOGF(WARNING) << "Can't write frame metadata";
    return false;
  }
  return true;
}

base::Value& MetadataLogger::GetOrCreateEntry(int frame_number) {
  if (frame_metadata_.count(frame_number) == 0) {
    if (frame_metadata_.size() == options_.ring_buffer_capacity) {
      frame_metadata_.erase(frame_metadata_.begin());
    }
    base::Value entry(base::Value::Type::DICTIONARY);
    entry.SetIntKey(kKeyFrameNumber, frame_number);
    frame_metadata_.insert({frame_number, std::move(entry)});
  }
  return frame_metadata_[frame_number];
}

}  // namespace cros
