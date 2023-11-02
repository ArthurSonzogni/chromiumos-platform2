//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <string>

#include <expat.h>

#include "update_engine/common/error_code.h"
#include "update_engine/cros/omaha_parser_data.h"

namespace chromeos_update_engine {

// Struct used for holding data obtained when parsing the XML.
class OmahaParserXml {
 public:
  OmahaParserXml(OmahaParserData* data,
                 const char* buffer,
                 int size,
                 int rollback_allowed_milestones)
      : data_(data),
        buffer_(buffer),
        size_(size),
        rollback_allowed_milestones_(rollback_allowed_milestones) {}
  virtual ~OmahaParserXml() = default;

  bool Parse(ErrorCode* error_code);

 private:
  // Callback functions invoked by expat.
  static void ParserHandlerStart(void* user_data,
                                 const XML_Char* element,
                                 const XML_Char** attr);
  static void ParserHandlerEnd(void* user_data, const XML_Char* element);
  // This is called for entity declarations. Since Omaha is guaranteed to never
  // return any XML with entities our course of action is to just stop
  // parsing. This avoids potential resource exhaustion problems AKA the
  // "billion laughs". CVE-2013-0340.
  static void ParserHandlerEntityDecl(void* user_data,
                                      const XML_Char* entity_name,
                                      int is_parameter_entity,
                                      const XML_Char* value,
                                      int value_length,
                                      const XML_Char* base,
                                      const XML_Char* system_id,
                                      const XML_Char* public_id,
                                      const XML_Char* notation_name);

  // The pointer to the data object to be filled out when parsing.
  OmahaParserData* data_;

  // The input buffer that needs to be parsed.
  const char* buffer_;
  int size_;

  int rollback_allowed_milestones_;

  XML_Parser xml_parser_;

  bool failed_{false};
  bool entity_decl_{false};
  std::string current_path_;
};

}  // namespace chromeos_update_engine
