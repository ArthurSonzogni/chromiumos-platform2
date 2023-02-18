// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_FRAME_H_
#define LIBIPP_FRAME_H_

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "attribute.h"
#include "colls_view.h"
#include "ipp_enums.h"
#include "ipp_export.h"
#include "ipp_log.h"

namespace ipp {

// represents operation id
typedef E_operations_supported Operation;

// represents status-code [rfc8010]
typedef E_status_code Status;

// represents the version of IPP protocol
// The first byte (MSB) of this value correspond to major version number, the
// second one to minor version number.
typedef E_ipp_versions_supported Version;

// There are methods that return error codes. The error code is usually
// returned via the last method's parameter that is optional.
// This enum contains possible values of error codes.
enum class Code {
  kOK,                 // success (no errors)
  kDataTooLong,        // the payload of the frame is too large
  kInvalidGroupTag,    // provided GroupTag is invalid
  kInvalidValueTag,    // provided ValueTag is invalid
  kIndexOutOfRange,    // parameter 'index' is wrong
  kTooManyGroups,      // reached the threshold
  kTooManyAttributes,  // reached the threshold
  kInvalidName,        // incorrect attribute name
  kNameConflict,       // attribute with this name already exists
  kIncompatibleType,   // conversion between C++ type and value is not supported
  kValueOutOfRange     // given C++ value is out of range (invalid)
};

// The correct values of GroupTag are 0x01, 0x02, 0x04-0x0f. This function
// checks if given GroupTag is valid.
constexpr bool IsValid(GroupTag tag) {
  if (tag > static_cast<GroupTag>(0x0f))
    return false;
  if (tag < static_cast<GroupTag>(0x01))
    return false;
  return (tag != static_cast<GroupTag>(0x03));
}
// This array contains all valid GroupTag values and may be used in loops like
//   for (GroupTag gt: kGroupTags) ...
constexpr std::array<GroupTag, 14> kGroupTags{
    static_cast<GroupTag>(0x01), static_cast<GroupTag>(0x02),
    static_cast<GroupTag>(0x04), static_cast<GroupTag>(0x05),
    static_cast<GroupTag>(0x06), static_cast<GroupTag>(0x07),
    static_cast<GroupTag>(0x08), static_cast<GroupTag>(0x09),
    static_cast<GroupTag>(0x0a), static_cast<GroupTag>(0x0b),
    static_cast<GroupTag>(0x0c), static_cast<GroupTag>(0x0d),
    static_cast<GroupTag>(0x0e), static_cast<GroupTag>(0x0f)};

// TODO(pawliczek) - move all limits to separate header, this one was moved here
//                   from ipp_parser.cc
// This parameters defines maximum number of attribute groups in single package.
constexpr size_t kMaxCountOfAttributeGroups = 20 * 1024;

struct ParsingResults {
  std::vector<Log> errors;
  bool whole_buffer_was_parsed;  // false <=> the parsing was not completed
};

class Collection;
class Attribute;

// This class represents an IPP frame (IPP request or IPP response). All
// pointers to Collection or Attribute returned by methods of this class
// point to internal objects. These objects are always owned by their Frame
// object and their lifetime does not exceed the lifetime of their owner.
class IPP_EXPORT Frame {
 public:
  // Constructor. Create an empty frame with all basic parameters set to 0.
  Frame();
  // Constructor. Create a frame and set basic parameters for IPP request.
  // If `set_localization_en_us` is true (default) the Group Operation
  // Attributes is added to the frame with two attributes:
  //   * "attributes-charset"="utf-8"
  //   * "attributes-natural-language"="en-us"
  // If `set_localization_en_us` equals false, you have to add the Group
  // Operation Attributes with these two attributes by hand since they are
  // required to be the first attributes in the frame (see section 4.1.4 from
  // RFC 8011).
  explicit Frame(Operation operation_id,
                 Version version_number = Version::_1_1,
                 int32_t request_id = 1,
                 bool set_localization_en_us = true);
  // Constructor. The same as the previous constructor but for IPP response.
  // There is no differences between frames created with this and the previous
  // constructor. IPP requests and IPP responses have the same structure.
  // Values of operation_id and status_code are saved in the same variable,
  // they are just casted to different enums with static_cast<>(). The parameter
  // `set_localization_en_us_and_status_message` works in similar way as the
  // parameter `set_localization_en_us` in the constructor above but also adds
  // the attribute "status-message" to the Group Operation Attributes. The value
  // of the attribute is set to a string representation of the `status_code`.
  // The attribute "status-message" is defined in the section 4.1.6.2 from
  // RFC 8011.
  explicit Frame(Status status_code,
                 Version version_number = Version::_1_1,
                 int32_t request_id = 1,
                 bool set_localization_en_us_and_status_message = true);
  // Constructor. Parse the frame of `size` bytes saved in `buffer`. If the
  // parameter `log` is not nullptr, it is overwritten with the list of errors
  // detected by the parser. The constructed object is always valid. In the
  // worst case scenario (nothing was parsed), the constructed object is empty
  // and has all basic parameters set to zeroes like after Frame() constructor.
  // In case of parsing errors, some groups or attributes from the input buffer
  // may be omitted. You should examine the ParsingResults structure to make
  // sure that the whole input frame was parsed.
  Frame(const uint8_t* buffer, size_t size, ParsingResults* log = nullptr);
  // These two constructors are deprecated. Use the constructors defined above.
  // TODO(b/249157310): remove these from the codebase.
  Frame(Version version_number,
        Operation operation_id,
        int32_t request_id = 1,
        bool set_charset = true);
  Frame(Version version_number,
        Status status_code,
        int32_t request_id = 1,
        bool set_charset = true);

  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  virtual ~Frame();

