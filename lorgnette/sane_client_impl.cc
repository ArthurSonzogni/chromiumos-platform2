// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <optional>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/containers/flat_map.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <re2/re2.h>
#include <sane/saneopts.h>
#include <sane-airscan/airscan.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/guess_source.h"

static const char* kDbusDomain = brillo::errors::dbus::kDomain;
static const char* kRightJustification = "right";
static const char* kCenterJustification = "center";

namespace lorgnette {

namespace {

DocumentSource CreateDocumentSource(const std::string& name) {
  DocumentSource source;
  source.set_name(name);
  std::optional<SourceType> type = GuessSourceType(name);
  if (type.has_value()) {
    source.set_type(type.value());
  }
  return source;
}

ColorMode ColorModeFromSaneString(const std::string& mode) {
  if (mode == kScanPropertyModeLineart)
    return MODE_LINEART;
  else if (mode == kScanPropertyModeGray)
    return MODE_GRAYSCALE;
  else if (mode == kScanPropertyModeColor)
    return MODE_COLOR;
  return MODE_UNSPECIFIED;
}

}  // namespace

// static
std::unique_ptr<SaneClientImpl> SaneClientImpl::Create(
    LibsaneWrapper* libsane) {
  SANE_Status status = libsane->sane_init(nullptr, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "Unable to initialize SANE";
    return nullptr;
  }

  // Cannot use make_unique() with a private constructor.
  return std::unique_ptr<SaneClientImpl>(new SaneClientImpl(libsane));
}

SaneClientImpl::~SaneClientImpl() {
  libsane_->sane_exit();
}

std::optional<std::vector<ScannerInfo>> SaneClientImpl::ListDevices(
    brillo::ErrorPtr* error) {
  base::AutoLock auto_lock(lock_);
  const SANE_Device** device_list;
  SANE_Status status = libsane_->sane_get_devices(&device_list, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Unable to get device list from SANE");
    return std::nullopt;
  }

  return DeviceListToScannerInfo(device_list);
}

// static
std::optional<std::vector<ScannerInfo>> SaneClientImpl::DeviceListToScannerInfo(
    const SANE_Device** device_list) {
  if (!device_list) {
    LOG(ERROR) << "'device_list' cannot be NULL";
    return std::nullopt;
  }

  std::unordered_set<std::string> names;
  std::vector<ScannerInfo> scanners;
  for (int i = 0; device_list[i]; i++) {
    const SANE_Device* dev = device_list[i];
    if (!dev->name || strcmp(dev->name, "") == 0)
      continue;

    if (names.count(dev->name) != 0) {
      LOG(ERROR) << "Duplicate device name: " << dev->name;
      return std::nullopt;
    }
    names.insert(dev->name);

    ScannerInfo info;
    info.set_name(dev->name);
    info.set_manufacturer(dev->vendor ? dev->vendor : "");
    info.set_model(dev->model ? dev->model : "");
    info.set_type(dev->type ? dev->type : "");
    scanners.push_back(info);
  }
  return scanners;
}

SaneClientImpl::SaneClientImpl(LibsaneWrapper* libsane)
    : libsane_(libsane), open_devices_(std::make_shared<DeviceSet>()) {}

std::unique_ptr<SaneDevice> SaneClientImpl::ConnectToDeviceInternal(
    brillo::ErrorPtr* error,
    SANE_Status* sane_status,
    const std::string& device_name) {
  LOG(INFO) << "Creating connection to device: " << device_name;
  base::AutoLock auto_lock(lock_);
  SANE_Handle handle;
  {
    base::AutoLock auto_lock(open_devices_->first);
    if (open_devices_->second.count(device_name) != 0) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Device '%s' is currently in-use", device_name.c_str());
      return nullptr;
    }

    SANE_Status status = libsane_->sane_open(device_name.c_str(), &handle);
    if (status != SANE_STATUS_GOOD) {
      brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                                 kManagerServiceError,
                                 "Unable to open device '%s': %s",
                                 device_name.c_str(), sane_strstatus(status));
      if (sane_status)
        *sane_status = status;

      return nullptr;
    }

    open_devices_->second.insert(device_name);
  }

  // Cannot use make_unique() with a private constructor.
  auto device = std::unique_ptr<SaneDeviceImpl>(
      new SaneDeviceImpl(libsane_, handle, device_name, open_devices_));
  device->LoadOptions(error);
  return device;
}

