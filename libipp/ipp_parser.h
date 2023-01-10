// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_PARSER_H_
#define LIBIPP_IPP_PARSER_H_

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ipp_attribute.h"
#include "ipp_frame.h"
#include "ipp_log.h"

namespace ipp {

// forward declarations
class Collection;
class Frame;
struct RawAttribute;
struct RawCollection;

// The errors spotted by the parser. Comments next to the values describe
// actions taken by the parser.
enum class ParserCode : uint8_t {
  kOK = 0,
  kAttributeNameIsEmpty,             // the parser stopped
  kValueMismatchTagConverted,        // the value was converted
  kValueMismatchTagOmitted,          // the value was omitted
  kAttributeNoValues,                // the attribute was omitted
  kAttributeNameConflict,            // the attribute was omitted
  kBooleanValueOutOfRange,           // the boolean value was set to 1
  kValueInvalidSize,                 // the value was omitted
  kErrorWhenAddingAttribute,         // the attribute was omitted
  kOutOfBandAttributeWithManyValues  // additional values were ignored
};

std::string_view ToStrView(ParserCode code);

class Parser {
 public:
  // Constructor, both parameters must not be nullptr. |frame| is used as
  // internal buffer to store intermediate form of parsed data. All spotted
  // issues are logged to |log| (by push_back()).
  Parser(FrameData* frame, std::vector<Log>* log)
      : frame_(frame), errors_(log) {}

  // Parses data from given buffer and saves it to internal buffer.
  bool ReadFrameFromBuffer(const uint8_t* ptr, const uint8_t* const buf_end);

  // Interpret the content from internal buffer and store it in |package|.
  // If |log_unknown_values| is set then all attributes unknown in |package|
  // are reported to the log.
  bool SaveFrameToPackage(bool log_unknown_values, Frame* package);

  // Resets the state of the object to initial state. It does not modify
  // objects provided in the constructor (|frame| and |log|).
  void ResetContent();

 private:
  // Copy/move/assign constructors/operators are forbidden.
  Parser(const Parser&) = delete;
  Parser(Parser&&) = delete;
  Parser& operator=(const Parser&) = delete;
  Parser& operator=(Parser&&) = delete;

  // Methods for adding entries to the log.
  void LogScannerError(const std::string& message, const uint8_t* position);
  void LogParserError(std::string_view message);
  void LogParserError(ParserCode error_code);
  void LogParserErrors(const std::vector<ParserCode>& errors);
  void LogParserWarning(const std::string& message);

  // Reads single tag_name_value from the buffer (ptr is updated) and append it
  // to the parameter. Returns false <=> error occurred.
  bool ReadTNVsFromBuffer(const uint8_t** ptr,
                          const uint8_t* const end_buf,
                          std::list<TagNameValue>*);

  // Parser helpers.
  bool ParseRawValue(int coll_level,
                     const TagNameValue& tnv,
                     std::list<TagNameValue>* data_chunks,
                     RawAttribute* attr);
  bool ParseRawCollection(int coll_level,
                          std::list<TagNameValue>* data_chunks,
                          RawCollection* coll);
  bool ParseRawGroup(std::list<TagNameValue>* data_chunks, RawCollection* coll);
  void DecodeCollection(RawCollection* raw, Collection* coll);

  // Internal buffer.
  FrameData* frame_;

  // Internal log: all errors & warnings are logged here.
  std::vector<Log>* errors_;

  // Temporary copies of begin/end pointers of the current buffer.
  const uint8_t* buffer_begin_ = nullptr;
  const uint8_t* buffer_end_ = nullptr;

  // This is a path to group/attribute being processed currently by the parser.
  // Each segment contains a name and a flag (true/false). When the  flag is
  // set to true then notices about direct offspring unknown attributes are
  // added to the log with LogParserNewElement(...) method. When false, this
  // method has no effect for offspring attributes.
  std::vector<std::pair<std::string, bool>> parser_context_;
};

}  // namespace ipp

#endif  //  LIBIPP_IPP_PARSER_H_
