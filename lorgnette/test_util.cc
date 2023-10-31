// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/test_util.h"

#include <utility>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace lorgnette {

void PrintTo(const lorgnette::DocumentSource& ds, std::ostream* os) {
  *os << "DocumentSource(" << std::endl;
  *os << "  name = " << ds.name() << "," << std::endl;
  *os << "  type = " << SourceType_Name(ds.type()) << "," << std::endl;

  if (ds.has_area()) {
    *os << "  area.width = " << ds.area().width() << "," << std::endl;
    *os << "  area.height = " << ds.area().height() << "," << std::endl;
  }

  for (const auto resolution : ds.resolutions())
    *os << "  resolution = " << resolution << "," << std::endl;

  for (const auto color_mode : ds.color_modes())
    *os << "  color_mode = " << color_mode << "," << std::endl;

  *os << ")";
}

DocumentSource CreateDocumentSource(const std::string& name,
                                    SourceType type,
                                    double width,
                                    double height,
                                    const std::vector<uint32_t>& resolutions,
                                    const std::vector<ColorMode>& color_modes) {
  DocumentSource source;
  source.set_name(name);
  source.set_type(type);
  source.mutable_area()->set_width(width);
  source.mutable_area()->set_height(height);
  source.mutable_resolutions()->Add(resolutions.begin(), resolutions.end());
  source.mutable_color_modes()->Add(color_modes.begin(), color_modes.end());
  return source;
}

libusb_device_descriptor MakeMinimalDeviceDescriptor() {
  libusb_device_descriptor descriptor;
  memset(&descriptor, 0, sizeof(descriptor));
  descriptor.bLength = sizeof(descriptor);
  descriptor.bDescriptorType = LIBUSB_DT_DEVICE;
  descriptor.idVendor = 0x1234;
  descriptor.idProduct = 0x4321;
  return descriptor;
}

std::unique_ptr<libusb_interface_descriptor> MakeIppUsbInterfaceDescriptor() {
  auto descriptor = std::make_unique<libusb_interface_descriptor>();
  descriptor->bLength = sizeof(libusb_interface_descriptor);
  descriptor->bDescriptorType = LIBUSB_DT_INTERFACE;
  descriptor->bInterfaceNumber = 0;
  descriptor->bAlternateSetting = 1;
  descriptor->bInterfaceClass = LIBUSB_CLASS_PRINTER;
  descriptor->bInterfaceProtocol = 0x04;  // IPP-USB protocol.
  return descriptor;
}

MatchesScannerInfoMatcher::MatchesScannerInfoMatcher(
    lorgnette::ScannerInfo info)
    : expected_(std::move(info)) {}

bool MatchesScannerInfoMatcher::MatchAndExplain(
    const std::unique_ptr<lorgnette::ScannerInfo>& value,
    testing::MatchResultListener* ml) const {
  // Each of the error cases here also logs to LOG(ERROR) because some of the
  // existing gtest matchers ignore the MatchResultListener output.
  if (!expected_.name().empty() && value->name() != expected_.name()) {
    std::string msg =
        base::StringPrintf("name is %s, expected %s", value->name().c_str(),
                           expected_.name().c_str());
    LOG(ERROR) << msg;
    *ml << msg;
    return false;
  }
  if (!expected_.manufacturer().empty() &&
      value->manufacturer() != expected_.manufacturer()) {
    std::string msg = base::StringPrintf("manufacturer is %s, expected %s",
                                         value->manufacturer().c_str(),
                                         expected_.manufacturer().c_str());
    LOG(ERROR) << msg;
    *ml << msg;
    return false;
  }
  if (!expected_.model().empty() && value->model() != expected_.model()) {
    std::string msg =
        base::StringPrintf("model is %s, expected %s", value->model().c_str(),
                           expected_.model().c_str());
    LOG(ERROR) << msg;
    *ml << msg;
    return false;
  }
  if (expected_.connection_type() != lorgnette::CONNECTION_UNSPECIFIED &&
      value->connection_type() != expected_.connection_type()) {
    std::string msg = base::StringPrintf(
        "connection_type is %s, expected %s",
        lorgnette::ConnectionType_Name(value->connection_type()).c_str(),
        lorgnette::ConnectionType_Name(expected_.connection_type()).c_str());
    LOG(ERROR) << msg;
    *ml << msg;
    return false;
  }
  if (value->secure() != expected_.secure()) {
    std::string msg = base::StringPrintf("secure is %d, expected %d",
                                         value->secure(), expected_.secure());
    LOG(ERROR) << msg;
    *ml << msg;
    return false;
  }
  // TODO(b/308191406): Compare image_formats once something other than the
  // hardcoded defaults is available.
  if (!expected_.display_name().empty() &&
      value->display_name() != expected_.display_name()) {
    std::string msg = base::StringPrintf("display name is %s, expected %s",
                                         value->display_name().c_str(),
                                         expected_.display_name().c_str());
    LOG(ERROR) << msg;
    *ml << msg;
    return false;
  }

  return true;
}

::testing::Matcher<std::unique_ptr<lorgnette::ScannerInfo>> MatchesScannerInfo(
    lorgnette::ScannerInfo info) {
  return MatchesScannerInfoMatcher(std::move(info));
}

}  // namespace lorgnette