SaneDeviceImpl::~SaneDeviceImpl() {
  if (handle_) {
    // If a scan is running, this will call sane_cancel() first.
    libsane_->sane_close(handle_);
  }
  base::AutoLock auto_lock(open_devices_->first);
  open_devices_->second.erase(name_);
}

std::optional<ValidOptionValues> SaneDeviceImpl::GetValidOptionValues(
    brillo::ErrorPtr* error) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scanner connected");
    return std::nullopt;
  }

  ValidOptionValues values;

  // TODO(b/179492658): Once the scan app is using the resolutions from
  // DocumentSource instead of ScannerCapabilities, remove this logic.
  std::optional<std::vector<uint32_t>> resolutions = GetResolutions(error);
  if (!resolutions.has_value()) {
    return std::nullopt;  // brillo::Error::AddTo already called.
  }
  values.resolutions = std::move(resolutions.value());

  if (known_options_.count(kSource) != 0) {
    const SaneOption& option = known_options_.at(kSource);
    std::optional<std::vector<std::string>> source_names =
        option.GetValidStringValues();
    if (!source_names.has_value()) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Failed to get valid values for sources setting from option %s",
          option.GetName().c_str());
      return std::nullopt;
    }

    for (const std::string& source_name : source_names.value()) {
      values.sources.push_back(CreateDocumentSource(source_name));
    }
  } else {
    // The backend doesn't expose any source options; add a special default
    // source using our special source name. We'll calculate the scannable area
    // for this default source later.
    values.sources.push_back(
        CreateDocumentSource(kUnspecifiedDefaultSourceName));
  }

  if (known_options_.count(kTopLeftX) != 0 &&
      known_options_.count(kTopLeftY) != 0 &&
      known_options_.count(kBottomRightX) != 0 &&
      known_options_.count(kBottomRightY) != 0) {
    DCHECK(!values.sources.empty())
        << "Sources is missing default source value.";
    // We can get the capabilities for each scan source by setting the
    // document source to each possible value, and then calculating the area
    // for that source and retrieving the source's supported resolutions and
    // color modes.
    std::optional<std::string> initial_source = GetDocumentSource(error);
    if (!initial_source.has_value()) {
      return std::nullopt;  // brillo::Error::AddTo already called.
    }

    for (DocumentSource& source : values.sources) {
      if (!SetDocumentSource(error, source.name())) {
        return std::nullopt;  // brillo::Error::AddTo already called.
      }

      std::optional<ScannableArea> area = CalculateScannableArea(error);
      if (!area.has_value()) {
        return std::nullopt;  // brillo::Error::AddTo already called.
      }

      *source.mutable_area() = std::move(area.value());

      std::optional<std::vector<uint32_t>> resolutions = GetResolutions(error);
      if (!resolutions.has_value()) {
        return std::nullopt;  // brillo::Error::AddTo already called.
      }

      // These values correspond to the values of Chromium's
      // ScanJobSettingsResolution enum in
      // src/ash/webui/scanning/scanning_uma.h. Before adding values
      // here, add them to the ScanJobSettingsResolution enum.
      const std::vector<uint32_t> supported_resolutions = {75,  100, 150,
                                                           200, 300, 600};

      for (const uint32_t resolution : resolutions.value()) {
        if (base::Contains(supported_resolutions, resolution)) {
          source.add_resolutions(resolution);
        }
      }

      std::optional<std::vector<std::string>> color_modes =
          GetColorModes(error);
      if (!color_modes.has_value()) {
        return std::nullopt;  // brillo::Error::AddTo already called.
      }

      for (const std::string& mode : color_modes.value()) {
        const ColorMode color_mode = ColorModeFromSaneString(mode);
        if (color_mode != MODE_UNSPECIFIED) {
          source.add_color_modes(color_mode);
        }
      }
    }

    // Restore DocumentSource to its initial value.
    if (!SetDocumentSource(error, initial_source.value())) {
      return std::nullopt;  // brillo::Error::AddTo already called.
    }
  }

  // TODO(b/179492658): Once the scan app is using the color modes from
  // DocumentSource instead of ScannerCapabilities, remove this logic.
  std::optional<std::vector<std::string>> color_modes = GetColorModes(error);
  if (!color_modes.has_value()) {
    return std::nullopt;  // brillo::Error::AddTo already called.
  }
  values.color_modes = std::move(color_modes.value());

  return values;
}

