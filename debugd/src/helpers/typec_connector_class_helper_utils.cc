// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "debugd/src/helpers/typec_connector_class_helper_utils.h"

namespace debugd {
namespace typec_connector_class_helper {

std::string GetIndentStr(int indent) {
  return std::string(indent, ' ');
}

std::string FormatString(std::string file_str, int indent) {
  std::string out_str;
  base::TrimWhitespaceASCII(file_str, base::TRIM_TRAILING, &out_str);

  std::string::size_type pos = 0;
  while ((pos = out_str.find("\n", pos)) != std::string::npos) {
    out_str.replace(pos, 1, ("\n" + GetIndentStr(indent)));
    pos = pos + indent + 1;
  }
  return out_str;
}

void ParseDirsAndExecute(const base::FilePath& dir,
                         int indent,
                         char const* regex,
                         void (*func)(const base::FilePath&, int)) {
  base::FileEnumerator it(dir, false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath s_dir = it.Next(); !s_dir.empty(); s_dir = it.Next()) {
    if (RE2::FullMatch(s_dir.BaseName().value(), regex))
      func(s_dir, indent);
  }
}

void PrintFile(const base::FilePath& path, int indent) {
  if (!base::PathExists(path))
    return;

  std::string f_str;
  if (!base::ReadFileToString(path, &f_str))
    return;

  f_str = FormatString(f_str, indent);
  std::cout << GetIndentStr(indent) << path.BaseName().value() << ": " << f_str
            << std::endl;
}

void PrintDirFiles(const base::FilePath& dir, int indent) {
  std::cout << GetIndentStr(indent) << dir.BaseName().value() << std::endl;
  base::FileEnumerator it(dir, false, base::FileEnumerator::FILES);
  for (base::FilePath f = it.Next(); !f.empty(); f = it.Next())
    PrintFile(f, indent + 2);
}

PDRev GetPDRev(const base::FilePath& dir) {
  std::string pd_revision_str;
  if (!base::ReadFileToString(dir.Append("usb_power_delivery_revision"),
                              &pd_revision_str))
    return PDRev::kNone;

  if (pd_revision_str.length() < 3)
    return PDRev::kNone;

  if (pd_revision_str[0] == '2' && pd_revision_str[2] == '0') {
    return PDRev::kPD20;
  } else if (pd_revision_str[0] == '3' && pd_revision_str[2] == '0') {
    return PDRev::kPD30;
  } else if (pd_revision_str[0] == '3' && pd_revision_str[2] == '1') {
    return PDRev::kPD31;
  }
  return PDRev::kNone;
}

bool ReadVdo(const base::FilePath& path, uint32_t* vdo) {
  std::string str;
  if (!vdo || !base::ReadFileToString(path, &str))
    return false;

  base::TrimWhitespaceASCII(str, base::TRIM_TRAILING, &str);
  base::HexStringToUInt(str, vdo);
  return true;
}

void PrintVdo(const base::FilePath& vdo_file,
              const std::vector<VdoField> vdo_description,
              int indent) {
  uint32_t vdo;
  if (!ReadVdo(vdo_file, &vdo))
    return;

  std::cout << GetIndentStr(indent) << vdo_file.BaseName().value() << ": 0x"
            << std::hex << vdo << std::endl;

  for (auto field : vdo_description) {
    int field_val = (vdo & field.mask) >> field.index;
    std::cout << GetIndentStr(indent + 2) << field.description << ": 0x"
              << std::hex << field_val << std::endl;
  }
}

void PrintAltMode(const base::FilePath& alt_mode, int indent) {
  if (!base::DirectoryExists(alt_mode))
    return;

  PrintDirFiles(alt_mode, indent);
  ParseDirsAndExecute(alt_mode, indent + 2, kModeRegex, &PrintDirFiles);
}

void PrintPdos(const base::FilePath& pdo_path, int indent) {
  if (!base::DirectoryExists(pdo_path))
    return;

  PrintDirFiles(pdo_path, indent);
  ParseDirsAndExecute(pdo_path, indent + 2, kPdoCapabilitiesRegex,
                      &PrintPdoCapabilities);
}

void PrintPdoCapabilities(const base::FilePath& capabilities, int indent) {
  if (!base::DirectoryExists(capabilities))
    return;

  PrintDirFiles(capabilities, indent);
  ParseDirsAndExecute(capabilities, indent + 2, kPdoTypeRegex, &PrintDirFiles);
}

void PrintPlugInfo(const base::FilePath& plug, int indent) {
  if (!base::DirectoryExists(plug))
    return;

  PrintDirFiles(plug, indent);
  ParseDirsAndExecute(plug, indent + 2, kPlugAltModeRegex, &PrintAltMode);
}

ProductType GetPartnerProductType(const base::FilePath& dir) {
  PDRev pd_rev = GetPDRev(dir);

  uint32_t id_header;
  if (!ReadVdo(dir.Append("identity").Append("id_header"), &id_header))
    return ProductType::kOther;

  ProductType ret = ProductType::kOther;
  if (pd_rev == PDRev::kPD20) {
    // Alternate Mode Adapter (AMA) is the only partner product type in the
    // USB PD 2.0 specification
    if ((id_header & kPDUFPProductTypeMask) == kPD20AMAComp)
      return ProductType::kPD20AMA;
    else
      return ProductType::kOther;
  } else if (pd_rev == PDRev::kPD30) {
    // In USB PD 3.0 a partner can be an upstream facing port (UFP),
    // downstream facing port (DFP), or a dual-role data port (DRD).
    // Information about UFP/DFP are in different fields, so they are checked
    // separately then compared to determine a partner's product type.
    // Separate from UFP/DFP, they can support AMA/VPD as a UFP type.
    bool ufp_supported = false;
    if ((id_header & kPDUFPProductTypeMask) == kPD30HubComp)
      ufp_supported = true;
    else if ((id_header & kPDUFPProductTypeMask) == kPD30PeripheralComp)
      ufp_supported = true;
    else if ((id_header & kPDUFPProductTypeMask) == kPD30AMAComp)
      return ProductType::kPD30AMA;
    else if ((id_header & kPDUFPProductTypeMask) == kPD30VPDComp)
      return ProductType::kPD30VPD;

    bool dfp_supported = false;
    if ((id_header & kPDDFPProductTypeMask) == kPD30DFPHubComp)
      dfp_supported = true;
    else if ((id_header & kPDDFPProductTypeMask) == kPD30DFPHostComp)
      dfp_supported = true;
    else if ((id_header & kPDDFPProductTypeMask) == kPD30PowerBrickComp)
      dfp_supported = true;

    if (ufp_supported && dfp_supported)
      ret = ProductType::kPD30DRD;
    else if (ufp_supported)
      ret = ProductType::kPD30UFP;
    else if (dfp_supported)
      ret = ProductType::kPD30DFP;
  } else if (pd_rev == PDRev::kPD31) {
    // Similar to USB PD 3.0, USB PD 3.1 can have a partner which is both UFP
    // and DFP (DRD).
    bool ufp_supported = false;
    if ((id_header & kPDUFPProductTypeMask) == kPD31HubComp)
      ufp_supported = true;
    else if ((id_header & kPDUFPProductTypeMask) == kPD31PeripheralComp)
      ufp_supported = true;

    bool dfp_supported = false;
    if ((id_header & kPDDFPProductTypeMask) == kPD31DFPHubComp)
      dfp_supported = true;
    else if ((id_header & kPDDFPProductTypeMask) == kPD31DFPHostComp)
      dfp_supported = true;
    else if ((id_header & kPDDFPProductTypeMask) == kPD31PowerBrickComp)
      dfp_supported = true;

    if (ufp_supported && dfp_supported)
      ret = ProductType::kPD31DRD;
    else if (ufp_supported)
      ret = ProductType::kPD31UFP;
    else if (dfp_supported)
      ret = ProductType::kPD31DFP;
  }
  return ret;
}

ProductType GetCableProductType(const base::FilePath& dir) {
  PDRev pd_rev = GetPDRev(dir);

  uint32_t id_header;
  if (!ReadVdo(dir.Append("identity").Append("id_header"), &id_header))
    return ProductType::kOther;

  if (pd_rev == PDRev::kPD20) {
    // USB PD 2.0 only supports active and passive cables.
    if ((id_header & kPDUFPProductTypeMask) == kPD20PassiveCableComp)
      return ProductType::kPD20PassiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD20ActiveCableComp)
      return ProductType::kPD20ActiveCable;
    else
      return ProductType::kOther;
  } else if (pd_rev == PDRev::kPD30) {
    // USB PD 3.0 supports only active and passive cables.
    if ((id_header & kPDUFPProductTypeMask) == kPD30PassiveCableComp)
      return ProductType::kPD30PassiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD30ActiveCableComp)
      return ProductType::kPD30ActiveCable;
    else
      return ProductType::kOther;
  } else if (pd_rev == PDRev::kPD31) {
    // USB PD 3.1 supports active cables, passive cables and Vconn Powered
    // Devices (VPD) definitions from id_header.
    if ((id_header & kPDUFPProductTypeMask) == kPD31PassiveCableComp)
      return ProductType::kPD31PassiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD31ActiveCableComp)
      return ProductType::kPD31ActiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD31VPDComp)
      return ProductType::kPD31VPD;
    else
      return ProductType::kOther;
  } else {
    return ProductType::kOther;
  }
}

