// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

namespace {

struct VdoField {
  int index;
  uint32_t mask;
  std::string description;
};

enum ProductType {
  kOther = 0,
  kPD2PassiveCable = 1,
  kPD2ActiveCable = 2,
  kPD2AMA = 3,
  kPD3PassiveCable = 4,
  kPD3ActiveCable = 5,
  kPD3VPD = 6,
  kPD3UFP = 7,
  kPD3DFP = 8,
  kPD3DRD = 9,
};

constexpr char kTypecSysfs[] = "/sys/class/typec";
constexpr char kPortRegex[] = "port[0-9]+$";
constexpr char kPartnerAltModeRegex[] = "port[0-9]+-partner\\.[0-9]+$";
constexpr char kModeRegex[] = "mode[0-9]+$";
constexpr char kPlugRegex[] = "port[0-9]+\\-plug[0-9]+$";
constexpr char kPlugAltModeRegex[] = "port[0-9]+\\-plug[0-9]+\\.[0-9]+$";

// Masks for id_header fields.
constexpr uint32_t kPDUFPProductTypeMask = 0x38000000;
constexpr uint32_t kPDDFPProductTypeMask = 0x03800000;

// Expected id_header field results.
constexpr uint32_t kPD2PassiveCableComp = 0x20000000;
constexpr uint32_t kPD2ActiveCableComp = 0x18000000;
constexpr uint32_t kPD2AMAComp = 0x28000000;
constexpr uint32_t kPD3PassiveCableComp = 0x18000000;
constexpr uint32_t kPD3ActiveCableComp = 0x20000000;
constexpr uint32_t kPD3VPDComp = 0x30000000;
constexpr uint32_t kPD3HubComp = 0x08000000;
constexpr uint32_t kPD3PeripheralComp = 0x10000000;
constexpr uint32_t kPD3PSDComp = 0x18000000;
constexpr uint32_t kPD3DFPHubComp = 0x00800000;
constexpr uint32_t kPD3DFPHostComp = 0x01000000;
constexpr uint32_t kPD3PowerBrickComp = 0x01800000;

// VDO descriptions from the USB PD Revision 2.0 and 3.1 specifications.
const std::vector<VdoField> kCertStatVDO = {{0, 0xffffffff, "XID"}};

const std::vector<VdoField> kIDHeaderVDO = {{0, 0x0000ffff, "Vendor ID"}};

const std::vector<VdoField> kProductVDO = {{16, 0xffff0000, "Product ID"}};

const std::vector<VdoField> kPD2PassiveVDO = {
    {0, 0x00000007, "USB Speed"},
    {3, 0x00000008, "Reserved"},
    {4, 0x00000010, "Vbus Through Cable"},
    {5, 0x00000060, "Vbus Current Handling"},
    {7, 0x00000080, "SSRX2 Directionality Support"},
    {8, 0x00000100, "SSRX1 Directionality Support"},
    {9, 0x00000200, "SSTX2 Directionality Support"},
    {10, 0x00000400, "SSTX1 Directionality Support"},
    {11, 0x00001800, "Cable Termination Type"},
    {13, 0x0001e000, "Cable Latency"},
    {17, 0x00020000, "Reserved"},
    {18, 0x000c0000, "USB Type-C Plug to USB Type"},
    {20, 0x00f00000, "Reserved"},
    {24, 0x0f000000, "Firmware Version"},
    {28, 0xf0000000, "HW Version"},
};

const std::vector<VdoField> kPD2ActiveVDO = {
    {0, 0x00000007, "USB Speed"},
    {3, 0x00000008, "SOP'' Controller Present"},
    {4, 0x00000010, "Vbus Through Cable"},
    {5, 0x00000060, "Vbus Current Handling"},
    {7, 0x00000080, "SSRX2 Directionality Support"},
    {8, 0x00000100, "SSRX1 Directionality Support"},
    {9, 0x00000200, "SSTX2 Directionality Support"},
    {10, 0x00000400, "SSTX1 Directionality Support"},
    {11, 0x00001800, "Cable Termination Type"},
    {13, 0x0001e000, "Cable Latency"},
    {17, 0x00020000, "Reserved"},
    {18, 0x000c0000, "USB Type-C Plug to USB Type"},
    {20, 0x00f00000, "Reserved"},
    {24, 0x0f000000, "Firmware Version"},
    {28, 0xf0000000, "HW Version"},
};

const std::vector<VdoField> kPD2AMAVDO = {
    {0, 0x00000007, "USB SS Signaling Support"},
    {3, 0x00000008, "Vbus Required"},
    {4, 0x00000010, "Vconn Required"},
    {5, 0x000000e0, "Vconn Power"},
    {8, 0x00000100, "SSRX2 Directionality Support"},
    {9, 0x00000200, "SSRX1 Directionality Support"},
    {10, 0x00000400, "SSTX2 Directionality Support"},
    {11, 0x00000800, "SSTX1 Directionality Support"},
    {12, 0x00fff000, "Resrved"},
    {24, 0x0f000000, "Firmware Version"},
    {28, 0xf0000000, "Hardware Version"},
};

const std::vector<VdoField> kPD3PassiveVDO = {
    {0, 0x00000007, "USB Speed"},
    {3, 0x00000018, "Reserved"},
    {5, 0x00000060, "Vbus Current Handling"},
    {7, 0x00000180, "Reserved"},
    {9, 0x00000600, "Maximum Vbus Voltage"},
    {11, 0x00001800, "Cable Termination Type"},
    {13, 0x0001e000, "Cable Latency"},
    {17, 0x00020000, "EPR Mode Cable"},
    {18, 0x000c0000, "USB Type-C Plug to USB Type"},
    {20, 0x00100000, "Reserved"},
    {21, 0x00e00000, "VDO Version"},
    {24, 0x0f000000, "Firmware Version"},
    {28, 0xf0000000, "HW Version"},
};

const std::vector<VdoField> kPD3ActiveVDO1 = {
    {0, 0x00000007, "USB Speed"},
    {3, 0x00000008, "SOP'' Controller Present"},
    {4, 0x00000010, "Vbus Through Cable"},
    {5, 0x00000060, "Vbus Current Handling"},
    {7, 0x00000080, "SBU Type"},
    {8, 0x00000100, "SBU Supported"},
    {9, 0x00000600, "Maximum Vbus Voltage"},
    {11, 0x00001800, "Cable Termination Type"},
    {13, 0x0001e000, "Cable Latency"},
    {17, 0x00020000, "EPR Mode Cable"},
    {18, 0x000c0000, "USB Type-C Plug to USB Type"},
    {20, 0x00100000, "Reserved"},
    {21, 0x00e00000, "VDO Version"},
    {24, 0x0f000000, "Firmware Version"},
    {28, 0xf0000000, "HW Version"},
};

const std::vector<VdoField> kPD3ActiveVDO2 = {
    {0, 0x00000001, "USB Gen"},
    {1, 0x00000002, "Reserved"},
    {2, 0x00000004, "Optically Insulated Active Cable"},
    {3, 0x00000008, "USB Lanes Supported"},
    {4, 0x00000010, "USB 3.2 Supported"},
    {5, 0x00000020, "USB 2.0 Supported"},
    {6, 0x000000c00, "USB 2.0 Hub Hops Command"},
    {8, 0x00000100, "USB4 Supported"},
    {9, 0x00000200, "Active Element"},
    {10, 0x00000400, "Physical Connection"},
    {11, 0x00000800, "U3 to U0 Transition Mode"},
    {12, 0x00007000, "U3/CLd Power"},
    {15, 0x00008000, "Reserved"},
    {16, 0x00ff0000, "Shutdown Tempurature"},
    {24, 0xff000000, "Max Operating Tempurature"},
};

const std::vector<VdoField> kPD3VPDVDO = {
    {0, 0x00000001, "Charge Through Support"},
    {1, 0x0000007e, "Ground Impedance"},
    {7, 0x00001f80, "Vbus Impedance"},
    {13, 0x00002000, "Reserved"},
    {14, 0x00004000, "Charge Through Current Support"},
    {15, 0x00018000, "Maximum Vbus Voltage"},
    {17, 0x001e0000, "Reserved"},
    {21, 0x00e00000, "VDO Version"},
    {24, 0x0f000000, "Firmware Version"},
    {28, 0xf0000000, "HW Version"},
};

const std::vector<VdoField> kPD3UFPVDO = {
    {0, 0x00000007, "USB Highest Speed"},
    {3, 0x00000038, "Alternate Modes"},
    {6, 0x00000040, "Vbus Required"},
    {7, 0x00000080, "Vconn Required"},
    {8, 0x00000700, "Vconn Power"},
    {11, 0x003ff800, "Reserved"},
    {22, 0x00c00000, "Connector Type (Legacy)"},
    {24, 0x0f000000, "Device Capability"},
    {28, 0x10000000, "Reserved"},
    {29, 0xe0000000, "UFP VDO Version"},
};

const std::vector<VdoField> kPD3DFPVDO = {
    {0, 0x0000001f, "Port Number"},
    {5, 0x003fffe0, "Reserved"},
    {22, 0x00c00000, "Connector Type (Legacy)"},
    {24, 0x07000000, "Host Capability"},
    {27, 0x18000000, "Reserved"},
    {29, 0xe0000000, "DFP VDO Version"},
};

// GetIndentStr returns a string to be used as an indent based on the
// provided "indent" input.
std::string GetIndentStr(int indent) {
  return std::string(indent, ' ');
}

// FormatString will remove trailing whitespace and add an indent to any new
// lines.
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

// ParseDirsAndExecute will look at subdirectories of a given directory and
// execute a passed function on directories matching a given regular expression.
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

// PrintFile will print a file's contents in a "name: content" format and also
// add indentations to multiline strings.
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

// PrintDirFiles will print all files in a directory in a "name: content"
// format.
void PrintDirFiles(const base::FilePath& dir, int indent) {
  std::cout << GetIndentStr(indent) << dir.BaseName().value() << std::endl;
  base::FileEnumerator it(dir, false, base::FileEnumerator::FILES);
  for (base::FilePath f = it.Next(); !f.empty(); f = it.Next())
    PrintFile(f, indent + 2);
}

// ReadVdo reads a file containing a 32 bit VDO value and loads it into a
// uint32_t pointer. It will return true if the file read is successful and
// false otherwise.
bool ReadVdo(const base::FilePath& path, uint32_t* vdo) {
  std::string str;
  if (!vdo || !base::ReadFileToString(path, &str))
    return false;

  base::TrimWhitespaceASCII(str, base::TRIM_TRAILING, &str);
  base::HexStringToUInt(str, vdo);
  return true;
}

// PrintVdo reads a vdo value from a text file and converts it to a uint32_t
// variable then prints out the values of each field according to the
// vdo_description. If hide_data is set, the full vdo will not be printed to
// obfuscate user information.
void PrintVdo(const base::FilePath& vdo_file,
              const std::vector<VdoField> vdo_description,
              bool hide_data,
              int indent) {
  uint32_t vdo;
  if (!ReadVdo(vdo_file, &vdo))
    return;

  if (hide_data)
    std::cout << GetIndentStr(indent) << vdo_file.BaseName().value()
              << std::endl;
  else
    std::cout << GetIndentStr(indent) << vdo_file.BaseName().value() << ": 0x"
              << std::hex << vdo << std::endl;

  for (auto field : vdo_description) {
    int field_val = (vdo & field.mask) >> field.index;
    std::cout << GetIndentStr(indent + 2) << field.description << ": 0x"
              << std::hex << field_val << std::endl;
  }
}

// PrintAltMode will print the immediate files in an alternate mode directory,
// then print the files in a mode subdirectory.
void PrintAltMode(const base::FilePath& alt_mode, int indent) {
  if (!base::DirectoryExists(alt_mode))
    return;

  PrintDirFiles(alt_mode, indent);
  ParseDirsAndExecute(alt_mode, indent + 2, kModeRegex, &PrintDirFiles);
}

// PrintPlugInfo will print the immediate files in an plug directory, then print
// the files in an alternate mode directory.
void PrintPlugInfo(const base::FilePath& plug, int indent) {
  if (!base::DirectoryExists(plug))
    return;

  PrintDirFiles(plug, indent);
  ParseDirsAndExecute(plug, indent + 2, kPlugAltModeRegex, &PrintAltMode);
}

// GetPartnerProductType will look at the id_header VDO and USB PD revision to
// decode what type of device is being parsed.
ProductType GetPartnerProductType(const base::FilePath& dir) {
  std::string pd_revision_str;
  if (!base::ReadFileToString(dir.Append("usb_power_delivery_revision"),
                              &pd_revision_str))
    return ProductType::kOther;

  if (pd_revision_str.empty())
    return ProductType::kOther;

  uint32_t id_header;
  if (!ReadVdo(dir.Append("identity").Append("id_header"), &id_header))
    return ProductType::kOther;

  ProductType ret = ProductType::kOther;
  if (pd_revision_str[0] == '2') {
    // Alternate Mode Adapter (AMA) is the only partner product type in the
    // USB PD 2.0 specification
    if ((id_header & kPDUFPProductTypeMask) == kPD2AMAComp)
      return ProductType::kPD2AMA;
    else
      return ProductType::kOther;
  } else if (pd_revision_str[0] == '3') {
    // In USB PD 3.0 a partner can be an upstream facing port (UFP),
    // downstream facing port (DFP), or a dual-role data port (DRD).
    // Information about UFP/DFP are in different fields, so they are checked
    // separately then compared to determine a partner's product type.

    bool ufp_supported = false;
    if ((id_header & kPDUFPProductTypeMask) == kPD3HubComp)
      ufp_supported = true;
    else if ((id_header & kPDUFPProductTypeMask) == kPD3PeripheralComp)
      ufp_supported = true;
    else if ((id_header & kPDUFPProductTypeMask) == kPD3PSDComp)
      ufp_supported = true;

    bool dfp_supported = false;
    if ((id_header & kPDDFPProductTypeMask) == kPD3DFPHubComp)
      dfp_supported = true;
    else if ((id_header & kPDDFPProductTypeMask) == kPD3DFPHostComp)
      dfp_supported = true;
    else if ((id_header & kPDDFPProductTypeMask) == kPD3PowerBrickComp)
      dfp_supported = true;

    if (ufp_supported && dfp_supported)
      ret = ProductType::kPD3DRD;
    else if (ufp_supported)
      ret = ProductType::kPD3UFP;
    else if (dfp_supported)
      ret = ProductType::kPD3DFP;
  }
  return ret;
}

// Similar to GetPartnerProductType, GetCableProductType will use the USB PD
// revision and id_header VDO to determine which type of cable is being used.
ProductType GetCableProductType(const base::FilePath& dir) {
  std::string pd_revision_str;
  if (!base::ReadFileToString(dir.Append("usb_power_delivery_revision"),
                              &pd_revision_str))
    return ProductType::kOther;

  if (pd_revision_str.empty())
    return ProductType::kOther;

  uint32_t id_header;
  if (!ReadVdo(dir.Append("identity").Append("id_header"), &id_header))
    return ProductType::kOther;

  if (pd_revision_str[0] == '2') {
    // USB PD 2.0 only supports active and passive cables.
    if ((id_header & kPDUFPProductTypeMask) == kPD2PassiveCableComp)
      return ProductType::kPD2PassiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD2ActiveCableComp)
      return ProductType::kPD2ActiveCable;
    else
      return ProductType::kOther;
  } else if (pd_revision_str[0] == '3') {
    // USB PD 3.0 supports active cables, passive cables and Vconn Powered
    // Devices (VPD) definitions from id_header.
    if ((id_header & kPDUFPProductTypeMask) == kPD3PassiveCableComp)
      return ProductType::kPD3PassiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD3ActiveCableComp)
      return ProductType::kPD3ActiveCable;
    else if ((id_header & kPDUFPProductTypeMask) == kPD3VPDComp)
      return ProductType::kPD3VPD;
    else
      return ProductType::kOther;
  } else {
    return ProductType::kOther;
  }
}