std::optional<ScannerConfig> SaneDeviceImpl::GetCurrentConfig(
    brillo::ErrorPtr* error) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scanner connected");
    return std::nullopt;
  }

  ScannerConfig config;
  for (const auto& kv : all_options_) {
    std::optional<ScannerOption> option = kv.second.ToScannerOption();
    if (!option.has_value()) {
      LOG(ERROR) << "Unable to convert option " << kv.second.GetName()
                 << " to ScannerOption proto";
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Unable to convert option %s to ScannerOption proto",
          kv.second.GetName().c_str());
      continue;
    }
    (*config.mutable_options())[kv.first] = std::move(*option);
  }
  for (const auto& group : option_groups_) {
    *config.add_option_groups() = group;
  }
  return config;
}

std::optional<int> SaneDeviceImpl::GetScanResolution(brillo::ErrorPtr* error) {
  return GetOption<int>(error, kResolution);
}

bool SaneDeviceImpl::SetScanResolution(brillo::ErrorPtr* error,
                                       int resolution) {
  return SetOption(error, kResolution, resolution);
}

std::optional<std::string> SaneDeviceImpl::GetDocumentSource(
    brillo::ErrorPtr* error) {
  return GetOption<std::string>(error, kSource);
}

bool SaneDeviceImpl::SetDocumentSource(brillo::ErrorPtr* error,
                                       const std::string& source_name) {
  return SetOption(error, kSource, source_name);
}

std::optional<ColorMode> SaneDeviceImpl::GetColorMode(brillo::ErrorPtr* error) {
  std::optional<std::string> sane_color_mode =
      GetOption<std::string>(error, kScanMode);
  if (!sane_color_mode.has_value())
    return std::nullopt;  // brillo::Error::AddTo already called.

  return ColorModeFromSaneString(sane_color_mode.value());
}

bool SaneDeviceImpl::SetColorMode(brillo::ErrorPtr* error,
                                  ColorMode color_mode) {
  std::string mode_string = "";
  switch (color_mode) {
    case MODE_LINEART:
      mode_string = kScanPropertyModeLineart;
      break;
    case MODE_GRAYSCALE:
      mode_string = kScanPropertyModeGray;
      break;
    case MODE_COLOR:
      mode_string = kScanPropertyModeColor;
      break;
    default:
      brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                                 kManagerServiceError, "Invalid color mode: %s",
                                 ColorMode_Name(color_mode).c_str());
      return false;
  }

  return SetOption(error, kScanMode, mode_string);
}

bool SaneDeviceImpl::SetScanRegion(brillo::ErrorPtr* error,
                                   const ScanRegion& region) {
  // If the scanner exposes page-width and page-height options, these need to be
  // set before the main scan region coordinates will be accepted.
  if (base::Contains(known_options_, kPageWidth)) {
    double page_width = region.bottom_right_x() - region.top_left_x();
    if (!SetOption(error, kPageWidth, page_width)) {
      return false;  // brillo::Error::AddTo already called.
    }
  }
  if (base::Contains(known_options_, kPageHeight)) {
    double page_height = region.bottom_right_y() - region.top_left_y();
    if (!SetOption(error, kPageHeight, page_height)) {
      return false;  // brillo::Error::AddTo already called.
    }
  }

  // Get the offsets for X and Y so that if the device's coordinate system
  // doesn't start at (0, 0), we can translate the requested region into the
  // device's coordinates. We provide the appearance to the user that all
  // region options start at (0, 0).
  std::optional<double> x_offset = GetOptionOffset(error, kTopLeftX);
  if (!x_offset.has_value())
    return false;  // brillo::Error::AddTo already called.

  // Get ADF justification offset modification if justification is specified.
  std::optional<uint32_t> justification_x_offset =
      GetJustificationXOffset(region, error);
  if (!justification_x_offset.has_value()) {
    return false;  // brillo::Error::AddTo already called.
  }
  x_offset.value() += justification_x_offset.value();

  std::optional<double> y_offset = GetOptionOffset(error, kTopLeftY);
  if (!y_offset.has_value())
    return false;  // brillo::Error::AddTo already called.

  const base::flat_map<ScanOption, double> values{
      {kTopLeftX, region.top_left_x() + x_offset.value()},
      {kTopLeftY, region.top_left_y() + y_offset.value()},
      {kBottomRightX, region.bottom_right_x() + x_offset.value()},
      {kBottomRightY, region.bottom_right_y() + y_offset.value()},
  };

  for (const auto& kv : values) {
    ScanOption option_name = kv.first;
    double value = kv.second;

    if (!SetOption(error, option_name, value)) {
      return false;  // brillo::Error::AddTo already called.
    }
  }
  return true;
}

