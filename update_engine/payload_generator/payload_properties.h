// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_GENERATOR_PAYLOAD_PROPERTIES_H_
#define UPDATE_ENGINE_PAYLOAD_GENERATOR_PAYLOAD_PROPERTIES_H_

#include <string>

#include <brillo/key_value_store.h>
#include <brillo/secure_blob.h>

namespace chromeos_update_engine {

// A class for extracting information about a payload from the payload file
// itself. Currently the metadata can be exported as a json file or a key/value
// properties file. But more can be added if required.
class PayloadProperties {
 public:
  explicit PayloadProperties(const std::string& payload_path);
  PayloadProperties(const PayloadProperties&) = delete;
  PayloadProperties& operator=(const PayloadProperties&) = delete;

  ~PayloadProperties() = default;

  // Get the properties in a json format. The json file will be used in
  // autotests, cros flash, etc. Mainly in Chrome OS.
  bool GetPropertiesAsJson(std::string* json_str);

  // Get the properties of the payload as a key/value store. This is mainly used
  // in Android.
  bool GetPropertiesAsKeyValue(std::string* key_value_str);

 private:
  // Does the main job of reading the payload and extracting information from
  // it.
  bool LoadFromPayload();

  // The path to the payload file.
  std::string payload_path_;

  // The version of the metadata json format. If the output json file changes
  // format, this needs to be increased.
  int version_{2};

  int64_t metadata_size_;
  std::string metadata_hash_;
  std::string metadata_signatures_;

  int64_t payload_size_;
  std::string payload_hash_;

  // Whether the payload is a delta (true) or full (false).
  bool is_delta_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_GENERATOR_PAYLOAD_PROPERTIES_H_
