// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_PARSER_H_
#define LIBIPP_PARSER_H_

#include <cstdint>
#include <vector>

#include "errors.h"
#include "frame.h"

namespace ipp {

// The errors spotted by the parser. Comments next to the values describe
// actions taken by the parser.
enum class ParserCode : uint8_t {
  kOK = 0,
  kAttributeNameIsEmpty,              // the parser stopped
  kValueMismatchTagConverted,         // the value was converted
  kValueMismatchTagOmitted,           // the value was omitted
  kAttributeNoValues,                 // the attribute was omitted
  kAttributeNameConflict,             // the attribute was omitted
  kBooleanValueOutOfRange,            // the boolean value was set to 1
  kValueInvalidSize,                  // the value was omitted
  kErrorWhenAddingAttribute,          // the attribute was omitted
  kOutOfBandAttributeWithManyValues,  // additional values were ignored
  kOutOfBandValueWithNonEmptyData,    // the data field was ignored
  kUnexpectedEndOfFrame,              // the parser stopped
  kGroupTagWasExpected,               // the parser stopped
  kEmptyNameExpectedInTNV,            // the parser stopped
  kEmptyValueExpectedInTNV,           // the parser stopped
  kNegativeNameLengthInTNV,           // the parser stopped
  kNegativeValueLengthInTNV,          // the parser stopped
  kTNVWithUnexpectedValueTag,         // the parser stopped
  kUnsupportedValueTag,               // the value was omitted
  kUnexpectedEndOfGroup,              // the parser stopped
  kLimitOnCollectionsLevelExceeded,   // the parser stopped
  kLimitOnGroupsCountExceeded,        // the parser stopped
  kErrorWhenAddingGroup               // the group was omitted
};

// The interface of parser log.
class ParserLog {
 public:
  ParserLog() = default;
  ParserLog(const ParserLog&) = delete;
  ParserLog& operator=(const ParserLog&) = delete;
  virtual ~ParserLog() = default;
  // Reports an `error` when parsing an element pointed by `path`. `critical`
  // set to true means that the parser cannot continue and will stop parsing
  // before reaching the end of input frame. `critical` == true DOES NOT mean
  // that this call is the last one. Also, there may be more than one call
  // with `critical` == true during single parser run.
  virtual void AddParserError(const AttrPath& path,
                              ParserCode error,
                              bool critical) = 0;
};

// Simple implementation of the ParserLog interface. It just saves the first
// `max_entries_count` (see the constructor) parser errors.
class IPP_EXPORT SimpleParserLog : public ParserLog {
 public:
  struct Entry {
    AttrPath path;
    ParserCode error;
    Entry(const AttrPath& path, ParserCode error) : path(path), error(error) {}
  };
  explicit SimpleParserLog(size_t max_entries_count = 100)
      : max_entries_count_(max_entries_count) {}
  void AddParserError(const AttrPath& path,
                      ParserCode error,
                      bool critical) override;

  // Returns all errors added by AddParserError() in the same order they were
  // added. The log is truncated <=> the number of entries reached the value
  // `max_entries_count` passed to the constructor.
  const std::vector<Entry>& Errors() const { return errors_; }
  // Returns all critical errors added by AddParserError() in the same order
  // they were added. The log is not truncated, but there is no more than a
  // couple of critical errors in a single parser run. All critical errors are
  // also included in Errors() (if it doesn't reach the limit).
  const std::vector<Entry>& CriticalErrors() const { return critical_errors_; }

 private:
  const size_t max_entries_count_;
  std::vector<Entry> errors_;
  std::vector<Entry> critical_errors_;
};

}  // namespace ipp

#endif  //  LIBIPP_PARSER_H_