  // Return the size of the binary representation of the frame in bytes.
  // DEPRECATED: Use functions from builder.h instead.
  size_t GetLength() const;
  // Save the binary representation of the frame to the given buffer. Use
  // GetLength() method before calling this method to make sure that the given
  // buffer is large enough. The method returns the number of bytes written to
  // `buffer` or 0 when `buffer_length` is smaller than binary representation of
  // the frame.
  // DEPRECATED: Use functions from builder.h instead.
  size_t SaveToBuffer(uint8_t* buffer, size_t buffer_length) const;
  // Return the binary representation of the frame as a vector.
  // DEPRECATED: Use functions from builder.h instead.
  std::vector<uint8_t> SaveToBuffer() const;

  // Access IPP version number.
  Version& VersionNumber();
  Version VersionNumber() const;
  // Access operation id or status code. OperationId() and StatusCode() refer to
  // the same field, they just cast the same value to different enums. The field
  // is interpreted as operation id in IPP request and as status code in IPP
  // responses.
  int16_t& OperationIdOrStatusCode();
  int16_t OperationIdOrStatusCode() const;
  Operation OperationId() const;
  Status StatusCode() const;
  // Access request id.
  int32_t& RequestId();
  int32_t RequestId() const;
  // Access to payload (e.g.: document to print).
  const std::vector<uint8_t>& Data() const;
  // Remove the payload from the frame and return it.
  std::vector<uint8_t> TakeData();
  // Set the new payload in the frame. The returned value is one of:
  //  * Code::kOK
  //  * Code::kDataTooLong
  Code SetData(std::vector<uint8_t>&& data);

  // Provides access to groups with the given GroupTag. You can iterate over
  // these groups in the following way:
  //   for (Collection& coll: frame.Groups(GroupTag::job_attributes)) {
  //     ...
  //   }
  // or
  //   for (size_t i = 0; i < frame.Groups(GroupTag::job_attributes).size(); ) {
  //     Collection& coll = frame.Groups(GroupTag::job_attributes)[i++];
  //     ...
  //   }
  // The groups are in the same order as they occur in the frame.
  CollectionsView Groups(GroupTag tag);
  ConstCollectionsView Groups(GroupTag tag) const;

  // Return all groups of attributes in the frame in the order they were added.
  // The returned vector never contains nullptr values.
  std::vector<std::pair<GroupTag, Collection*>> GetGroups();
  std::vector<std::pair<GroupTag, const Collection*>> GetGroups() const;

  // DEPRECATED. Use Groups(tag) instead (see above).
  // Return all groups of attributes in the frame with given Group Tag. The
  // returned vector never contains nullptr values. If the given GroupTag is
  // invalid or there is no groups with given `tag` in the frame an empty vector
  // is returned.
  std::vector<Collection*> GetGroups(GroupTag tag);
  std::vector<const Collection*> GetGroups(GroupTag tag) const;

  // DEPRECATED. Use Groups(tag)[index] instead (see above).
  // Return a group of attributes with given Group Tag at position `index`.
  // The position is always the same as in the corresponding vector returned
  // by the GetGroups() method. Returns nullptr if the frame does not have such
  // group of attributes (i.e.: `tag` is invalid or `index` is out of range).
  Collection* GetGroup(GroupTag tag, size_t index = 0);
  const Collection* GetGroup(GroupTag tag, size_t index = 0) const;

  // Add a new group of attributes to the frame. The pointer to the new group
  // (Collection*) is saved in `new_group` if `new_group` != nullptr.
  // The returned value is one of the following:
  //  * Code::kOK
  //  * Code::kInvalidGroupTag
  //  * Code::kTooManyGroups
  // If the returned value != Code::kOK then `new_group` is not modified.
  Code AddGroup(GroupTag tag, Collection** new_group = nullptr);

 private:
  Version version_;
  int16_t operation_id_or_status_code_;
  int32_t request_id_;
  // Groups stored in the order they appear in the binary representation.
  std::vector<std::pair<GroupTag, Collection*>> groups_;
  // Content of `group_` sorted by GroupTag. The largest valid value of GroupTag
  // is 0x0f (see kGroupTags above).
  std::array<std::vector<Collection*>, 16> groups_by_tag_;
  std::vector<uint8_t> data_;
};

}  // namespace ipp

#endif  //  LIBIPP_FRAME_H_
