// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_VALIDATOR_H_
#define LIBIPP_VALIDATOR_H_

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "errors.h"
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

// Describes types of validation errors in a single value.
enum class ValidatorCode {
  // The string value is empty when it is not allowed.
  kStringEmpty = 0,
  // The string value is too long.
  kStringTooLong,
  // The string value is supposed to start with a lowercase letter and it
  // doesn't.
  kStringMustStartLowercaseLetter,
  // The string value contains invalid character.
  kStringInvalidCharacter,
  // The value of type textWithLanguage or nameWithLanguage has invalid language
  // part.
  kStringWithLangInvalidLanguage,
  // The dateTime value has invalid date.
  kDateTimeInvalidDate,
  // The dateTime value has invalid time of day.
  kDateTimeInvalidTimeOfDay,
  // The dateTime value has invalid timezone.
  kDateTimeInvalidZone,
  // The resolution value has invalid units.
  kResolutionInvalidUnit,
  // The resolution value has at least one invalid dimension.
  kResolutionInvalidDimension,
  // The rangeOfIntegers value has min threshold larger than max threshold.
  kRangeOfIntegerMaxLessMin,
  // The integer value is out of allowed range.
  kIntegerOutOfRange
};

// Represents information about invalid value or name of an attribute.
class LIBIPP_EXPORT AttrError {
 public:
  // `errors` contains validation errors of an attribute's name.
  explicit AttrError(std::set<ValidatorCode>&& errors)
      : index_(0xffffu), errors_(errors) {}
  // `errors` contains validation errors for the value at `index`.
  AttrError(uint16_t index, std::set<ValidatorCode>&& errors)
      : index_(index), errors_(errors) {}
  // Returns true if it is about the attribute's name.
  bool IsInTheName() const { return Index() == 0xffffu; }
  // Returns the index of the incorrect value or 0xffff if it is about
  // the attribute's name.
  uint16_t Index() const { return index_; }
  // Returns errors as SetOfErrors.
  const std::set<ValidatorCode> Errors() const { return errors_; }
  // Returns errors as vector. Codes in the vector are sorted and unique.
  std::vector<ValidatorCode> ErrorsAsVector() const;

 private:
  uint16_t index_;
  std::set<ValidatorCode> errors_;
};

// The interface of errors log.
class ValidatorLog {
 public:
  ValidatorLog() = default;
  ValidatorLog(const ValidatorLog&) = delete;
  ValidatorLog& operator=(const ValidatorLog&) = delete;
  virtual ~ValidatorLog() = default;
  // Reports an `error` for the attribute at `path`. The errors are reported in
  // the same order as they occurred in the frame. Return false if you do not
  // want to get any more AddValidationError() calls.
  virtual bool AddValidationError(const AttrPath& path, AttrError error) = 0;
};

// Simple implementation of the ValidatorLog interface. It just saves the first
// `max_entries_count` (see the constructor) errors in the frame.
class LIBIPP_EXPORT SimpleValidatorLog : public ValidatorLog {
 public:
  struct Entry {
    AttrPath path;
    AttrError error;
    Entry(const AttrPath& path, AttrError error) : path(path), error(error) {}
  };
  explicit SimpleValidatorLog(size_t max_entries_count = 100)
      : max_entries_count_(max_entries_count) {}
  bool AddValidationError(const AttrPath& path, AttrError error) override;
  const std::vector<Entry>& Entries() const { return entries_; }

 private:
  const size_t max_entries_count_;
  std::vector<Entry> entries_;
};

// Validates all groups in the `frame`. All detected errors are saved in `log`
// in the order they occur in the original frame. The function returns true <=>
// no errors were detected.
// For string types only the basic features are validated, there is no UTF-8
// parsing or type-specific parsing like URL or MIME types.
bool LIBIPP_EXPORT Validate(const Frame& frame, ValidatorLog& log);

}  // namespace ipp

#endif  //  LIBIPP_VALIDATOR_H_
