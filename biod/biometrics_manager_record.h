// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOMETRICS_MANAGER_RECORD_H_
#define BIOD_BIOMETRICS_MANAGER_RECORD_H_

#include <string>
#include <vector>

#include <base/base64.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace biod {

// Represents a record previously registered with this BiometricsManager in an
// EnrollSession. These objects can be retrieved with GetRecords.
class BiometricsManagerRecord {
 public:
  virtual ~BiometricsManagerRecord() = default;
  virtual const std::string& GetId() const = 0;
  virtual const std::string& GetUserId() const = 0;
  virtual const std::string& GetLabel() const = 0;
  virtual const std::vector<uint8_t>& GetValidationVal() const = 0;

  // Returns true on success.
  virtual bool SetLabel(std::string label) = 0;

  // Returns true on success.
  virtual bool Remove() = 0;

  virtual const std::string GetValidationValBase64() const {
    const auto& validation_val_bytes = GetValidationVal();
    std::string validation_val(validation_val_bytes.begin(),
                               validation_val_bytes.end());
    base::Base64Encode(validation_val, &validation_val);
    return validation_val;
  }

  virtual bool IsValidUTF8() const {
    if (!base::IsStringUTF8(GetLabel())) {
      LOG(ERROR) << "Label is not valid UTF8";
      return false;
    }

    if (!base::IsStringUTF8(GetId())) {
      LOG(ERROR) << "Record ID is not valid UTF8";
      return false;
    }

    if (!base::IsStringUTF8(GetValidationValBase64())) {
      LOG(ERROR) << "Validation value is not valid UTF8";
      return false;
    }

    if (!base::IsStringUTF8(GetUserId())) {
      LOG(ERROR) << "User ID is not valid UTF8";
      return false;
    }

    return true;
  }
};

}  //  namespace biod

#endif  // BIOD_BIOMETRICS_MANAGER_RECORD_H_