// PrintPartnerIdentity prints the contents of an identity directory including
// VDO fields which are determined by product type.
void PrintPartnerIdentity(const base::FilePath& partner, int indent) {
  auto identity = partner.Append("identity");
  if (!base::DirectoryExists(identity))
    return;

  std::cout << GetIndentStr(indent) << "identity" << std::endl;

  // Print*Identity function will print cert_stat, id_header and product
  // files. Then, check the product type to determine the vdo
  // descriptions for product_type_vdo[1,2,3].
  PrintVdo(identity.Append("cert_stat"), kCertStatVDO, true, indent + 2);
  PrintVdo(identity.Append("id_header"), kIDHeaderVDO, true, indent + 2);
  PrintVdo(identity.Append("product"), kProductVDO, true, indent + 2);

  ProductType product_type = GetPartnerProductType(partner);
  switch (product_type) {
    case ProductType::kPD2AMA:
      PrintVdo(identity.Append("product_type_vdo1"), kPD2AMAVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD3UFP:
      PrintVdo(identity.Append("product_type_vdo1"), kPD3UFPVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD3DFP:
      PrintVdo(identity.Append("product_type_vdo1"), kPD3DFPVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD3DRD:
      PrintVdo(identity.Append("product_type_vdo1"), kPD3UFPVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), kPD3DFPVDO, false,
               indent + 2);
      break;
    default:
      PrintVdo(identity.Append("product_type_vdo1"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
  }
}

// Similar to PrintPartnerIdentity, PrintCableIdentity will display the contents
// of the identity directory for a cable including VDO fields.
void PrintCableIdentity(const base::FilePath& cable, int indent) {
  auto identity = cable.Append("identity");
  if (!base::DirectoryExists(identity))
    return;

  std::cout << GetIndentStr(indent) << "identity" << std::endl;

  PrintVdo(identity.Append("cert_stat"), kCertStatVDO, true, indent + 2);
  PrintVdo(identity.Append("id_header"), kIDHeaderVDO, true, indent + 2);
  PrintVdo(identity.Append("product"), kProductVDO, true, indent + 2);

  ProductType product_type = GetCableProductType(cable);
  switch (product_type) {
    case ProductType::kPD2PassiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD2PassiveVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD2ActiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD2ActiveVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD3PassiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD3PassiveVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD3ActiveCable:
      PrintVdo(identity.Append("product_type_vdo1"), kPD3ActiveVDO1, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), kPD3ActiveVDO2, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    case ProductType::kPD3VPD:
      PrintVdo(identity.Append("product_type_vdo1"), kPD3VPDVDO, false,
               indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
    default:
      PrintVdo(identity.Append("product_type_vdo1"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo2"), {}, false, indent + 2);
      PrintVdo(identity.Append("product_type_vdo3"), {}, false, indent + 2);
      break;
  }
}

// PrintPartner will print the immediate information in the partner directory,
// then print the identity and alternate mode information.
void PrintPartner(const base::FilePath& port, int indent) {
  auto partner_dir = port.Append(port.BaseName().value() + "-partner");
  if (!base::DirectoryExists(partner_dir))
    return;

  PrintDirFiles(partner_dir, indent);
  PrintPartnerIdentity(partner_dir, indent + 2);
  ParseDirsAndExecute(partner_dir, indent + 2, kPartnerAltModeRegex,
                      &PrintAltMode);
}

// PrintCable will print the immediate information in the cable directory,
// then print the identity and alternate mode information.
void PrintCable(const base::FilePath& port, int indent) {
  auto cable_dir = port.Append(port.BaseName().value() + "-cable");
  if (!base::DirectoryExists(cable_dir))
    return;

  PrintDirFiles(cable_dir, indent);
  PrintCableIdentity(cable_dir, indent + 2);
  ParseDirsAndExecute(cable_dir, indent + 2, kPlugRegex, &PrintPlugInfo);
}

// PrintPortInfo will print relevant type-c connector class information for the
// port located at the sysfs path "port"
void PrintPortInfo(const base::FilePath& port, int indent) {
  PrintDirFiles(port, indent);
  PrintPartner(port, indent + 2);
  PrintCable(port, indent + 2);
  std::cout << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 1) {
    std::cout
        << "typec_connector_class_helper.cc does not accept any arguements."
        << std::endl;
    return 1;
  }

  if (!base::PathExists(base::FilePath(kTypecSysfs)))
    return 1;

  ParseDirsAndExecute(base::FilePath(kTypecSysfs), 0, kPortRegex,
                      &PrintPortInfo);
  return EXIT_SUCCESS;
}
