// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIVACY_LOG_H_
#define PRIVACY_LOG_H_

#include <string>

#include <base/logging.h>
#include <brillo/brillo_export.h>

namespace privacy {

enum PIIType {
  NOT_SPECIFIED = 0,

  // In some cases, a field (either the name or the description) may appear
  // to contain sensitive info but actually doesn't. This type would be
  // helpful in cases where a tool may determine a field to be sensitive
  // but after manually reviewing the field, it may be helpful to add this
  // type to indicate that the field was reviewed and determined to be not
  // storing any sensitive info.
  NOT_REQUIRED = 1,

  // For pseudonymous data that forms a unique id but does not
  // identify the actual user or entity.  If you're unsure, you should use
  // IDENTIFYING_ID, not PSEUDONYMOUS_ID
  PSEUDONYMOUS_ID = 2,

  // IDENTIFYING_ID should be used to annotate the fields that contain
  // data that can identify a person or entity directly.
  // example: email address, phone number, username etc.
  IDENTIFYING_ID = 3,

  // The following types should be used only when the data has sensitive PII.
  // This includes fields with passwords, credit card numbers, govt id etc.
  SENSITIVE_PERSONAL_INFORMATION = 4,

  // NETWORK_ENDPOINT types define network endpoints such as IP addresses.
  NETWORK_ENDPOINT = 5,

  // HARDWARE_ID are Serial numbers identifying specific hardware devices,
  // such as IMEI or MAC addresses.
  HARDWARE_ID = 6,

  // Anonymous data points such as:
  //  - race, ethnicity
  //  - political affiliation
  ANONYMOUS_DATA = 7,

  // Used for any location data.
  LOCATION = 8,

  // User entered content.
  //
  // This is used for data that has been gathered incidentally, such as user
  // entered search query.
  USER_CONTENT = 10,

  // Third party data. For example, information sent by a publisher
  // to Google that may contain sensitive info.
  THIRD_PARTY_DATA = 11,

  // Security material.
  //
  // This should be used for fields that contain internal security material such
  // as cryptographic keys, nonces, and other entities that require special
  // handling
  SECURITY_MATERIAL = 12,
};
struct PrivacyMetadata {
  std::string value;
  PIIType piiType;
};

BRILLO_EXPORT std::ostream& operator<<(std::ostream& out,
                                       const privacy::PrivacyMetadata metadata);

}  // namespace privacy

#endif  // PRIVACY_LOG_H_