void PrintPartnerIdentity(const base::FilePath& partner, int indent) {
  auto identity = partner.Append("identity");
  if (!base::DirectoryExists(identity))
    return;

  std::cout << GetIndentStr(indent) << "identity" << std::endl;

  // Print*Identity function will print cert_stat, id_header and product
  // files. Then, check the product type to determine the vdo
  // descriptions for product_type_vdo[1,2,3].
  PDRev pd_rev = GetPDRev(partner);
  switch (pd_rev) {
    case PDRev::kPD20:
      PrintVdo(identity.Append("id_header"), kPD20IDHeaderVDO, indent + 2);
      PrintVdo(identity.Append("product"), kPD20ProductVDO, indent + 2);
      PrintVdo(identity.Append("cert_stat"), kPD20CertStatVDO, indent + 2);
      break;
    case PDRev::kPD30:
      PrintVdo(identity.Append("id_header"), kPD30IDHeaderVDO, indent + 2);
      PrintVdo(identity.Append("product"), kPD30ProductVDO, indent + 2);
      PrintVdo(identity.Append("cert_stat"), kPD30CertStatVDO, indent + 2);
      break;
    case PDRev::kPD31:
      PrintVdo(identity.Append("id_header"), kPD31IDHeaderVDO, indent + 2);
      PrintVdo(identity.Append("product"), kPD31ProductVDO, indent + 2);
      PrintVdo(identity.Append("cert_stat"), kPD31CertStatVDO, indent + 2);
      break;
    default:
      PrintVdo(identity.Append("id_header"), {}, indent + 2);
      PrintVdo(identity.Append("product"), {}, indent + 2);
      PrintVdo(identity.Append("cert_stat"), {}, indent + 2);
      break;
  }

  ProductType product_type = GetPartnerProductType(partner);
  switch (product_type) {
    case ProductType::kPD20AMA:
      PrintVdo(identity.Append("product_type_vdo1"), kPD20AMAVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30VPD:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30VPDVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30AMA:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30AMAVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30UFP:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30UFPVDO1, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), kPD30UFPVDO2, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30DFP:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30DFPVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30DRD:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30UFPVDO1, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), kPD30UFPVDO2, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), kPD30DFPVDO, indent + 2);
      break;
    case ProductType::kPD31UFP:
      PrintVdo(identity.Append("product_type_vdo1"), kPD31UFPVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD31DFP:
      PrintVdo(identity.Append("product_type_vdo1"), kPD31DFPVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD31DRD:
      PrintVdo(identity.Append("product_type_vdo1"), kPD31UFPVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), kPD31DFPVDO, indent + 2);
      break;
    default:
      PrintVdo(identity.Append("product_type_vdo1"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
  }
}

void PrintCableIdentity(const base::FilePath& cable, int indent) {
  auto identity = cable.Append("identity");
  if (!base::DirectoryExists(identity))
    return;

  std::cout << GetIndentStr(indent) << "identity" << std::endl;

  PDRev pd_rev = GetPDRev(cable);
  switch (pd_rev) {
    case PDRev::kPD20:
      PrintVdo(identity.Append("id_header"), kPD20IDHeaderVDO, indent + 2);
      PrintVdo(identity.Append("product"), kPD20ProductVDO, indent + 2);
      PrintVdo(identity.Append("cert_stat"), kPD20CertStatVDO, indent + 2);
      break;
    case PDRev::kPD30:
      PrintVdo(identity.Append("id_header"), kPD30IDHeaderVDO, indent + 2);
      PrintVdo(identity.Append("product"), kPD30ProductVDO, indent + 2);
      PrintVdo(identity.Append("cert_stat"), kPD30CertStatVDO, indent + 2);
      break;
    case PDRev::kPD31:
      PrintVdo(identity.Append("id_header"), kPD31IDHeaderVDO, indent + 2);
      PrintVdo(identity.Append("product"), kPD31ProductVDO, indent + 2);
      PrintVdo(identity.Append("cert_stat"), kPD31CertStatVDO, indent + 2);
      break;
    default:
      PrintVdo(identity.Append("id_header"), {}, indent + 2);
      PrintVdo(identity.Append("product"), {}, indent + 2);
      PrintVdo(identity.Append("cert_stat"), {}, indent + 2);
      break;
  }

  ProductType product_type = GetCableProductType(cable);
  switch (product_type) {
    case ProductType::kPD20PassiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD20PassiveVDO,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD20ActiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD20ActiveVDO,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30PassiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30PassiveVDO,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD30ActiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD30ActiveVDO1,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), kPD30ActiveVDO2,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD31PassiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD31PassiveVDO,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD31ActiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD31ActiveVDO1,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), kPD31ActiveVDO2,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    case ProductType::kPD31VPD:
      PrintVdo(identity.Append("product_type_vdo1"), kPD31VPDVDO, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
    default:
      PrintVdo(identity.Append("product_type_vdo1"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, indent + 2);
      break;
  }
}

void PrintPartner(const base::FilePath& port, int indent) {
  auto partner_dir = port.Append(port.BaseName().value() + "-partner");
  if (!base::DirectoryExists(partner_dir))
    return;

  PrintDirFiles(partner_dir, indent);
  PrintPartnerIdentity(partner_dir, indent + 2);
  ParseDirsAndExecute(partner_dir, indent + 2, kPartnerAltModeRegex,
                      &PrintAltMode);
  ParseDirsAndExecute(partner_dir, indent + 2, kPartnerPdoRegex, &PrintPdos);
}

void PrintCable(const base::FilePath& port, int indent) {
  auto cable_dir = port.Append(port.BaseName().value() + "-cable");
  if (!base::DirectoryExists(cable_dir))
    return;

  PrintDirFiles(cable_dir, indent);
  PrintCableIdentity(cable_dir, indent + 2);
  ParseDirsAndExecute(cable_dir, indent + 2, kPlugRegex, &PrintPlugInfo);
}

void PrintPhysicalLocation(const base::FilePath& port, int indent) {
  auto physical_location_dir = port.Append("physical_location");
  if (!base::DirectoryExists(physical_location_dir))
    return;

  std::cout << GetIndentStr(indent) << "physical_location" << std::endl;
  PrintFile(physical_location_dir.Append("panel"), indent + 2);
  PrintFile(physical_location_dir.Append("horizontal_position"), indent + 2);
}

void PrintUsbDeviceInfo(const base::FilePath& usb_device, int indent) {
  std::cout << GetIndentStr(indent) << "usb_device" << std::endl;
  PrintFile(usb_device.Append("busnum"), indent + 2);
  PrintFile(usb_device.Append("devnum"), indent + 2);
  PrintFile(usb_device.Append("devpath"), indent + 2);
  ParseDirsAndExecute(usb_device, indent + 2, kUsbDeviceRegex,
                      &PrintUsbDeviceInfo);
}

void PrintUsbDevice(const base::FilePath& usb_port, int indent) {
  auto usb_device_dir = usb_port.Append("device");
  if (!base::DirectoryExists(usb_device_dir))
    return;

  PrintUsbDeviceInfo(usb_device_dir, indent);
}

void PrintUsbSubsystem(const base::FilePath& port, int indent) {
  ParseDirsAndExecute(port, indent, kUsbPortRegex, &PrintUsbDevice);
}

void PrintDrmSubsystem(const base::FilePath& port, int indent) {
  auto drm_connector_dir = port.Append("drm_connector");
  if (!base::DirectoryExists(drm_connector_dir))
    return;

  std::cout << GetIndentStr(indent) << "dp_connector" << std::endl;
  PrintFile(drm_connector_dir.Append("connector_id"), indent + 2);
}

void PrintPortInfo(const base::FilePath& port, int indent) {
  PrintDirFiles(port, indent);
  PrintPartner(port, indent + 2);
  PrintCable(port, indent + 2);
  PrintPhysicalLocation(port, indent + 2);
  PrintUsbSubsystem(port, indent + 2);
  PrintDrmSubsystem(port, indent + 2);
  std::cout << std::endl;
}

}  // namespace typec_connector_class_helper
}  // namespace debugd
