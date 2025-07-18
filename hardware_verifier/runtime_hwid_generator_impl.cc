// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_generator_impl.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/hash/sha1.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/file_utils.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <libsegmentation/feature_management.h>
#include <re2/re2.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/encoding_spec_loader.h"
#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/factory_hwid_processor_impl.h"
#include "hardware_verifier/runtime_hwid_generator.h"
#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

namespace {

constexpr char kCameraCategoryName[] = "camera";
constexpr char kDisplayPanelCategoryName[] = "display_panel";
constexpr char kDramCategoryName[] = "dram";

constexpr char kCompGroupField[] = "comp_group";
constexpr char kInformationField[] = "information";
constexpr char kCompNameField[] = "name";
constexpr char kPositionField[] = "position";
constexpr char kFeatureLevelField[] = "feature_level";
constexpr char kScopeLevelField[] = "scope_level";

constexpr char kGenericComponent[] = "generic";
constexpr char kCrosConfigModelNamePath[] = "/";
constexpr char kCrosConfigModelNameKey[] = "name";

constexpr char kRuntimeHWIDMagicString[] = "R:";
constexpr char kRuntimeHWIDFieldSeparator[] = "-";
constexpr char kRuntimeHWIDCompSeparator[] = ",";
constexpr char kRuntimeHWIDUnidentifiedComp[] = "?";
constexpr char kRuntimeHWIDNullComp[] = "X";
constexpr char kRuntimeHWIDSkipComp[] = "#";

constexpr int kFilePermission644 =
    base::FILE_PERMISSION_READ_BY_USER | base::FILE_PERMISSION_WRITE_BY_USER |
    base::FILE_PERMISSION_READ_BY_GROUP | base::FILE_PERMISSION_READ_BY_OTHERS;

struct ProbeComponent {
  std::string name;
  std::string position;
};

// Get the device model name.
std::string ModelName() {
  std::string model_name;

  if (Context::Get()->cros_config()->GetString(
          kCrosConfigModelNamePath, kCrosConfigModelNameKey, &model_name)) {
    return model_name;
  }

  LOG(ERROR) << "Failed to get \"" << kCrosConfigModelNamePath << " "
             << kCrosConfigModelNameKey << "\" from cros config";
  return "";
}

std::string CalculateChecksum(std::string_view runtime_hwid) {
  const auto& sha1_hash = base::SHA1HashString(runtime_hwid);
  return base::HexEncode(sha1_hash.data(), sha1_hash.size());
}

std::string GenerateCategoryRegex(std::string_view category_name) {
  if (category_name == kCameraCategoryName) {
    return R"((?:camera|video))";
  }
  return RE2::QuoteMeta(category_name);
}

int GetUnidentifiedComponentCount(
    const std::vector<ProbeComponent>& probe_components) {
  int generic_count = std::count_if(
      probe_components.begin(), probe_components.end(),
      [](const auto& comp) { return comp.name == kGenericComponent; });
  int identified_count = probe_components.size() - generic_count;
  return generic_count - identified_count;
}

// Checks if the component name is AVL compliant, i.e. matches format:
//   ({MODEL}_){CATEGORY}_{CID}(_{QID})(#{SEQ})
// Where:
//   {MODEL} is the device model name (optional prefix).
//   {CATEGORY} is the component category name.
//   {CID} is the component ID.
//   {QID} is the qualification ID (optional suffix).
//   {SEQ} is the sequence number (optional suffix).
// If it matches, returns the normalized format: {CATEGORY}_{CID}.
std::optional<std::string> NormalizeComponentNameIfAVLCompliant(
    std::string_view component_name,
    std::string_view category_name,
    std::string_view model_name) {
  std::string regex_str = R"(^(?:)" + RE2::QuoteMeta(model_name) + R"(_)?)" +
                          GenerateCategoryRegex(category_name) +
                          R"(_(\d+)(?:_\d+)?(?:#.*)?$)";

  RE2 regex(regex_str);
  std::string cid;
  if (RE2::FullMatch(component_name, regex, &cid)) {
    return base::JoinString({category_name, cid}, "_");
  }
  return std::nullopt;
}

// Normalizes all component names in |component_names|, and returns a
// |std::multiset| of normalized component names. Skips names that are not AVL
// compliant.
std::multiset<std::string> GetNormalizedComponentNames(
    const std::vector<std::string>& component_names,
    std::string_view category_name,
    std::string_view model_name) {
  std::multiset<std::string> normalized_comp_names;
  for (const auto& comp_name : component_names) {
    const auto normalized_name = NormalizeComponentNameIfAVLCompliant(
        comp_name, category_name, model_name);
    if (!normalized_name.has_value()) {
      continue;
    }
    normalized_comp_names.insert(*normalized_name);
  }
  return normalized_comp_names;
}

// Extracts |ProbeComponent| from |probe_result| for the category with name
// |category_name|. If `comp_group` in `information` field is set, use it as the
// component name. Otherwise, use the `name` field as the component name.
std::vector<ProbeComponent> GetProbeComponentsByCategoryName(
    const runtime_probe::ProbeResult& probe_result,
    std::string_view category_name) {
  const auto* probe_result_ref = probe_result.GetReflection();
  const auto* probe_result_desc = probe_result.GetDescriptor();
  const auto* component_field_desc =
      probe_result_desc->FindFieldByName(category_name);
  CHECK(component_field_desc != nullptr);

  int comp_count =
      probe_result_ref->FieldSize(probe_result, component_field_desc);
  std::vector<ProbeComponent> probe_components;
  for (int i = 0; i < comp_count; ++i) {
    const auto& component_message = probe_result_ref->GetRepeatedMessage(
        probe_result, component_field_desc, i);
    const auto* component_reflection = component_message.GetReflection();
    const auto* component_descriptor = component_message.GetDescriptor();

    // Try to get component name from information.comp_group.
    std::string component_name;
    const auto* information_field_desc =
        component_descriptor->FindFieldByName(kInformationField);
    if (information_field_desc &&
        component_reflection->HasField(component_message,
                                       information_field_desc)) {
      const auto& information_message = component_reflection->GetMessage(
          component_message, information_field_desc);
      const auto* information_ref = information_message.GetReflection();
      const auto* information_desc = information_message.GetDescriptor();
      const auto* comp_group_field_desc =
          information_desc->FindFieldByName(kCompGroupField);

      if (comp_group_field_desc &&
          information_ref->HasField(information_message,
                                    comp_group_field_desc)) {
        component_name = information_ref->GetString(information_message,
                                                    comp_group_field_desc);
      }
    }

    if (component_name.empty()) {
      const auto* name_descriptor =
          component_descriptor->FindFieldByName(kCompNameField);
      CHECK(name_descriptor != nullptr &&
            component_reflection->HasField(component_message, name_descriptor));
      component_name =
          component_reflection->GetString(component_message, name_descriptor);
    }

    const auto* position_field_desc =
        component_descriptor->FindFieldByName(kPositionField);
    CHECK(position_field_desc != nullptr);
    std::string position;
    if (component_reflection->HasField(component_message,
                                       position_field_desc)) {
      position = component_reflection->GetString(component_message,
                                                 position_field_desc);
    }

    probe_components.push_back({.name = component_name, .position = position});
  }
  return probe_components;
}

// Extracts component names from |decode_result| for the category |category|.
std::vector<std::string> GetDecodeComponentsByCategory(
    const CategoryMapping<std::vector<std::string>>& decode_result,
    runtime_probe::ProbeRequest_SupportCategory category) {
  if (!decode_result.contains(category)) {
    return {};
  }

  return decode_result.at(category);
}

// Checks if the probed components match the decoded components for a given
// category.
//
// For most categories, this function returns true if the normalized probed
// components are an exact match to the normalized decoded components, and there
// are no unidentified components in the probe result.
//
// For the "display_panel" category, it returns true if all normalized decoded
// components are present in the normalized probed components (i.e., probed is a
// superset of decoded).
bool MatchProbeAndDecodeComponents(
    const std::vector<ProbeComponent>& probe_components,
    const std::vector<std::string>& decode_components,
    std::string_view category_name,
    std::string_view model_name) {
  if (category_name != kDisplayPanelCategoryName &&
      GetUnidentifiedComponentCount(probe_components) > 0) {
    return false;
  }

  std::vector<std::string> probe_component_names;
  for (const auto& probe_component : probe_components) {
    probe_component_names.push_back(probe_component.name);
  }
  const std::multiset<std::string> normalized_probe_component_names =
      GetNormalizedComponentNames(probe_component_names, category_name,
                                  model_name);
  const std::multiset<std::string> normalized_decode_component_names =
      GetNormalizedComponentNames(decode_components, category_name, model_name);

  if (category_name == kDisplayPanelCategoryName) {
    for (const auto& decode_comp : normalized_decode_component_names) {
      if (!normalized_probe_component_names.contains(decode_comp)) {
        return false;
      }
    }
    return true;
  }
  return normalized_probe_component_names == normalized_decode_component_names;
}

std::vector<std::string> GetRuntimeHWIDComponentFieldNames() {
  std::vector<std::string> field_names;
  const auto runtime_hwid_comp_desc =
      runtime_probe::RuntimeHwidComponent::GetDescriptor();
  for (int i = 0; i < runtime_hwid_comp_desc->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field =
        runtime_hwid_comp_desc->field(i);
    field_names.push_back(field->name());
  }
  return field_names;
}

void HandleNonComponentField(std::string_view field_name,
                             std::vector<std::string>& comp_segment) {
  if (field_name == kFeatureLevelField) {
    comp_segment.push_back(base::NumberToString(
        Context::Get()->feature_management()->GetFeatureLevel()));
  } else if (field_name == kScopeLevelField) {
    comp_segment.push_back(base::NumberToString(
        Context::Get()->feature_management()->GetScopeLevel()));
  } else {
    LOG(ERROR) << "Got invalid Runtime HWID field: " << field_name;
  }
}

}  // namespace

