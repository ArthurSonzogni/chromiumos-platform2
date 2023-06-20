// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/xml/android_xml_util.h"

#include <fstream>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "arc/setup/xml/android_binary_xml_tokenizer.h"

using base::StringPiece;

namespace arc {

namespace {

// Version element prefix in packages.xml and packages_cache.xml files.
constexpr char kElementVersion[] = "<version ";

// Fingerprint attribute prefix in packages.xml and packages_cache.xml files.
constexpr char kAttributeFingerprint[] = " fingerprint=\"";

// Helper function for extracting an attribute value from an XML line.
// Expects |key| to be suffixed with '=\"' (e.g. ' sdkVersion=\"').
StringPiece GetAttributeValue(const StringPiece& line, const StringPiece& key) {
  StringPiece::size_type key_begin_pos = line.find(key);
  if (key_begin_pos == StringPiece::npos)
    return StringPiece();
  StringPiece::size_type value_begin_pos = key_begin_pos + key.length();
  StringPiece::size_type value_end_pos = line.find('"', value_begin_pos);
  if (value_end_pos == StringPiece::npos)
    return StringPiece();
  return line.substr(value_begin_pos, value_end_pos - value_begin_pos);
}

}  // namespace

bool GetFingerprintAndSdkVersionFromPackagesXml(
    const base::FilePath& packages_xml_path,
    std::string* out_fingerprint,
    std::string* out_sdk_version) {
  if (USE_ARCVM) {
    // Newer version Android uses binary XML format.
    if (GetFingerprintAndSdkVersionFromBinaryPackagesXml(
            packages_xml_path, out_fingerprint, out_sdk_version)) {
      return true;
    }
    // Failure may mean that the file is a text XML.
    // TODO(hashimoto): Remove this fallback after switching to binary XML.
    LOG(INFO) << "Failed to interpret the file as a binary XML. "
              << "Going to read the file as a text XML.";
  }
  if (FindLine(packages_xml_path,
               base::BindRepeating(&FindFingerprintAndSdkVersion,
                                   out_fingerprint, out_sdk_version))) {
    return true;  // found it.
  }
  LOG(WARNING) << "No fingerprint found in " << packages_xml_path.value();
  return false;
}

bool GetFingerprintAndSdkVersionFromBinaryPackagesXml(
    const base::FilePath& packages_xml_path,
    std::string* out_fingerprint,
    std::string* out_sdk_version) {
  AndroidBinaryXmlTokenizer tokenizer;
  if (!tokenizer.Init(packages_xml_path)) {
    LOG(ERROR) << "Failed to initialize the tokenizer with file "
               << packages_xml_path.value();
    return false;
  }
  using Token = AndroidBinaryXmlTokenizer::Token;
  while (tokenizer.Next()) {
    // Try to find a tag whose name is "version".
    if (tokenizer.token() == Token::kStartTag &&
        tokenizer.name() == "version") {
      // Get attributes of the "version" tag.
      std::optional<int> sdk_version, database_version;
      std::string volume_uuid, fingerprint;
      while (tokenizer.Next() && tokenizer.token() == Token::kAttribute) {
        if (tokenizer.name() == "sdkVersion") {
          sdk_version = tokenizer.int_value();
        } else if (tokenizer.name() == "databaseVersion") {
          database_version = tokenizer.int_value();
        } else if (tokenizer.name() == "volumeUuid") {
          volume_uuid = tokenizer.string_value();
        } else if (tokenizer.name() == "fingerprint") {
          fingerprint = tokenizer.string_value();
        }
      }
      // If volume_uuid is not empty, it's for an external storage and
      // should be ignored.
      if (volume_uuid.empty() && !fingerprint.empty() &&
          sdk_version.has_value() && database_version.has_value()) {
        *out_fingerprint = fingerprint;
        *out_sdk_version = base::NumberToString(*sdk_version);
        return true;
      }
    }
  }
  return false;
}

bool FindLine(
    const base::FilePath& file_path,
    const base::RepeatingCallback<bool(const std::string&)>& callback) {
  // Do exactly the same stream handling as TextContentsEqual() in
  // base/files/file_util.cc which is known to work.
  std::ifstream file(file_path.value().c_str(), std::ios::in);
  if (!file.is_open()) {
    PLOG(WARNING) << "Cannot open " << file_path.value();
    return false;
  }

  do {
    std::string line;
    std::getline(file, line);

    // Check for any error state.
    if (file.bad()) {
      PLOG(WARNING) << "Failed to read " << file_path.value();
      return false;
    }

    // Trim all '\r' and '\n' characters from the end of the line.
    std::string::size_type end = line.find_last_not_of("\r\n");
    if (end == std::string::npos)
      line.clear();
    else if (end + 1 < line.length())
      line.erase(end + 1);

    // Stop reading the file if |callback| returns true.
    if (callback.Run(line))
      return true;
  } while (!file.eof());

  // |callback| didn't find anything in the file.
  return false;
}

bool FindFingerprintAndSdkVersion(std::string* out_fingerprint,
                                  std::string* out_sdk_version,
                                  const std::string& line) {
  constexpr char kAttributeVolumeUuid[] = " volumeUuid=\"";
  constexpr char kAttributeSdkVersion[] = " sdkVersion=\"";
  constexpr char kAttributeDatabaseVersion[] = " databaseVersion=\"";

  // Parsing an XML this way is not very clean but in this case, it works (and
  // fast.) Android's packages.xml is written in com.android.server.pm.Settings'
  // writeLPr(), and the write function always uses Android's FastXmlSerializer.
  // The serializer does not try to pretty-print the XML, and inserts '\n' only
  // to certain places like endTag.
  StringPiece trimmed = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
  if (!base::StartsWith(trimmed, kElementVersion, base::CompareCase::SENSITIVE))
    return false;  // Not a <version> element. Ignoring.

  if (trimmed.find(kAttributeVolumeUuid) != std::string::npos)
    return false;  // This is for an external storage. Ignoring.

  StringPiece fingerprint = GetAttributeValue(trimmed, kAttributeFingerprint);
  if (fingerprint.empty()) {
    LOG(WARNING) << "<version> doesn't have a valid fingerprint: " << trimmed;
    return false;
  }
  StringPiece sdk_version = GetAttributeValue(trimmed, kAttributeSdkVersion);
  if (sdk_version.empty()) {
    LOG(WARNING) << "<version> doesn't have a valid sdkVersion: " << trimmed;
    return false;
  }
  // Also checks existence of databaseVersion.
  if (GetAttributeValue(trimmed, kAttributeDatabaseVersion).empty()) {
    LOG(WARNING) << "<version> doesn't have a databaseVersion: " << trimmed;
    return false;
  }

  out_fingerprint->assign(fingerprint.data(), fingerprint.size());
  out_sdk_version->assign(sdk_version.data(), sdk_version.size());
  return true;
}

}  // namespace arc
