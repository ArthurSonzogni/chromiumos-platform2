// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_VALIDATOR_H_
#define LIBIPP_VALIDATOR_H_

#include <cstddef>

#include "frame.h"
#include "ipp_export.h"

namespace ipp {

// Maximum size of 'text' value (rfc8011, section 5.1.2).
constexpr size_t kMaxLengthOfText = 1023;

// Maximum size of 'name' value (rfc8011, section 5.1.3).
constexpr size_t kMaxLengthOfName = 255;

// Maximum size of 'keyword' value (rfc8011, section 5.1.4).
constexpr size_t kMaxLengthOfKeyword = 255;

// Maximum size of 'uri' value (rfc8011, section 5.1.6).
constexpr size_t kMaxLengthOfUri = 1023;

// Maximum size of 'uriScheme' value (rfc8011, section 5.1.7).
constexpr size_t kMaxLengthOfUriScheme = 63;

// Maximum size of 'charset' value (rfc8011, section 5.1.8).
constexpr size_t kMaxLengthOfCharset = 63;

// Maximum size of 'naturalLanguage' value (rfc8011, section 5.1.9).
constexpr size_t kMaxLengthOfNaturalLanguage = 63;

// Maximum size of 'mimeMediaType' value (rfc8011, section 5.1.10).
constexpr size_t kMaxLengthOfMimeMediaType = 255;

// Maximum size of 'octetString' value (rfc8011, section 5.1.11).
constexpr size_t kMaxLengthOfOctetString = 1023;

class ErrorsLog;

// Validates all groups in the `frame`. All detected errors are saved in `log`
// in the order they occur in the original frame. The function returns true <=>
// no errors were detected.
// For string types only the basic features are validated, there is no UTF-8
// parsing or type-specific parsing like URL or MIME types.
bool LIBIPP_EXPORT Validate(const Frame& frame, ErrorsLog& log);

}  // namespace ipp

#endif  //  LIBIPP_VALIDATOR_H_