std::unique_ptr<RuntimeHWIDGeneratorImpl> RuntimeHWIDGeneratorImpl::Create() {
  EncodingSpecLoader encoding_spec_loader;
  const auto& encoding_spec = encoding_spec_loader.Load();
  if (encoding_spec == nullptr) {
    LOG(ERROR) << "Failed to load the encoding spec.";
    return nullptr;
  }

  auto factory_hwid_processor =
      FactoryHWIDProcessorImpl::Create(*encoding_spec);
  if (factory_hwid_processor == nullptr) {
    return nullptr;
  }

  std::set<runtime_probe::ProbeRequest_SupportCategory> waived_categories;
  for (const auto& waived_category : encoding_spec->waived_categories()) {
    if (!runtime_probe::ProbeRequest_SupportCategory_IsValid(waived_category)) {
      LOG(ERROR) << "Got invalid category: " << waived_category;
      continue;
    }
    waived_categories.insert(
        static_cast<runtime_probe::ProbeRequest_SupportCategory>(
            waived_category));
  }
  return std::unique_ptr<RuntimeHWIDGeneratorImpl>(new RuntimeHWIDGeneratorImpl(
      std::move(factory_hwid_processor), waived_categories));
}

RuntimeHWIDGeneratorImpl::RuntimeHWIDGeneratorImpl(
    std::unique_ptr<FactoryHWIDProcessor> factory_hwid_processor,
    const std::set<runtime_probe::ProbeRequest_SupportCategory>&
        waived_categories)
    : factory_hwid_processor_(std::move(factory_hwid_processor)),
      waived_categories_(waived_categories) {
  CHECK(factory_hwid_processor_ != nullptr);
}

