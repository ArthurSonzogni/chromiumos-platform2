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

#include "update_engine/cros/omaha_parser_xml.h"

#include <map>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

using std::map;
using std::string;

namespace chromeos_update_engine {

bool OmahaParserXml::Parse(ErrorCode* error_code) {
  *error_code = ErrorCode::kSuccess;

  xml_parser_ = XML_ParserCreate(nullptr);
  XML_SetUserData(xml_parser_, this);
  XML_SetElementHandler(xml_parser_, &OmahaParserXml::ParserHandlerStart,
                        OmahaParserXml::ParserHandlerEnd);
  XML_SetEntityDeclHandler(xml_parser_,
                           OmahaParserXml::ParserHandlerEntityDecl);
  XML_Status res = XML_Parse(xml_parser_, buffer_, size_, XML_TRUE);

  if (res != XML_STATUS_OK || failed_) {
    LOG(ERROR) << "Omaha response not valid XML: "
               << XML_ErrorString(XML_GetErrorCode(xml_parser_)) << " at line "
               << XML_GetCurrentLineNumber(xml_parser_) << " col "
               << XML_GetCurrentColumnNumber(xml_parser_);
    *error_code = ErrorCode::kOmahaRequestXMLParseError;
    if (size_ == 0) {
      *error_code = ErrorCode::kOmahaRequestEmptyResponseError;
    } else if (entity_decl_) {
      *error_code = ErrorCode::kOmahaRequestXMLHasEntityDecl;
    }
  }
  XML_ParserFree(xml_parser_);
  return *error_code == ErrorCode::kSuccess;
}

// static
void OmahaParserXml::ParserHandlerStart(void* user_data,
                                        const XML_Char* element,
                                        const XML_Char** attr) {
  OmahaParserXml* parser = reinterpret_cast<OmahaParserXml*>(user_data);
  if (parser->failed_)
    return;

  parser->current_path_ += string("/") + element;

  map<string, string> attrs;
  if (attr != nullptr) {
    for (int n = 0; attr[n] != nullptr && attr[n + 1] != nullptr; n += 2) {
      string key = attr[n];
      string value = attr[n + 1];
      attrs[key] = value;
    }
  }

  OmahaParserData* data = parser->data_;
  if (parser->current_path_ == "/response/daystart") {
    data->daystart = {
        .elapsed_days = attrs[kAttrElapsedDays],
        .elapsed_seconds = attrs[kAttrElapsedSeconds],
    };
  } else if (parser->current_path_ == "/response/app") {
    data->apps.push_back({.id = attrs[kAttrAppId]});
    if (attrs.find(kAttrCohort) != attrs.end())
      data->apps.back().cohort = attrs[kAttrCohort];
    if (attrs.find(kAttrCohortHint) != attrs.end())
      data->apps.back().cohorthint = attrs[kAttrCohortHint];
    if (attrs.find(kAttrCohortName) != attrs.end())
      data->apps.back().cohortname = attrs[kAttrCohortName];
  } else if (parser->current_path_ == "/response/app/updatecheck") {
    data->apps.back().updatecheck = {
        .status = attrs[kAttrStatus],
        .poll_interval = attrs[kAttrPollInterval],
        .eol_date = attrs[kAttrEolDate],
        .rollback = attrs[kAttrRollback],
        .firmware_version = attrs[kAttrFirmwareVersion],
        .kernel_version = attrs[kAttrKernelVersion],
        .past_firmware_version =
            attrs[base::StringPrintf("%s_%i", kAttrFirmwareVersion,
                                     parser->rollback_allowed_milestones_)],
        .past_kernel_version = attrs[base::StringPrintf(
            "%s_%i", kAttrKernelVersion, parser->rollback_allowed_milestones_)],
        .disable_market_segment = attrs[kAttrDisableMarketSegment],
        .invalidate_last_update = attrs[kAttrInvalidateLastUpdate],
        .no_update_reason = attrs[kAttrNoUpdateReason],
    };
  } else if (parser->current_path_ == "/response/app/updatecheck/urls/url") {
    data->apps.back().urls.push_back({.codebase = attrs[kAttrCodeBase]});
  } else if (parser->current_path_ ==
             "/response/app/updatecheck/manifest/packages/package") {
    data->apps.back().packages.push_back({
        .name = attrs[kAttrName],
        .size = attrs[kAttrSize],
        .hash = attrs[kAttrHashSha256],
        .fp = attrs[kAttrFp],
    });
  } else if (parser->current_path_ == "/response/app/updatecheck/manifest") {
    data->apps.back().manifest.version = attrs[kAttrVersion];
  } else if (parser->current_path_ ==
             "/response/app/updatecheck/manifest/actions/action") {
    // We only care about the postinstall action.
    if (attrs[kAttrEvent] == kValPostInstall) {
      OmahaParserData::App::PostInstallAction action = {
          .is_delta_payloads =
              base::SplitString(attrs[kAttrIsDeltaPayload], ":",
                                base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL),
          .metadata_signature_rsas =
              base::SplitString(attrs[kAttrMetadataSignatureRsa], ":",
                                base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL),
          .metadata_sizes =
              base::SplitString(attrs[kAttrMetadataSize], ":",
                                base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL),
          .max_days_to_scatter = attrs[kAttrMaxDaysToScatter],
          .no_update = attrs[kAttrNoUpdate],
          .more_info_url = attrs[kAttrMoreInfo],
          .prompt = attrs[kAttrPrompt],
          .deadline = attrs[kAttrDeadline],
          .disable_p2p_for_downloading = attrs[kAttrDisableP2PForDownloading],
          .disable_p2p_for_sharing = attrs[kAttrDisableP2PForSharing],
          .public_key_rsa = attrs[kAttrPublicKeyRsa],
          .max_failure_count_per_url = attrs[kAttrMaxFailureCountPerUrl],
          .disable_payload_backoff = attrs[kAttrDisablePayloadBackoff],
          .powerwash_required = attrs[kAttrPowerwash],
          .disable_hash_checks = attrs[kAttrDisableHashChecks],
          .disable_repeated_updates = attrs[kAttrDisableRepeatedUpdates],
      };
      data->apps.back().postinstall_action = std::move(action);
    }
  }
}

// static
void OmahaParserXml::ParserHandlerEnd(void* user_data,
                                      const XML_Char* element) {
  OmahaParserXml* parser = reinterpret_cast<OmahaParserXml*>(user_data);
  if (parser->failed_)
    return;

  const string path_suffix = string("/") + element;

  if (!base::EndsWith(parser->current_path_, path_suffix,
                      base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Unexpected end element '" << element
               << "' with current_path_='" << parser->current_path_ << "'";
    parser->failed_ = true;
    return;
  }
  parser->current_path_.resize(parser->current_path_.size() -
                               path_suffix.size());
}

// static
void OmahaParserXml::ParserHandlerEntityDecl(void* user_data,
                                             const XML_Char* entity_name,
                                             int is_parameter_entity,
                                             const XML_Char* value,
                                             int value_length,
                                             const XML_Char* base,
                                             const XML_Char* system_id,
                                             const XML_Char* public_id,
                                             const XML_Char* notation_name) {
  LOG(ERROR) << "XML entities are not supported. Aborting parsing.";
  OmahaParserXml* parser = reinterpret_cast<OmahaParserXml*>(user_data);
  parser->failed_ = true;
  parser->entity_decl_ = true;
  XML_StopParser(parser->xml_parser_, false);
}

}  // namespace chromeos_update_engine