SANE_Status SaneDeviceImpl::StartScan(brillo::ErrorPtr* error) {
  if (scan_running_) {
    // If we haven't already reached EOF for the current image frame and we
    // try to start acquiring a new frame, SANE will fail with an unhelpful
    // error. This error message makes it a little clearer what's happening.
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan is already in progress");
    return SANE_STATUS_DEVICE_BUSY;
  }

  SANE_Status status = libsane_->sane_start(handle_);
  if (status == SANE_STATUS_GOOD) {
    scan_running_ = true;
  }

  return status;
}

std::optional<ScanParameters> SaneDeviceImpl::GetScanParameters(
    brillo::ErrorPtr* error) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scanner connected");
    return std::nullopt;
  }

  SANE_Parameters params;
  SANE_Status status = libsane_->sane_get_parameters(handle_, &params);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Failed to read scan parameters: %s", sane_strstatus(status));
    return std::nullopt;
  }

  ScanParameters parameters;
  switch (params.format) {
    case SANE_FRAME_GRAY:
      parameters.format = kGrayscale;
      break;
    case SANE_FRAME_RGB:
      parameters.format = kRGB;
      break;
    default:
      brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                           "Unsupported scan frame format");
      return std::nullopt;
  }

  parameters.bytes_per_line = params.bytes_per_line;
  parameters.pixels_per_line = params.pixels_per_line;
  parameters.lines = params.lines;
  parameters.depth = params.depth;
  return parameters;
}

SANE_Status SaneDeviceImpl::ReadScanData(brillo::ErrorPtr* error,
                                         uint8_t* buf,
                                         size_t count,
                                         size_t* read_out) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scanner connected");
    return SANE_STATUS_INVAL;
  }

  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scan in progress");
    return SANE_STATUS_INVAL;
  }

  if (!buf || !read_out) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "'buf' and 'read' pointers cannot be null");
    return SANE_STATUS_INVAL;
  }
  SANE_Int read = 0;
  SANE_Status status = libsane_->sane_read(handle_, buf, count, &read);
  // The SANE API requires that a non GOOD status will return 0 bytes read.
  *read_out = read;
  if (status != SANE_STATUS_GOOD) {
    scan_running_ = false;
  }
  return status;
}

bool SaneDeviceImpl::CancelScan(brillo::ErrorPtr* error) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scanner connected");
    return false;
  }

  scan_running_ = false;
  libsane_->sane_cancel(handle_);
  return true;
}

SaneDeviceImpl::SaneDeviceImpl(LibsaneWrapper* libsane,
                               SANE_Handle handle,
                               const std::string& name,
                               std::shared_ptr<DeviceSet> open_devices)
    : libsane_(libsane),
      handle_(handle),
      name_(name),
      open_devices_(open_devices),
      scan_running_(false) {
  CHECK(libsane);
  CHECK(open_devices);
}

