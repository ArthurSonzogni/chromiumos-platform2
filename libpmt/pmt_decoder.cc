// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libpmt/pmt_decoder.h"

#include <algorithm>
#include <cstring>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/to_string.h>
#include <re2/re2.h>

#include "bits/pmt_data_interface.h"
#include "bits/pmt_metadata.h"
#include "libpmt/bits/pmt_data.pb.h"
#include "libpmt/pmt_impl.h"
#include "libpmt/xml_helper.h"

namespace pmt {

namespace {

// Shortcut to keep parsing error logs consistent.
#define LOG_ERROR_AND_RETURN(result, msg) \
  {                                       \
    LOG(ERROR) << msg << ".";             \
    return result;                        \
  }                                       \
  while (0)

#define PMT_XML_ERROR_EXIT(msg) \
  LOG_ERROR_AND_RETURN(result,  \
                       "Failed to parse PMT metadata mapping file: " << msg)

#define AGG_XML_ERROR_EXIT(msg) \
  LOG_ERROR_AND_RETURN(-EBADF, "Failed to parse PMT metadata file: " << msg)

// Node names used in metadata file parsing.
constexpr char kAttrGuid[] = "guid";
constexpr char kAttrTransformId[] = "transformID";

// XPaths used in metadata file parsing.
constexpr char kXPathMappings[] = "/pmt/mappings/mapping";
constexpr char kXPathBaseDir[] = "./xmlset/basedir";
constexpr char kXPathAggregatorFile[] = "./xmlset/aggregator";
constexpr char kXPathAggregatorInterfaceFile[] = "./xmlset/aggregatorinterface";
constexpr char kXPathTransforms[] =
    "/TELI:AggregatorInterface/cmn:TransFormations/cmn:TransFormation";
constexpr char kXPathTransformType[] = "./cmn:output_dataclass";
constexpr char kXPathSamples[] =
    "/TELEM:Aggregator/TELEM:SampleGroup/TELC:sample";
constexpr char kXPathLsb[] = "./TELC:lsb";
constexpr char kXPathMsb[] = "./TELC:msb";
constexpr char kXPathDescription[] = "./TELC:description";
constexpr char kXPathSubgroup[] = "./TELC:sampleSubGroup";
constexpr char kXPathTransformRef[] = "./TELI:transformREF";

// XML namespaces present within metadata files.
constexpr char kXsiNs[] = "xsi";
constexpr char kXiNs[] = "xi";
constexpr char kTELCNs[] = "TELC";
constexpr char kTELEMNs[] = "TELEM";
constexpr char kTELINs[] = "TELI";
// Libxml2 does not support nodes with namespace but no prefix in XPath search.
// Therefore an artificial prefix is chosen.
constexpr char kCommonNs[] = "cmn";

constexpr char kXsiNsUri[] = "http://www.w3.org/2001/XMLSchema-instance";
constexpr char kTELCNsUri[] = "http://schemas.intel.com/telemetry/base/common";
constexpr char kTELEMNsUri[] =
    "http://schemas.intel.com/telemetry/base/aggregator";
constexpr char kXiNsUri[] = "http://www.w3.org/2001/XInclude";
constexpr char kTELINsUri[] =
    "http://schemas.intel.com/telemetry/interface/aggregatorinterface";
constexpr char kCommonNsUri[] =
    "http://schemas.intel.com/telemetry/base/common";

// Regular expression to detect fields to skip.
constexpr char kRsvdRegExp[] = "reserved|rsvd|spare";

// Sample names which require special handling.
constexpr char kSamplePkgcBlockRefcnt[] = "PACKAGE_CSTATE_BLOCK_REFCNT";
constexpr char kSamplePkgcWakeRefcnt[] = "PACKAGE_CSTATE_WAKE_REFCNT";

// Extract an [msb, lsb] unsigned field from an 8B word.
constexpr uint64_t GetField(uint64_t v, unsigned int msb, unsigned int lsb) {
  return (v >> lsb) & ((uint64_t{1} << (msb - lsb + 1)) - 1);
}

}  // namespace

// Shorten calls to frequently used symbols.
using std::string, std::unordered_map, std::vector, std::hex;

PmtDecoder::PmtDecoder() : intf_(new PmtSysfsData()) {}

PmtDecoder::~PmtDecoder() {
  CleanUpDecoding();
}

PmtDecoder::PmtDecoder(std::unique_ptr<PmtDataInterface> intf)
    : intf_(std::move(intf)) {}

unordered_map<Guid, struct PmtDecoder::MetadataFilePaths>
PmtDecoder::FindMetadata() {
  unordered_map<Guid, struct MetadataFilePaths> result;

  base::FilePath meta_path;
  meta_path = intf_->GetMetadataMappingsFile();
  if (meta_path.empty())
    PMT_XML_ERROR_EXIT("pmt.xml is missing");

  xml::XmlParser parser;
  int parse_result = parser.ParseFile(meta_path);
  if (parse_result != 0)
    PMT_XML_ERROR_EXIT("Failed to parse " << meta_path << ": "
                                          << strerror(parse_result));

  xml::ScopedXmlXPathObject mappings_match = parser.XPathEval(kXPathMappings);
  if (!mappings_match || !mappings_match->nodesetval ||
      !mappings_match->nodesetval->nodeTab)
    PMT_XML_ERROR_EXIT("failed to find " << kXPathMappings);

  xmlNodeSetPtr mappings = mappings_match->nodesetval;

  // For each mapping, read its GUID and basedir. If basedir doesn't exist, skip
  // it. Otherwise find the aggregator and aggregator interface files ensure
  // they exist.
  for (size_t i = 0; i < mappings->nodeNr; i++) {
    MetadataFilePaths guid_paths;
    xmlNodePtr mapping = mappings->nodeTab[i];

    auto guid_str = parser.GetAttrValue(mapping, kAttrGuid);
    Guid guid;
    if (!guid_str || !base::HexStringToUInt(*guid_str, &guid))
      PMT_XML_ERROR_EXIT("could not decode GUID " << *guid_str);

    auto base_dir = parser.GetXPathNodeTextValue(mapping, kXPathBaseDir);
    if (!base_dir)
      PMT_XML_ERROR_EXIT("malformed <basedir>");

    base::FilePath base_dir_path(meta_path.DirName().Append(base_dir->data()));
    // If the path doesn't exist it simply means it's not supported so skip it.
    if (!base::DirectoryExists(base_dir_path))
      continue;

    auto agg_file = parser.GetXPathNodeTextValue(mapping, kXPathAggregatorFile);
    if (!agg_file)
      PMT_XML_ERROR_EXIT("malformed <aggregator>");
    guid_paths.aggregator_ = base_dir_path.Append(*agg_file);
    if (!base::PathExists(guid_paths.aggregator_))
      PMT_XML_ERROR_EXIT(guid_paths.aggregator_.value() << " doesn't exist");

    auto agg_intf =
        parser.GetXPathNodeTextValue(mapping, kXPathAggregatorInterfaceFile);
    if (!agg_intf)
      PMT_XML_ERROR_EXIT("malformed <aggregatorinterface>");
    guid_paths.aggregator_interface_ = base_dir_path.Append(*agg_intf);
    if (!base::PathExists(guid_paths.aggregator_interface_))
      PMT_XML_ERROR_EXIT(guid_paths.aggregator_interface_.value()
                         << " doesn't exist");

    result[guid] = guid_paths;
  }

  return result;
}

vector<Guid> PmtDecoder::DetectMetadata() {
  unordered_map<Guid, struct MetadataFilePaths> guid_map = FindMetadata();
  vector<Guid> result;
  for (const auto& kv : guid_map)
    result.push_back(kv.first);
  std::sort(result.begin(), result.end());
  return result;
}

int PmtDecoder::SetUpDecoding(const vector<Guid> guids) {
  // Sort by GUIDs. GUIDs need to be sorted because some transformations
  // are relying on data from other devices (see the 'pkgc_block_cause'
  // transformation).
  if (!ctx_.info_.empty())
    return -EBUSY;

  // Prepare RegExp for skipping reserved samples later.
  re2::RE2::Options opts;
  opts.set_case_sensitive(false);
  re2::RE2 samples_to_skip(kRsvdRegExp, opts);

  auto sorted_guids = guids;
  std::sort(sorted_guids.begin(), sorted_guids.end());
  auto supported_guids = FindMetadata();

  // 1st pass through guids to check if metadata is available for all.
  for (const auto& guid : guids) {
    if (!supported_guids.contains(guid)) {
      LOG(ERROR) << "GUID 0x" << hex << guid << " not supported";
      return -EINVAL;
    }
  }

  // Do a pass on aggregator interface files to gather all transformations.
  unordered_map<string, DataType> transform_map;
  for (const auto& guid : guids) {
    auto& metadata_files = supported_guids[guid];

    // Setup parser for aggregator interface file. That's where transformations
    // are described.
    xml::XmlParser agg_intf_parser;
    int result =
        agg_intf_parser.ParseFile(metadata_files.aggregator_interface_);
    if (result)
      AGG_XML_ERROR_EXIT(metadata_files.aggregator_interface_
                         << ": " << strerror(result));

    agg_intf_parser.RegisterNamespace(kCommonNs, kCommonNsUri);
    agg_intf_parser.RegisterNamespace(kXiNs, kXiNsUri);
    agg_intf_parser.RegisterNamespace(kTELINs, kTELINsUri);

    xml::ScopedXmlXPathObject transforms_match =
        agg_intf_parser.XPathEval(kXPathTransforms);
    if (!transforms_match || !transforms_match->nodesetval ||
        !transforms_match->nodesetval->nodeTab)
      AGG_XML_ERROR_EXIT("failed to find " << kXPathTransforms);

    xmlNodeSetPtr transforms = transforms_match->nodesetval;
    // For each transformation, read its output datatype as we'll need it later
    // to select the transformation type. For safety check whether earlier
    // entries (from other GUIDs) are consistent and error out if they're not.
    for (size_t i = 0; i < transforms->nodeNr; i++) {
      auto transform = transforms->nodeTab[i];
      auto id = agg_intf_parser.GetAttrValue(transform, kAttrTransformId);
      if (!id)
        AGG_XML_ERROR_EXIT("failed to find " << kAttrTransformId
                                             << " in a transformation node");
      auto output_dataclass =
          agg_intf_parser.GetXPathNodeTextValue(transform, kXPathTransformType);
      if (!output_dataclass)
        AGG_XML_ERROR_EXIT("failed to parse the type of " << *id);
      // Determine the type. Most samples are floats. For others, default to an
      // unsigned integer.
      DataType type = DataType::FLOAT;
      if (*output_dataclass != "float") {
        // Try to detect if it's a signed integer. This is unfortunately not
        // explicitly given but the "Sxxx" transformID seems to indicate a
        // signed integer as the transformation is essentially a U2
        // representation of it.
        if (id->starts_with("S"))
          type = DataType::SINT;
        else
          type = DataType::UINT;
      }
      auto transform_id = string(*id);
      if (transform_map.contains(transform_id)) {
        if (transform_map[transform_id] != type)
          AGG_XML_ERROR_EXIT("conflicting transformation types for "
                             << transform_id << ": "
                             << transform_map[transform_id] << " != " << type);
      } else {
        transform_map[transform_id] = type;
      }
    }
  }

  // Now a final pass to extract sample extraction and transformation rules.

  // Maps to track the encountered samples and their position in the result.
  // This is later used for extra parameters handling.
  unordered_map<string, size_t> sample_name_map, extra_arg_map;
  // Indexes to values for every ctx_.extra_args_ entry. This is needed as we
  // build-up the result table while recording extra_args_ data, so pointers
  // will change.
  vector<size_t> extra_arg_indexes;
  for (const auto& guid : guids) {
    // GUID-local sample index.
    size_t guid_sample_idx = 0;
    // Data offset in bytes.
    size_t data_offset = 0;
    // Set of metadata files for the current GUID.
    auto& metadata_files = supported_guids[guid];
    // Each parsed file will only be parsed once, so keep the parser within loop
    // scope.
    xml::XmlParser agg_parser;
    xml::XmlParser agg_intf_parser;

    // Setup parsers for aggregator and aggregator interface files.
    int result = agg_parser.ParseFile(metadata_files.aggregator_);
    if (result)
      AGG_XML_ERROR_EXIT(metadata_files.aggregator_ << ": "
                                                    << strerror(result));
    result = agg_intf_parser.ParseFile(metadata_files.aggregator_interface_);
    if (result)
      AGG_XML_ERROR_EXIT(metadata_files.aggregator_interface_
                         << ": " << strerror(result));

    agg_parser.RegisterNamespace(kXsiNs, kXsiNsUri);
    agg_parser.RegisterNamespace(kXiNs, kXiNsUri);
    agg_parser.RegisterNamespace(kTELCNs, kTELCNsUri);
    agg_parser.RegisterNamespace(kTELEMNs, kTELEMNsUri);
    agg_intf_parser.RegisterNamespace(kCommonNs, kCommonNsUri);
    agg_intf_parser.RegisterNamespace(kXiNs, kXiNsUri);
    agg_intf_parser.RegisterNamespace(kTELINs, kTELINsUri);

    xml::ScopedXmlXPathObject samples_match =
        agg_parser.XPathEval(kXPathSamples);
    if (!samples_match || !samples_match->nodesetval ||
        !samples_match->nodesetval->nodeTab)
      AGG_XML_ERROR_EXIT("failed to find " << kXPathSamples);

    xmlNodeSetPtr samples = samples_match->nodesetval;
    // Iterate over samples defined in the aggregator. For every sample decode
    // extraction parameters. If it's not a placeholder sample to skip,
    // correlate it with the aggregator interface based on the sample index and
    // extract the transformation parameters.
    // Note that both files seem to be ordered by the sample index so in
    // theory one could just iterate both files in parallel and do this in O(n)
    // but it's safer to correlate both files (though we end up with O(n**2)).

    // Keep track of the current sample group to figure out when to switch the
    // data offset to a new word.
    auto current_group = samples->nodeTab[0]->parent;
    for (size_t i = 0; i < samples->nodeNr; i++, guid_sample_idx++) {
      auto sample = samples->nodeTab[i];

      // Parse the extraction parameters.
      auto sample_id(agg_parser.GetAttrValue(sample, "sampleID"));
      if (!sample_id)
        AGG_XML_ERROR_EXIT("failed to parse GUID 0x"
                           << hex << guid << " sample nr " << guid_sample_idx);
      auto sample_name(agg_parser.GetAttrValue(sample, "name"));
      if (!sample_name)
        AGG_XML_ERROR_EXIT("failed to parse GUID 0x"
                           << hex << guid << " sample nr " << guid_sample_idx);
      auto lsb_str(agg_parser.GetXPathNodeTextValue(sample, kXPathLsb));
      auto msb_str(agg_parser.GetXPathNodeTextValue(sample, kXPathMsb));
      if (!lsb_str || !msb_str)
        AGG_XML_ERROR_EXIT("failed to find lsb and msb fields for GUID 0x"
                           << hex << guid << " sample " << *sample_id);

      struct SampleDecodingInfo info = {0};
      unsigned int integer;
      if (!base::StringToUint(*lsb_str, &integer))
        AGG_XML_ERROR_EXIT("failed to parse GUID 0x" << hex << guid
                                                     << " sample " << *sample_id
                                                     << " lsb: " << *lsb_str);
      info.lsb_ = integer;
      if (!base::StringToUint(*msb_str, &integer))
        AGG_XML_ERROR_EXIT("failed to parse GUID 0x" << hex << guid
                                                     << " sample " << *sample_id
                                                     << " msb: " << *msb_str);
      info.msb_ = integer;

      // If sample group changed adjust the data offset.
      if (current_group != sample->parent) {
        data_offset += 8;
        current_group = sample->parent;
      }
      // Set the offset to the beginning of the current 64bit word.
      info.offset_ = data_offset;

      // If this sample should be skipped, do so. The offset was updated
      // already.
      if (RE2::PartialMatch(*sample_id, samples_to_skip))
        continue;

      // Find the corresponding aggregate interface definition.
      string xpath_sample_intf_id =
          "/TELI:AggregatorInterface/TELI:AggregatorSamples/"
          "TELI:T_AggregatorSample[@sampleID='";
      xpath_sample_intf_id += base::ToString(guid_sample_idx);
      xpath_sample_intf_id += "']";
      xml::ScopedXmlXPathObject sample_intf_match =
          agg_intf_parser.XPathEval(xpath_sample_intf_id);

      if (!sample_intf_match || !sample_intf_match->nodesetval ||
          sample_intf_match->nodesetval->nodeNr != 1)
        AGG_XML_ERROR_EXIT("Failed to find aggregator interface for GUID 0x"
                           << hex << guid << " sample " << *sample_id);
      auto sample_intf = sample_intf_match->nodesetval->nodeTab[0];

      // Safety check: TELC:sample.name == TELI:T_AggregatorSample.sampleName.
      auto sample_intf_name =
          agg_intf_parser.GetAttrValue(sample_intf, "sampleName");
      if (!sample_intf_name || *sample_intf_name != sample_name)
        AGG_XML_ERROR_EXIT("aggregator interface for GUID 0x"
                           << hex << guid << " sample " << *sample_name
                           << " does not match: " << *sample_intf_name);

      // Find and fill transformation parameters.
      auto transform_ref = agg_intf_parser.GetXPathNodeTextValue(
          sample_intf, kXPathTransformRef);
      if (!transform_ref)
        AGG_XML_ERROR_EXIT("failed to find transformation type for GUID 0x"
                           << hex << guid << " sample " << *sample_id);

      if (!transform_map.contains(*transform_ref))
        AGG_XML_ERROR_EXIT("unknown transformation " << *transform_ref);
      auto data_type = transform_map[*transform_ref];
      if (data_type == DataType::FLOAT) {
        info.transform_.to_float_ = GetFloatTransform(*transform_ref);
        if (info.transform_.to_float_ == nullptr) {
          LOG(WARNING) << "No known transformation for GUID 0x" << hex << guid
                       << " sample " << *sample_id << ". Skipping.";
          continue;
        }
      } else {
        info.transform_.to_int_ = GetIntegerTransform(*transform_ref);
        if (info.transform_.to_int_ == nullptr) {
          LOG(WARNING) << "No known transformation for GUID 0x" << hex << guid
                       << " sample " << *sample_id << ". Skipping.";
          continue;
        }
      }

      // Check transformation parameters or find other parameters if necessary.
      auto parameters_match = agg_intf_parser.XPathNodeEval(
          sample_intf,
          "./cmn:TransFormInputs/cmn:TransFormInput/cmn:sampleIDREF");
      if (!parameters_match || !parameters_match->nodesetval ||
          parameters_match->nodesetval->nodeNr < 1)
        AGG_XML_ERROR_EXIT("invalid number of parameters for GUID 0x"
                           << hex << guid << " sample " << *sample_id);
      for (int param_idx = 0; param_idx < parameters_match->nodesetval->nodeNr;
           param_idx++) {
        auto param = parameters_match->nodesetval->nodeTab[param_idx];
        // This should never happen (nodeNr > 0), it means an error in
        // libxml2 or the metadata schema changed drastically.
        if (!param || !param->children || !param->children->content)
          AGG_XML_ERROR_EXIT("error in libxml child parsing for GUID 0x"
                             << hex << guid << " sample " << *sample_id);
        string param_name(xml::XmlCharCast(param->children->content));
        if (param_idx == 0) {
          // The first parameter in all supported transformations is the
          // sample. Make sure it is so.
          if (param_name != sample_id)
            AGG_XML_ERROR_EXIT("first parameter of GUID 0x"
                               << hex << guid << " sample " << *sample_id
                               << " is not the sample: " << param_name);
          // Now handle 2 special cases of a single-parameter transformation
          // with an implicit parameter.
          if (transform_ref == "pkgc_wake_cause") {
            // pkgc_wake_cause is a special case that in fact is a 2-parameter
            // transformation, with the PACKAGE_CSTATE_WAKE_REFCNT as the 2nd
            // parameter implicit in some metadata files and not in others.
            if (!sample_name_map.contains(kSamplePkgcWakeRefcnt))
              AGG_XML_ERROR_EXIT(
                  "failed to setup pkgc_wake_cause transformation, "
                  << kSamplePkgcWakeRefcnt << " missing.");
            size_t sample_idx = sample_name_map[kSamplePkgcWakeRefcnt];
            if (!extra_arg_map.contains(kSamplePkgcWakeRefcnt)) {
              extra_arg_map[kSamplePkgcWakeRefcnt] = ctx_.extra_args_.size();
              // Push empty for now, we'll update it with pointers later.
              ctx_.extra_args_.push_back({});
              extra_arg_indexes.push_back(sample_idx);
            }
            info.extra_arg_idx_ = extra_arg_map[kSamplePkgcWakeRefcnt];
            break;
          } else if (transform_ref == "pkgc_block_cause") {
            // pkgc_block_cause is a special case that in fact is a 2-parameter
            // transformation, with the PACKAGE_CSTATE_BLOCK_REFCNT 2nd
            // parameter implicit in some metadata and not in others.
            if (!sample_name_map.contains(kSamplePkgcBlockRefcnt))
              AGG_XML_ERROR_EXIT(
                  "failed to setup pkgc_wake_cause transformation, "
                  << kSamplePkgcBlockRefcnt << " missing.");
            size_t sample_idx = sample_name_map[kSamplePkgcBlockRefcnt];
            if (!extra_arg_map.contains(kSamplePkgcBlockRefcnt)) {
              extra_arg_map[kSamplePkgcBlockRefcnt] = ctx_.extra_args_.size();
              // Push an empty struct, pointers will be updated later.
              ctx_.extra_args_.push_back({});
              extra_arg_indexes.push_back(sample_idx);
            }
            info.extra_arg_idx_ = extra_arg_map[kSamplePkgcBlockRefcnt];
            break;
          }
        } else if (param_idx == 1) {
          if (!sample_name_map.contains(param_name))
            AGG_XML_ERROR_EXIT("failed to setup " << *transform_ref
                                                  << " transformation, "
                                                  << param_name << " missing.");
          size_t sample_idx = sample_name_map[param_name];
          if (!extra_arg_map.contains(param_name)) {
            extra_arg_map[param_name] = ctx_.extra_args_.size();
            // Push an empty struct, pointers will be updated later.
            ctx_.extra_args_.push_back({});
            extra_arg_indexes.push_back(sample_idx);
          }
          info.extra_arg_idx_ = extra_arg_map[param_name];
        } else {
          AGG_XML_ERROR_EXIT("invalid number of parameters for GUID 0x"
                             << hex << guid << " sample " << *sample_id);
        }
      }

      // Fill in the metadata.
      SampleMetadata metadata{
          .name_ = string(*sample_id),
          .group_ = agg_parser.GetXPathNodeTextValue(sample, kXPathSubgroup)
                        .value_or(""),
          .description_ =
              agg_parser.GetXPathNodeTextValue(sample, kXPathDescription)
                  .value_or(""),
          .type_ = data_type,
          .guid_ = guid,
      };

      // Commit the new sample.
      sample_name_map[metadata.name_] = ctx_.info_.size();
      ctx_.info_.push_back(info);
      ctx_.result_.meta_.push_back(metadata);
      ctx_.result_.values_.push_back(SampleValue());
    }
    // Now that the vectors are set, update the extra_args pointers.
    for (int i = 0; i < extra_arg_indexes.size(); i++)
      ctx_.extra_args_[i].parameter_1_ =
          &ctx_.result_.values_[extra_arg_indexes[i]];
  }

  return 0;
}

int PmtDecoder::CleanUpDecoding() {
  if (ctx_.info_.empty())
    return -ENOENT;
  ctx_.extra_args_.clear();
  ctx_.info_.clear();
  ctx_.result_.meta_.clear();
  ctx_.result_.values_.clear();
  return 0;
}

const DecodingResult* PmtDecoder::Decode(const Snapshot* const data) {
  const size_t nvals = ctx_.result_.values_.size();
  Guid guid = 0;
  const char* pmt_data = nullptr;
  size_t pmt_data_size = 0;
  // Iterate over ctx_.info_ while keeping track of the current GUID.
  for (int i = 0; i < nvals; i++) {
    const auto& meta = ctx_.result_.meta_[i];
    const auto& info = ctx_.info_[i];
    auto& value = ctx_.result_.values_[i];
    // When moving to a new GUID, switch the data pointer.
    if (UNLIKELY(guid != meta.guid_)) {
      pmt_data = nullptr;
      for (const auto& device : data->devices()) {
        if (device.guid() == meta.guid_) {
          pmt_data = device.data().c_str();
          pmt_data_size = device.data().size();
          break;
        }
      }
      // There is an edge case where user set up collection for a different
      // set of GUIDs than decoding. It's better to error out in that case
      // instead of silently skipping all the samples for that GUID.
      if (UNLIKELY(pmt_data == nullptr)) {
        LOG(ERROR) << "GUID 0x" << hex << meta.guid_
                   << " is not present in the PMT snapshot.";
        return nullptr;
      }
      guid = meta.guid_;
    }
    // Since PMT data buffer is read from sysfs, this can only happen if the
    // PMT schema assumes that PMC should generate more data. This means
    // either schema error or a need to perform a uCode update. To maintain
    // forward compatibility: warn and skip.
    if (UNLIKELY(info.offset_ > pmt_data_size)) {
      LOG(WARNING) << "Not enough data in PMT: " << meta.name_
                   << " is missing (" << info.offset_ << " > " << pmt_data_size
                   << ")";
      continue;
    }
    // Extract the value.
    memcpy(&value.u64_, pmt_data + info.offset_, sizeof(value.u64_));
    value.u64_ = GetField(value.u64_, info.msb_, info.lsb_);
    // Transform the value.
    if (meta.type_ == FLOAT) {
      value.f_ = info.transform_.to_float_(value, &ctx_, i);
    } else {
      // Transform function will sign-extend the integer if needed. This means
      // we can always assign to the i64_ field while u64_ is just a shortcut
      // for accessing DataType::UINT.
      value.i64_ = info.transform_.to_int_(value, &ctx_, i);
    }
  }
  return &ctx_.result_;
}

}  // namespace pmt
