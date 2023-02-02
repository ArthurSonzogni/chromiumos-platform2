// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_ERRORS_H_
#define LIBIPP_ERRORS_H_

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ipp_enums.h"
#include "ipp_export.h"

namespace ipp {

// Describes types of validation errors in a single value.
enum class ValidationCode {
  // The string value is empty when it is not allowed.
  kStringEmpty = 0,
  // The string value is too long.
  kStringTooLong,
  // The string value is supposed to start from a lowercase letter and it
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
class IPP_EXPORT AttrError {
 public:
  // `errors` contains validation errors of an attribute's name.
  explicit AttrError(std::set<ValidationCode>&& errors)
      : index_(0xffffu), errors_(errors) {}
  // `errors` contains validation errors for the value at `index`.
  AttrError(uint16_t index, std::set<ValidationCode>&& errors)
      : index_(index), errors_(errors) {}
  // Returns true if it is about the attribute's name.
  bool IsInTheName() const { return Index() == 0xffffu; }
  // Returns the index of the incorrect value or 0xffff if it is about
  // the attribute's name.
  uint16_t Index() const { return index_; }
  // Returns errors as SetOfErrors.
  const std::set<ValidationCode> Errors() const { return errors_; }
  // Returns errors as vector. Codes in the vector are sorted and unique.
  std::vector<ValidationCode> ErrorsAsVector() const;

 private:
  uint16_t index_;
  std::set<ValidationCode> errors_;
};

// Describes location of the attribute in the frame and the attribute's name.
class IPP_EXPORT AttrPath {
 public:
  struct Segment {
    uint16_t collection_index;
    std::string attribute_name;
  };
  // Invalid value of GroupTag used to represent location in a frame's header.
  static constexpr GroupTag kHeader = static_cast<GroupTag>(0);
  explicit AttrPath(GroupTag group) : group_(group) {}
  // Returns a string representation of the attribute's locations.
  std::string AsString() const;
  // Adds a new segment at the end of attribute's path. Converts the attribute's
  // path to the path to one of its sub-attributes.
  void PushBack(uint16_t collection_index, std::string_view attribute_name) {
    path_.emplace_back(Segment{collection_index, std::string(attribute_name)});
  }
  // Removes the last segment from the attribute's path. Converts the
  // attribute's path to the path to its parent attribute.
  void PopBack() { path_.pop_back(); }
  // Returns reference to the last element.
  Segment& Back() { return path_.back(); }

 private:
  GroupTag group_;
  std::vector<Segment> path_;
};

// The interface of errors log.
class ErrorsLog {
 public:
  ErrorsLog() = default;
  ErrorsLog(const ErrorsLog&) = delete;
  ErrorsLog& operator=(const ErrorsLog&) = delete;
  virtual ~ErrorsLog() = default;
  // Reports an `error` for the attribute at `path`. The errors are reported in
  // the same order as they occurred in the frame. Return false if do not want
  // to get any more AddValidationError() calls.
  virtual bool AddValidationError(const AttrPath& path, AttrError error) = 0;
};

// Simple implementation of the ErrorsLog interface. It just saves the first
// `max_entries_count` (see the constructor) errors in the frame.
class IPP_EXPORT SimpleLog : public ErrorsLog {
 public:
  struct Entry {
    AttrPath path;
    AttrError error;
    Entry(const AttrPath& path, AttrError error) : path(path), error(error) {}
  };
  explicit SimpleLog(size_t max_entries_count = 100)
      : max_entries_count_(max_entries_count) {}
  bool AddValidationError(const AttrPath& path, AttrError error) override;
  const std::vector<Entry>& Entries() const { return entries_; }

 private:
  const size_t max_entries_count_;
  std::vector<Entry> entries_;
};

}  // namespace ipp

#endif  //  LIBIPP_ERRORS_H_