bool SaneDeviceImpl::LoadOptions(brillo::ErrorPtr* error) {
  // First we get option descriptor 0, which contains the total count of
  // options. We don't strictly need the descriptor, but it's "Good form" to
  // do so according to 'scanimage'.
  const SANE_Option_Descriptor* desc =
      libsane_->sane_get_option_descriptor(handle_, 0);
  if (!desc) {
    LOG(ERROR) << __func__ << ": Unable to retrieve option descriptor 0";
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Unable to get option count descriptor for device");
    return false;
  }

  SANE_Int num_options = 0;
  SANE_Status status = libsane_->sane_control_option(
      handle_, 0, SANE_ACTION_GET_VALUE, &num_options, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << __func__ << ": Unable to retrieve value from option 0: "
               << sane_strstatus(status);
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Unable to get option count for device: %s", sane_strstatus(status));
    return false;
  }
  LOG(INFO) << __func__ << ": Expected option count: "
            << num_options - 1;  // -1 to ignore option 0.

  base::flat_map<std::string, ScanOption> region_options = {
      {SANE_NAME_SCAN_TL_X, kTopLeftX},
      {SANE_NAME_SCAN_TL_Y, kTopLeftY},
      {SANE_NAME_SCAN_BR_X, kBottomRightX},
      {SANE_NAME_SCAN_BR_Y, kBottomRightY},
      {SANE_NAME_PAGE_WIDTH, kPageWidth},
      {SANE_NAME_PAGE_HEIGHT, kPageHeight},
  };

  known_options_.clear();
  all_options_.clear();
  all_options_.reserve(num_options);
  option_groups_.clear();
  lorgnette::OptionGroup* current_group = nullptr;
  size_t active_options = 0;
  size_t inactive_options = 0;

  // Start at 1, since we've already checked option 0 above.
  for (int i = 1; i < num_options; i++) {
    const SANE_Option_Descriptor* opt =
        libsane_->sane_get_option_descriptor(handle_, i);
    if (!opt) {
      LOG(ERROR) << __func__ << ": Unable to get option descriptor " << i;
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Unable to get option descriptor %d for device", i);
      return false;
    }

    // Don't track group options in the main option list.
    if (opt->type == SANE_TYPE_GROUP) {
      lorgnette::OptionGroup& group = option_groups_.emplace_back();
      group.set_title(opt->title ? opt->title : "Untitled");
      current_group = &group;
      continue;
    }

    // Check for known options used by the simplified API.
    std::optional<ScanOption> known_option_name;
    if ((opt->type == SANE_TYPE_INT || opt->type == SANE_TYPE_FIXED) &&
        opt->size == sizeof(SANE_Int) && opt->unit == SANE_UNIT_DPI &&
        strcmp(opt->name, SANE_NAME_SCAN_RESOLUTION) == 0) {
      known_option_name = kResolution;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_MODE) == 0) {
      known_option_name = kScanMode;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_SOURCE) == 0) {
      known_option_name = kSource;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_ADF_JUSTIFICATION_X) == 0) {
      known_option_name = kJustificationX;
    } else if ((opt->type == SANE_TYPE_INT || opt->type == SANE_TYPE_FIXED) &&
               opt->size == sizeof(SANE_Int)) {
      auto enum_value = region_options.find(opt->name);
      if (enum_value != region_options.end()) {
        // Do not support the case where scan dimensions are specified in
        // pixels.  Don't stop parsing the option because the advanced API
        // still can make use of this case.
        if (opt->unit == SANE_UNIT_MM) {
          known_option_name = enum_value->second;
        } else {
          LOG(WARNING) << __func__ << ": Found dimension option " << opt->name
                       << " with incompatible unit: " << opt->unit;
        }
      }
    }

    // For options that are supposed to have a value, retrieve the value.
    SaneOption sane_option(*opt, i);
    if (sane_option.IsActive() && sane_option.GetSize() > 0) {
      SANE_Status status = libsane_->sane_control_option(
          handle_, i, SANE_ACTION_GET_VALUE, sane_option.GetPointer(), NULL);
      if (status != SANE_STATUS_GOOD) {
        std::string name = known_option_name.has_value()
                               ? OptionDisplayName(known_option_name.value())
                               : sane_option.GetName();
        LOG(ERROR) << __func__ << ": Unable to read value of option "
                   << sane_option.GetName() << " at index " << i << ": "
                   << sane_strstatus(status);
        brillo::Error::AddToPrintf(
            error, FROM_HERE, kDbusDomain, kManagerServiceError,
            "Unable to read value of %s option for device", name.c_str());
        return false;
      }
    }

    if (sane_option.IsActive()) {
      active_options++;
    } else {
      inactive_options++;
    }

    // known_options gets a copy of the option, not a pointer to the same
    // object.  There are fewer than a dozen known options and they don't
    // interact directly with all_options, so this duplication shouldn't be a
    // problem.
    if (known_option_name.has_value()) {
      known_options_.insert({known_option_name.value(), sane_option});
    }

    if (current_group) {
      current_group->add_members(sane_option.GetName());
    } else {
      LOG(WARNING) << __func__ << ": Option " << sane_option.GetName()
                   << " is not part of any group";
    }
    all_options_.emplace(sane_option.GetName(), std::move(sane_option));
  }

  LOG(INFO) << __func__ << ": Successfully loaded " << active_options
            << " active and " << inactive_options
            << " inactive device options in " << option_groups_.size()
            << " groups";
  return true;
}

