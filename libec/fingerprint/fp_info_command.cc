// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_command.h"

#include <vector>

#include <base/strings/string_util.h>

namespace ec {

uint32_t FpInfoCommand::GetVersion() const {
  return command_version;
}

std::optional<SensorId> FpInfoCommand::sensor_id() {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->sensor_id();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->sensor_id();
  }
  return fp_info_command_v1_->sensor_id();
}

std::vector<SensorImage> FpInfoCommand::sensor_image() {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->sensor_image();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->sensor_image();
  }

  auto sensor_image = fp_info_command_v1_->sensor_image();
  if (!sensor_image.has_value()) {
    return std::vector<SensorImage>();
  }

  return std::vector{*sensor_image};
}

std::optional<TemplateInfo> FpInfoCommand::template_info() {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->template_info();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->template_info();
  }
  return fp_info_command_v1_->template_info();
}

int FpInfoCommand::NumDeadPixels() {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->NumDeadPixels();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->NumDeadPixels();
  }
  return fp_info_command_v1_->NumDeadPixels();
}

FpSensorErrors FpInfoCommand::GetFpSensorErrors() {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->GetFpSensorErrors();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->GetFpSensorErrors();
  }
  return fp_info_command_v1_->GetFpSensorErrors();
}

bool FpInfoCommand::Run(int ec_fd) {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->Run(ec_fd);
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->Run(ec_fd);
  }
  return fp_info_command_v1_->Run(ec_fd);
}

bool FpInfoCommand::Run(ec::EcUsbEndpointInterface& uep) {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->Run(uep);
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->Run(uep);
  }
  return fp_info_command_v1_->Run(uep);
}

bool FpInfoCommand::RunWithMultipleAttempts(int fd, int num_attempts) {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->RunWithMultipleAttempts(fd, num_attempts);
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->RunWithMultipleAttempts(fd, num_attempts);
  }
  return fp_info_command_v1_->RunWithMultipleAttempts(fd, num_attempts);
}

uint32_t FpInfoCommand::Version() const {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->Version();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->Version();
  }
  return fp_info_command_v1_->Version();
}

uint32_t FpInfoCommand::Command() const {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->Command();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->Command();
  }
  return fp_info_command_v1_->Command();
}

uint32_t FpInfoCommand::Result() const {
  if (GetVersion() == 3) {
    return fp_info_command_v3_->Result();
  }
  if (GetVersion() == 2) {
    return fp_info_command_v2_->Result();
  }
  return fp_info_command_v1_->Result();
}

std::string FpInfoCommand::ParseSensorInfo() {
  std::stringstream ss;

  auto sensor_id = this->sensor_id();
  ss << "Fingerprint sensor: ";
  if (sensor_id) {
    ss << "vendor 0x" << std::hex << sensor_id->vendor_id << " product 0x"
       << sensor_id->product_id << " model 0x" << sensor_id->model_id
       << " version 0x" << sensor_id->version << std::dec << std::endl;
  } else {
    ss << "Not available" << std::endl;
  }

  auto errors = GetFpSensorErrors();
  std::vector<std::string> error_flags;

  if (errors == FpSensorErrors::kNone) {
    error_flags.emplace_back("NONE");
  } else {
    if ((errors & FpSensorErrors::kNoIrq) != FpSensorErrors::kNone) {
      error_flags.emplace_back("NO_IRQ");
    }
    if ((errors & FpSensorErrors::kSpiCommunication) != FpSensorErrors::kNone) {
      error_flags.emplace_back("SPI_COMM");
    }
    if ((errors & FpSensorErrors::kBadHardwareID) != FpSensorErrors::kNone) {
      error_flags.emplace_back("BAD_HWID");
    }
    if ((errors & FpSensorErrors::kInitializationFailure) !=
        FpSensorErrors::kNone) {
      error_flags.emplace_back("INIT_FAIL");
    }
  }

  ss << "Error flags: " << base::JoinString(error_flags, " ") << std::endl;

  int dead_pixels = NumDeadPixels();
  if (dead_pixels == kDeadPixelsUnknown) {
    ss << "Dead pixels: UNKNOWN";
  } else {
    ss << "Dead pixels: " << dead_pixels;
  }
  ss << std::endl;

  auto images = sensor_image();
  if (!images.empty()) {
    int i = 0;
    for (const auto& image : images) {
      // Image parameters are parsed by tests. They MUST follow
      // key<space>value format.
      ss << "Image [" << i << "]: size " << image.width << "x" << image.height
         << " bpp " << image.bpp;

      if (image.image_data_offset_bytes) {
        ss << " offset " << *image.image_data_offset_bytes;
      }

      ss << std::endl;
      i++;
    }
  } else {
    ss << "Image: Not available" << std::endl;
  }

  auto template_info = this->template_info();
  ss << "Templates: ";
  if (template_info) {
    ss << "version " << template_info->version << " size "
       << template_info->size << " count " << template_info->num_valid << "/"
       << template_info->max_templates << " dirty bitmap " << std::hex
       << template_info->dirty.to_ulong() << std::dec << std::endl;
  } else {
    ss << "Not available" << std::endl;
  }

  return ss.str();
}

}  // namespace ec