bool RuntimeHWIDGeneratorImpl::ShouldGenerateRuntimeHWID(
    const runtime_probe::ProbeResult& probe_result) const {
  const auto decode_result = factory_hwid_processor_->DecodeFactoryHWID();
  if (!decode_result.has_value()) {
    LOG(ERROR) << "Failed to decode factory HWID.";
    return false;
  }

  const std::string model_name = ModelName();
  if (model_name.empty()) {
    LOG(ERROR) << "Failed to get device model name.";
    return false;
  }

  const auto skip_zero_bit_categories =
      factory_hwid_processor_->GetSkipZeroBitCategories();
  const auto field_names = GetRuntimeHWIDComponentFieldNames();
  for (const auto& category_name : field_names) {
    runtime_probe::ProbeRequest_SupportCategory category;
    if (category_name == kDramCategoryName ||
        !runtime_probe::ProbeRequest_SupportCategory_Parse(category_name,
                                                           &category) ||
        skip_zero_bit_categories.contains(category) ||
        waived_categories_.contains(category)) {
      continue;
    }

    const std::vector<ProbeComponent> probe_components =
        GetProbeComponentsByCategoryName(probe_result, category_name);
    const std::vector<std::string> decode_components =
        GetDecodeComponentsByCategory(*decode_result, category);
    if (!MatchProbeAndDecodeComponents(probe_components, decode_components,
                                       category_name, model_name)) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> RuntimeHWIDGeneratorImpl::Generate(
    const runtime_probe::ProbeResult& probe_result) const {
  const auto masked_factory_hwid =
      factory_hwid_processor_->GenerateMaskedFactoryHWID();
  if (!masked_factory_hwid.has_value()) {
    LOG(ERROR) << "Failed to generate masked Factory HWID.";
    return std::nullopt;
  }

  const auto field_names = GetRuntimeHWIDComponentFieldNames();
  std::vector<std::string> runtime_hwid_comp_segment;
  for (const auto& field_name : field_names) {
    runtime_probe::ProbeRequest_SupportCategory category;
    if (!runtime_probe::ProbeRequest_SupportCategory_Parse(field_name,
                                                           &category)) {
      HandleNonComponentField(field_name, runtime_hwid_comp_segment);
      continue;
    }

    std::vector<std::string> component_positions;
    const std::vector<ProbeComponent> probe_components =
        GetProbeComponentsByCategoryName(probe_result, field_name);
    for (const auto& component : probe_components) {
      if (!component.position.empty()) {
        uint32_t unused_position;
        if (!base::StringToUint(component.position, &unused_position)) {
          LOG(ERROR) << "Got invalid component position \""
                     << component.position << "\" for component \""
                     << component.name << "\" in category \"" << field_name
                     << "\"";
          return std::nullopt;
        }
        component_positions.push_back(component.position);
      }
    }
    std::sort(component_positions.begin(), component_positions.end(),
              [](const std::string& lhs, const std::string& rhs) {
                uint32_t lhs_num, rhs_num;
                base::StringToUint(lhs, &lhs_num);
                base::StringToUint(rhs, &rhs_num);
                return lhs_num < rhs_num;
              });

    int unidentified_count = GetUnidentifiedComponentCount(probe_components);
    component_positions.insert(component_positions.end(), unidentified_count,
                               kRuntimeHWIDUnidentifiedComp);

    if (component_positions.empty()) {
      if (waived_categories_.contains(category)) {
        component_positions.push_back(kRuntimeHWIDSkipComp);
      } else {
        component_positions.push_back(kRuntimeHWIDNullComp);
      }
    }
    runtime_hwid_comp_segment.push_back(
        base::JoinString(component_positions, kRuntimeHWIDCompSeparator));
  }

  std::string runtime_hwid_comp =
      base::JoinString(runtime_hwid_comp_segment, kRuntimeHWIDFieldSeparator);

  return *masked_factory_hwid + " " + kRuntimeHWIDMagicString +
         runtime_hwid_comp;
}

bool RuntimeHWIDGeneratorImpl::GenerateToDevice(
    const runtime_probe::ProbeResult& probe_result) const {
  const auto runtime_hwid = Generate(probe_result);
  if (!runtime_hwid.has_value()) {
    LOG(ERROR) << "Failed to generate Runtime HWID.";
    return false;
  }
  const auto checksum = CalculateChecksum(*runtime_hwid);
  std::string content =
      base::StringPrintf("%s\n%s", runtime_hwid->c_str(), checksum.c_str());

  const auto runtime_hwid_path =
      Context::Get()->root_dir().Append(kRuntimeHWIDFilePath);
  auto file_data = base::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(content.data()), content.size());
  if (!brillo::WriteFileAtomically(runtime_hwid_path, file_data,
                                   kFilePermission644)) {
    PLOG(ERROR) << "Failed to write Runtime HWID to " << runtime_hwid_path;
    return false;
  }
  return true;
}

}  // namespace hardware_verifier