bool SaneDeviceImpl::UpdateDeviceOption(brillo::ErrorPtr* error,
                                        SaneOption* option) {
  SANE_Int result_flags;
  SANE_Status status = libsane_->sane_control_option(
      handle_, option->GetIndex(), SANE_ACTION_SET_VALUE, option->GetPointer(),
      &result_flags);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Unable to set " + option->GetName() + " to " +
                             option->DisplayValue() + " : " +
                             sane_strstatus(status));
    // Reload options, to bring local value and device value back in sync.
    LoadOptions(error);
    return false;
  }

  // We also reload if we get SANE_INFO_INEXACT because we want to know
  // what value the printer changed our requested value to.
  // As an optimization, we could only reload this particular option.
  if (result_flags & (SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT)) {
    return LoadOptions(error);
  }
  return true;
}

std::optional<ScannableArea> SaneDeviceImpl::CalculateScannableArea(
    brillo::ErrorPtr* error) {
  // What we know from the SANE API docs (verbatim):
  // * The unit of all four scan region options must be identical
  // * A frontend can determine the size of the scan surface by first checking
  //   that the options have range constraints associated. If a range or
  //   word-list constraints exist, the frontend can take the minimum and
  //   maximum values of one of the x and y option range-constraints to
  //   determine the scan surface size.
  //
  // Based on my examination of sane-backends, every backend that declares this
  // set of options uses a range constraint.
  //
  // Several backends also have --page-width and --page-height options that
  // define the real maximum values.  If these are present, they are handled
  // automatically in the GetXRange and GetYRange functions.
  ScannableArea area;
  std::optional<OptionRange> x_range = GetXRange(error);
  if (!x_range.has_value()) {
    return std::nullopt;  // brillo::Error::AddTo already called.
  }
  area.set_width(x_range.value().size);

  std::optional<OptionRange> y_range = GetYRange(error);
  if (!y_range.has_value()) {
    return std::nullopt;  // brillo::Error::AddTo already called.
  }
  area.set_height(y_range.value().size);
  return area;
}

// Calculates the starting value of the range for the given ScanOption.
// Requires that |known_options_| contains |option|, and that the corresponding
// option descriptor for |option| has a range constraint.
std::optional<double> SaneDeviceImpl::GetOptionOffset(
    brillo::ErrorPtr* error, SaneDeviceImpl::ScanOption option) {
  if (known_options_.count(option) == 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Device is missing option %s", OptionDisplayName(option));
    return std::nullopt;
  }

  const SaneOption& sane_opt = known_options_.at(option);
  std::optional<OptionRange> range = sane_opt.GetValidRange();
  if (!range.has_value()) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Failed to get range for option: %s", sane_opt.GetName().c_str());
    return std::nullopt;
  }

  return range->start;
}

const char* SaneDeviceImpl::OptionDisplayName(ScanOption option) {
  switch (option) {
    case kResolution:
      return SANE_NAME_SCAN_RESOLUTION;
    case kScanMode:
      return SANE_NAME_SCAN_MODE;
    case kSource:
      return SANE_NAME_SCAN_SOURCE;
    case kTopLeftX:
      return SANE_NAME_SCAN_TL_X;
    case kTopLeftY:
      return SANE_NAME_SCAN_TL_Y;
    case kBottomRightX:
      return SANE_NAME_SCAN_BR_X;
    case kBottomRightY:
      return SANE_NAME_SCAN_BR_Y;
    case kJustificationX:
      return SANE_NAME_ADF_JUSTIFICATION_X;
    case kPageWidth:
      return SANE_NAME_PAGE_WIDTH;
    case kPageHeight:
      return SANE_NAME_PAGE_HEIGHT;
  }
}

template <typename T>
bool SaneDeviceImpl::SetOption(brillo::ErrorPtr* error,
                               ScanOption option_type,
                               T value) {
  if (known_options_.count(option_type) == 0) {
    brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                               kManagerServiceError, "No %s option found.",
                               OptionDisplayName(option_type));
    return false;
  }

  SaneOption& option = known_options_.at(option_type);
  if (!option.Set(value)) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Failed to set SaneOption %s", OptionDisplayName(option_type));
    return false;
  }
  return UpdateDeviceOption(error, &option);
}

template <typename T>
std::optional<T> SaneDeviceImpl::GetOption(brillo::ErrorPtr* error,
                                           ScanOption option_type) {
  if (known_options_.count(option_type) == 0) {
    brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                               kManagerServiceError, "No %s option found.",
                               OptionDisplayName(option_type));
    return std::nullopt;
  }

  const SaneOption& option = known_options_.at(option_type);
  std::optional<T> value = option.Get<T>();
  if (!value.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         option.GetName() + " is the wrong type");
  }

  return value;
}

std::optional<std::vector<uint32_t>> SaneDeviceImpl::GetResolutions(
    brillo::ErrorPtr* error) {
  if (known_options_.count(kResolution) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No resolutions available");
    return std::nullopt;
  }

  const SaneOption& option = known_options_.at(kResolution);
  std::optional<std::vector<uint32_t>> resolutions = option.GetValidIntValues();
  if (!resolutions.has_value()) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Failed to get valid values for resolution setting from %s",
        option.GetName().c_str());
    return std::nullopt;
  }
  return resolutions.value();
}

std::optional<std::vector<std::string>> SaneDeviceImpl::GetColorModes(
    brillo::ErrorPtr* error) {
  if (known_options_.count(kScanMode) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No color modes available");
    return std::nullopt;
  }

  const SaneOption& option = known_options_.at(kScanMode);
  std::optional<std::vector<std::string>> color_modes =
      option.GetValidStringValues();

  if (!color_modes.has_value()) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Failed to get valid values for scan modes setting from %s",
        option.GetName().c_str());
    return std::nullopt;
  }
  return color_modes.value();
}

std::optional<uint32_t> SaneDeviceImpl::GetJustificationXOffset(
    const ScanRegion& region, brillo::ErrorPtr* error) {
  // Offset modification only necessary for ADF source at the moment.
  std::optional<std::string> current_source = GetDocumentSource(error);
  if (!current_source.has_value()) {
    return std::nullopt;  // brillo::Error::AddTo already called.
  }
  DocumentSource src = CreateDocumentSource(current_source.value());
  if (src.type() != SOURCE_ADF_SIMPLEX && src.type() != SOURCE_ADF_DUPLEX) {
    return 0;
  }

  std::optional<OptionRange> x_range = GetXRange(error);
  if (!x_range.has_value()) {
    return std::nullopt;  // brillo::Error::AddTo already called.
  }

  std::optional<std::string> x_justification =
      GetOption<std::string>(error, kJustificationX);
  if (!x_justification.has_value()) {
    return 0;
  }

  int max_width = (x_range.value().size);
  int width = region.bottom_right_x() - region.top_left_x();
  // Calculate offset based off of Epson-provided math.
  uint32_t x_offset = 0;
  if (x_justification.value() == kRightJustification) {
    x_offset = max_width - width;
  } else if (x_justification.value() == kCenterJustification) {
    x_offset = (max_width - width) / 2;
  }

  return x_offset;
}

std::optional<OptionRange> SaneDeviceImpl::GetXRange(brillo::ErrorPtr* error) {
  ScanOption which_option;
  if (base::Contains(known_options_, kPageWidth)) {
    which_option = kPageWidth;
  } else {
    which_option = kTopLeftX;
  }

  const SaneOption& option = known_options_.at(which_option);
  std::optional<OptionRange> x_range = option.GetValidRange();
  if (!x_range.has_value()) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Invalid top-left X constraint in option %s", option.GetName().c_str());
    return std::nullopt;
  }

  return x_range;
}

std::optional<OptionRange> SaneDeviceImpl::GetYRange(brillo::ErrorPtr* error) {
  ScanOption which_option;
  if (base::Contains(known_options_, kPageHeight)) {
    which_option = kPageHeight;
  } else {
    which_option = kBottomRightY;
  }

  const SaneOption& option = known_options_.at(which_option);
  std::optional<OptionRange> y_range = option.GetValidRange();
  if (!y_range.has_value()) {
    brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                               kManagerServiceError,
                               "Invalid bottom-right Y constraint in option %s",
                               option.GetName().c_str());
    return std::nullopt;
  }

  return y_range;
}

}  // namespace lorgnette
