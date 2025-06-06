// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/bluetooth_devcd_parser_util.h"

#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "crash-reporter/udev_bluetooth_util.h"
#include "crash-reporter/util.h"

namespace {

enum class ParseErrorReason {
  kErrorFileIO,
  kErrorEventHeaderParsing,
  kErrorTlvParsing,
  kErrorDataLength,
  kErrorEventDataParsing,
};

std::string CreateDumpEntry(const std::string& key, const std::string& value);
bool ReportDefaultPC(base::File& file, std::string* pc);
bool ReportParseError(ParseErrorReason error_code, base::File& file);

}  // namespace

namespace vendor {

namespace intel {

// More information about Intel telemetry spec: go/cros-bt-intel-telemetry

constexpr char kVendorName[] = "Intel";
constexpr int kAddrLen = 4;
constexpr uint8_t kDebugCode = 0xFF;

// Possible values for TlvHeader::type
enum TlvTypeId {
  kTlvExcType = 0x01,
  kTlvLineNum = 0x02,
  kTlvModule = 0x03,
  kTlvErrorId = 0x04,
  kTlvBacktrace = 0x05,
  kTlvAuxReg = 0x06,
  kTlvSubType = 0x07,
};

struct EventHeader {
  uint8_t code;
  uint8_t len;
  uint8_t prefix[3];
} __attribute__((packed));

// The telemetry data is written as a series of Type-Length-Value triplets.
// Each record starts with a TlvHeader giving the Type and Length, followed by
// a Value. The value maps to one of the structures below; the |type| field
// tells us which one.
struct TlvHeader {
  uint8_t type;
  uint8_t len;
} __attribute__((packed));

struct TlvExcType {
  uint8_t val;
} __attribute__((packed));

struct TlvLineNum {
  uint16_t val;
} __attribute__((packed));

struct TlvModule {
  uint8_t val;
} __attribute__((packed));

struct TlvErrorId {
  uint8_t val;
} __attribute__((packed));

struct TlvBacktrace {
  uint8_t val[5][kAddrLen];
} __attribute__((packed));

struct TlvAuxReg {
  uint8_t val[4][kAddrLen];
} __attribute__((packed));

struct TlvAuxRegExt {
  uint8_t val[7][kAddrLen];
} __attribute__((packed));

struct TlvSubType {
  uint8_t val;
} __attribute__((packed));

bool ParseEventHeader(base::File& file, int* data_len, std::string* line) {
  struct EventHeader evt_header;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&evt_header),
                              sizeof(evt_header));
  if (ret < sizeof(evt_header)) {
    LOG(WARNING) << "Error reading Intel devcoredump Event Header";
    return false;
  }

  *line = CreateDumpEntry("Intel Event Header",
                          base::HexEncode(&evt_header, sizeof(evt_header)));

  if (evt_header.code != kDebugCode) {
    LOG(WARNING) << "Incorrect Intel devcoredump debug code";
    return false;
  }

  if (evt_header.len <= sizeof(evt_header.prefix)) {
    LOG(WARNING) << "Incorrect Intel devcoredump data length";
    return false;
  }

  *data_len = evt_header.len - sizeof(evt_header.prefix);

  return true;
}

bool VerifyTlvLength(struct TlvHeader& tlv_header) {
  switch (tlv_header.type) {
    case kTlvExcType:
      return tlv_header.len == sizeof(struct TlvExcType);
    case kTlvLineNum:
      return tlv_header.len == sizeof(struct TlvLineNum);
    case kTlvModule:
      return tlv_header.len == sizeof(struct TlvModule);
    case kTlvErrorId:
      return tlv_header.len == sizeof(struct TlvErrorId);
    case kTlvBacktrace:
      return tlv_header.len == sizeof(struct TlvBacktrace);
    case kTlvAuxReg:
      return tlv_header.len == sizeof(struct TlvAuxReg) ||
             tlv_header.len == sizeof(struct TlvAuxRegExt);
    case kTlvSubType:
      return tlv_header.len == sizeof(struct TlvSubType);
    default:
      // There may be other, unknown types in the data stream. Assume they have
      // the correct length since we don't understand them.
      return true;
  }
}

bool ParseTlvHeader(base::File& file, int* tlv_type, int* tlv_len) {
  struct TlvHeader tlv_header;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&tlv_header),
                              sizeof(tlv_header));
  if (ret < sizeof(tlv_header)) {
    LOG(WARNING) << "Error reading Intel devcoredump TLV Header";
    return false;
  }

  *tlv_type = tlv_header.type;
  *tlv_len = tlv_header.len;

  if (!VerifyTlvLength(tlv_header)) {
    LOG(WARNING) << "Incorrect TLV length " << tlv_header.len
                 << " for TLV type " << tlv_header.type;
    return false;
  }

  return true;
}

bool ParseExceptionType(base::File& file, std::string* line) {
  struct TlvExcType exc_type;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&exc_type),
                              sizeof(exc_type));
  if (ret < sizeof(exc_type)) {
    LOG(WARNING) << "Error reading Intel devcoredump Exception Type";
    return false;
  }

  *line = CreateDumpEntry("Exception Type",
                          base::HexEncode(&exc_type, sizeof(exc_type)));
  return true;
}

bool ParseLineNumber(base::File& file, std::string* line) {
  struct TlvLineNum line_num;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&line_num),
                              sizeof(line_num));
  if (ret < sizeof(line_num)) {
    LOG(WARNING) << "Error reading Intel devcoredump Line Number";
    return false;
  }

  *line = CreateDumpEntry("Line Number",
                          base::HexEncode(&line_num, sizeof(line_num)));
  return true;
}

bool ParseModuleNumber(base::File& file, std::string* line) {
  struct TlvModule module_num;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&module_num),
                              sizeof(module_num));
  if (ret < sizeof(module_num)) {
    LOG(WARNING) << "Error reading Intel devcoredump Module Number";
    return false;
  }

  *line = CreateDumpEntry("Module Number",
                          base::HexEncode(&module_num, sizeof(module_num)));
  return true;
}

bool ParseErrorId(base::File& file, std::string* line) {
  struct TlvErrorId error_id;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&error_id),
                              sizeof(error_id));
  if (ret < sizeof(error_id)) {
    LOG(WARNING) << "Error reading Intel devcoredump Error Id";
    return false;
  }

  *line =
      CreateDumpEntry("Error Id", base::HexEncode(&error_id, sizeof(error_id)));
  return true;
}

bool ParseBacktrace(base::File& file, std::string* line) {
  struct TlvBacktrace trace;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&trace), sizeof(trace));
  if (ret < sizeof(trace)) {
    LOG(WARNING) << "Error reading Intel devcoredump Call Backtrace";
    return false;
  }

  std::string traces;
  for (auto& val : trace.val) {
    base::StrAppend(&traces, {base::HexEncode(&val, kAddrLen), " "});
  }
  traces.pop_back();  // remove trailing whitespace.

  *line = CreateDumpEntry("Call Backtrace", traces);
  return true;
}

bool ParseAuxRegisters(base::File& file, std::string* pc, std::string* line) {
  struct TlvAuxReg reg;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&reg), sizeof(reg));
  if (ret < sizeof(reg)) {
    LOG(WARNING) << "Error reading Intel devcoredump Aux Registers";
    return false;
  }

  *pc = base::HexEncode(&reg.val[1], kAddrLen);
  *line = base::StrCat(
      {CreateDumpEntry("CPSR", base::HexEncode(&reg.val[0], kAddrLen)),
       CreateDumpEntry("PC", base::HexEncode(&reg.val[1], kAddrLen)),
       CreateDumpEntry("SP", base::HexEncode(&reg.val[2], kAddrLen)),
       CreateDumpEntry("BLINK", base::HexEncode(&reg.val[3], kAddrLen))});
  return true;
}

bool ParseAuxRegistersExtended(base::File& file,
                               std::string* pc,
                               std::string* line) {
  struct TlvAuxRegExt reg;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&reg), sizeof(reg));
  if (ret < sizeof(reg)) {
    LOG(WARNING) << "Error reading Intel devcoredump Aux Registers";
    return false;
  }

  *pc = base::HexEncode(&reg.val[1], kAddrLen);
  *line = base::StrCat(
      {CreateDumpEntry("BLINK", base::HexEncode(&reg.val[0], kAddrLen)),
       CreateDumpEntry("PC", base::HexEncode(&reg.val[1], kAddrLen)),
       CreateDumpEntry("ERSTATUS", base::HexEncode(&reg.val[2], kAddrLen)),
       CreateDumpEntry("ECR", base::HexEncode(&reg.val[3], kAddrLen)),
       CreateDumpEntry("EFA", base::HexEncode(&reg.val[4], kAddrLen)),
       CreateDumpEntry("IRQ", base::HexEncode(&reg.val[5], kAddrLen)),
       CreateDumpEntry("ICAUSE", base::HexEncode(&reg.val[6], kAddrLen))});
  return true;
}

bool ParseExceptionSubtype(base::File& file, std::string* line) {
  struct TlvSubType sub_type;
  int ret;

  ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&sub_type),
                              sizeof(sub_type));
  if (ret < sizeof(sub_type)) {
    LOG(WARNING) << "Error reading Intel devcoredump Exception Subtype";
    return false;
  }

  *line = CreateDumpEntry("Exception Subtype",
                          base::HexEncode(&sub_type, sizeof(sub_type)));
  return true;
}

bool ParseIntelDump(const base::FilePath& coredump_path,
                    const base::FilePath& target_path,
                    const int64_t dump_start,
                    std::string* pc) {
  base::File dump_file(coredump_path,
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  base::File target_file(target_path,
                         base::File::FLAG_OPEN | base::File::FLAG_APPEND);

  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  if (!dump_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << coredump_path << " Error: "
               << base::File::ErrorToString(dump_file.error_details());
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  if (dump_file.Seek(base::File::FROM_BEGIN, dump_start) == -1) {
    PLOG(ERROR) << "Error seeking file " << coredump_path;
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  std::string line;
  int data_len;
  bool ret = ParseEventHeader(dump_file, &data_len, &line);

  // Always report the event header whenever available, even if parsing fails.
  if (!line.empty() &&
      !target_file.WriteAtCurrentPosAndCheck(base::as_byte_span(line))) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  if (!ret) {
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorEventHeaderParsing,
                          target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  while (data_len > 0) {
    int tlv_type;
    int tlv_len;

    line.clear();
    ret = ParseTlvHeader(dump_file, &tlv_type, &tlv_len);
    if (!ret || tlv_len <= 0 || tlv_len > data_len) {
      LOG(ERROR) << "Error parsing TLV header with type " << tlv_type
                 << " and length " << tlv_len;
      if (!ReportParseError(ParseErrorReason::kErrorTlvParsing, target_file)) {
        PLOG(ERROR) << "Error writing to target file " << target_path;
        return false;
      }
      break;
    }

    switch (tlv_type) {
      case kTlvExcType:
        ret = ParseExceptionType(dump_file, &line);
        break;
      case kTlvLineNum:
        ret = ParseLineNumber(dump_file, &line);
        break;
      case kTlvModule:
        ret = ParseModuleNumber(dump_file, &line);
        break;
      case kTlvErrorId:
        ret = ParseErrorId(dump_file, &line);
        break;
      case kTlvBacktrace:
        ret = ParseBacktrace(dump_file, &line);
        break;
      case kTlvAuxReg:
        if (tlv_len == sizeof(struct TlvAuxReg)) {
          ret = ParseAuxRegisters(dump_file, pc, &line);
        } else {
          ret = ParseAuxRegistersExtended(dump_file, pc, &line);
        }
        break;
      case kTlvSubType:
        ret = ParseExceptionSubtype(dump_file, &line);
        break;
      default:
        if (dump_file.Seek(base::File::FROM_CURRENT, tlv_len) == -1) {
          PLOG(ERROR) << "Error seeking file " << coredump_path;
          ret = false;
        }
        break;
    }

    if (!ret) {
      // Do not continue if parsing of any of the TLV fails because once we are
      // out of sync with the dump, parsing further information is going to be
      // erroneous information.
      LOG(ERROR) << "Error parsing TLV with type " << tlv_type << " and length "
                 << tlv_len;
      if (!ReportParseError(ParseErrorReason::kErrorTlvParsing, target_file)) {
        PLOG(ERROR) << "Error writing to target file " << target_path;
        return false;
      }
      break;
    }

    if (!line.empty() &&
        !target_file.WriteAtCurrentPosAndCheck(base::as_byte_span(line))) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }

    data_len -= (sizeof(struct TlvHeader) + tlv_len);
  }

  if (pc->empty()) {
    // If no PC found in the coredump blob, use the default value for PC
    if (!ReportDefaultPC(target_file, pc)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  return true;
}

}  // namespace intel

namespace realtek {

// More information about Realtek telemetry spec: go/cros-bt-realtek-telemetry

constexpr char kVendorName[] = "Realtek";
constexpr uint8_t kOpCodeEventField = 0xFF;

struct EventHeader {
  uint8_t devcd_code[4];
  uint8_t opcode_event_field;
  uint8_t len;
} __attribute__((packed));

struct EventData {
  uint8_t sub_event_code;
  uint8_t reserved;
  uint8_t isr;
  uint8_t isr_number;
  uint8_t cpu_idle;
  uint8_t signal_id[2];
  uint8_t isr_cause[4];
  uint8_t isr_cnts[4];
  uint8_t last_epc[4];
  uint8_t timer_handle[4];
  uint8_t calendar_table_index;
  uint8_t timer_count;
  uint8_t timer_value[4];
  uint8_t timeout_function[4];
  uint8_t timer_type;
  uint8_t timer_args[4];
  uint8_t next_os_timer[4];
  uint8_t state_of_timer;
  uint8_t sniff_tick_timer[4];
  uint8_t isr_cause_ori[4];
  uint8_t return_addr[4];
} __attribute__((packed));

bool ParseEventHeader(base::File& file, int& data_len, std::string& out) {
  struct EventHeader evt_header;
  int ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&evt_header),
                                  sizeof(evt_header));
  if (ret < sizeof(evt_header)) {
    LOG(WARNING) << "Error reading Realtek devcoredump Event Header";
    return false;
  }

  out = base::StrCat(
      {CreateDumpEntry("Realtek Event Header",
                       base::HexEncode(&evt_header, sizeof(evt_header))),
       CreateDumpEntry("Devcoredump Code",
                       base::HexEncode(&evt_header.devcd_code,
                                       sizeof(evt_header.devcd_code)))});

  if (evt_header.opcode_event_field != kOpCodeEventField) {
    LOG(WARNING) << "Incorrect Realtek OpCode Event Field";
    return false;
  }

  data_len = evt_header.len;

  return true;
}

bool ParseEventData(base::File& file, std::string* pc, std::string& out) {
  struct EventData evt_data;
  int ret = file.ReadAtCurrentPos(reinterpret_cast<char*>(&evt_data),
                                  sizeof(evt_data));
  if (ret < sizeof(evt_data)) {
    LOG(WARNING) << "Error reading Realtek devcoredump Event Data";
    return false;
  }

  *pc = base::HexEncode(&evt_data.last_epc, sizeof(evt_data.last_epc));

  // Clang format inconsistently formats following lines. Disable clang format
  // to keep it as it is for better readability.
  // clang-format off
  out = base::StrCat(
      {CreateDumpEntry("Sub-event Code",
                       base::HexEncode(&evt_data.sub_event_code,
                                       sizeof(evt_data.sub_event_code))),
       CreateDumpEntry("ISR",
                       base::HexEncode(&evt_data.isr, sizeof(evt_data.isr))),
       CreateDumpEntry("Number of ISR",
                       base::HexEncode(&evt_data.isr_number,
                                       sizeof(evt_data.isr_number))),
       CreateDumpEntry("CPU Idle",
                       base::HexEncode(&evt_data.cpu_idle,
                                       sizeof(evt_data.cpu_idle))),
       CreateDumpEntry("Signal ID",
                       base::HexEncode(&evt_data.signal_id,
                                       sizeof(evt_data.signal_id))),
       CreateDumpEntry("ISR Cause",
                       base::HexEncode(&evt_data.isr_cause,
                                       sizeof(evt_data.isr_cause))),
       CreateDumpEntry("ISR Cnts",
                       base::HexEncode(&evt_data.isr_cnts,
                                       sizeof(evt_data.isr_cnts))),
       CreateDumpEntry("PC",
                       base::HexEncode(&evt_data.last_epc,
                                       sizeof(evt_data.last_epc))),
       CreateDumpEntry("Timer Handle",
                       base::HexEncode(&evt_data.timer_handle,
                                       sizeof(evt_data.timer_handle))),
       CreateDumpEntry("Calendar Table Index",
                       base::HexEncode(&evt_data.calendar_table_index,
                                       sizeof(evt_data.calendar_table_index))),
       CreateDumpEntry("Timer Count",
                       base::HexEncode(&evt_data.timer_count,
                                       sizeof(evt_data.timer_count))),
       CreateDumpEntry("Timer Value",
                       base::HexEncode(&evt_data.timer_value,
                                       sizeof(evt_data.timer_value))),
       CreateDumpEntry("Timeout Function",
                       base::HexEncode(&evt_data.timeout_function,
                                       sizeof(evt_data.timeout_function))),
       CreateDumpEntry("Timer Type",
                       base::HexEncode(&evt_data.timer_type,
                                       sizeof(evt_data.timer_type))),
       CreateDumpEntry("Timer Args",
                       base::HexEncode(&evt_data.timer_args,
                                       sizeof(evt_data.timer_args))),
       CreateDumpEntry("Next OS Timer",
                       base::HexEncode(&evt_data.next_os_timer,
                                       sizeof(evt_data.next_os_timer))),
       CreateDumpEntry("State of Timer",
                       base::HexEncode(&evt_data.state_of_timer,
                                       sizeof(evt_data.state_of_timer))),
       CreateDumpEntry("Sniff Tick Timer",
                       base::HexEncode(&evt_data.sniff_tick_timer,
                                       sizeof(evt_data.sniff_tick_timer))),
       CreateDumpEntry("ISR Cause ori",
                       base::HexEncode(&evt_data.isr_cause_ori,
                                       sizeof(evt_data.isr_cause_ori))),
       CreateDumpEntry("Return Addr",
                       base::HexEncode(&evt_data.return_addr,
                                       sizeof(evt_data.return_addr)))});
  // clang-format on

  return true;
}

bool ParseRealtekDump(const base::FilePath& coredump_path,
                      const base::FilePath& target_path,
                      const int64_t dump_start,
                      std::string* pc) {
  base::File dump_file(coredump_path,
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  base::File target_file(target_path,
                         base::File::FLAG_OPEN | base::File::FLAG_APPEND);

  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  if (!dump_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << coredump_path << " Error: "
               << base::File::ErrorToString(dump_file.error_details());
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  if (dump_file.Seek(base::File::FROM_BEGIN, dump_start) == -1) {
    PLOG(ERROR) << "Error seeking file " << coredump_path;
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  std::string line;
  int data_len;
  bool ret = ParseEventHeader(dump_file, data_len, line);

  // Always report the event header whenever available, even if parsing fails.
  if (!line.empty() &&
      !target_file.WriteAtCurrentPosAndCheck(base::as_byte_span(line))) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  if (!ret) {
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorEventHeaderParsing,
                          target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  if (data_len != sizeof(EventData)) {
    LOG(ERROR) << "Incorrect data length " << data_len << " (expected "
               << sizeof(EventData) << ")";
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorDataLength, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  if (!ParseEventData(dump_file, pc, line)) {
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorEventDataParsing,
                          target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  if (!line.empty() &&
      !target_file.WriteAtCurrentPosAndCheck(base::as_byte_span(line))) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  return true;
}

}  // namespace realtek

namespace mediatek {

// More information about MediaTek telemetry spec: go/cros-bt-mediatek-telemetry

constexpr char kVendorName[] = "MediaTek";
constexpr int kTotalLogRegisters = 32;

enum class ParserState {
  kParseAssertLine,
  kParseProgCounter,
  kParseLogRegisters,
  kParseDone,
};

bool ParseAssertLine(base::File& file, std::string& out) {
  std::string line;

  if (!util::GetNextLine(file, line)) {
    return false;
  }

  std::vector<std::string> tokens = base::SplitString(
      line, ";,", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Record the first part after ";" which is the file name and line number of
  // the crash
  if (tokens.size() > 1) {
    out = CreateDumpEntry("Crash Location", tokens[1]);
  }

  return true;
}

bool ParseProgCounter(base::File& file, std::string& out, std::string* pc) {
  std::string line;

  if (!util::GetNextLine(file, line)) {
    return false;
  }

  std::vector<std::string> out_vec;
  std::vector<std::string> tokens = base::SplitString(
      line, ";()", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (const auto& token : tokens) {
    std::vector<std::pair<std::string, std::string>> target_keyval;

    // SplitStringIntoKeyValuePairs() returns true only if all key-value pairs
    // are non-empty. So, no need to check the return value here, instead,
    // process all key-value pairs and report the non-empty ones.
    base::SplitStringIntoKeyValuePairs(token, '=', '\0', &target_keyval);

    for (const auto& key_value : target_keyval) {
      // The dump emitted by MediaTek firmware has a typo "contorl". So, check
      // for both "contorl" and "control" to avoid future breakages if and when
      // they fix it.
      if (key_value.first == "PC log contorl" ||
          key_value.first == "PC log control") {
        *pc = key_value.second;
        out_vec.push_back(CreateDumpEntry("PC", key_value.second));
      } else if (!key_value.first.empty()) {
        out_vec.push_back(CreateDumpEntry(key_value.first, key_value.second));
      }
    }
  }

  out = base::StrCat(out_vec);

  return true;
}

bool ParseLogRegisters(base::File& file, std::string& out) {
  std::vector<std::string> out_vec;

  for (int i = 0; i < kTotalLogRegisters; i++) {
    std::string line;

    if (!util::GetNextLine(file, line)) {
      return false;
    }

    std::vector<std::pair<std::string, std::string>> target_keyval;

    // SplitStringIntoKeyValuePairs() returns true only if all key-value pairs
    // are non-empty. So, no need to check the return value here, instead,
    // process all key-value pairs and report the non-empty ones.
    base::SplitStringIntoKeyValuePairs(line, '=', ';', &target_keyval);

    for (const auto& key_value : target_keyval) {
      if (!key_value.first.empty()) {
        out_vec.push_back(CreateDumpEntry(key_value.first, key_value.second));
      }
    }
  }

  out = base::StrCat(out_vec);

  return true;
}

bool ParseMediatekDump(const base::FilePath& coredump_path,
                       const base::FilePath& target_path,
                       const int64_t dump_start,
                       std::string* pc) {
  base::File dump_file(coredump_path,
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  base::File target_file(target_path,
                         base::File::FLAG_OPEN | base::File::FLAG_APPEND);

  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  if (!dump_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << coredump_path << " Error: "
               << base::File::ErrorToString(dump_file.error_details());
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  if (dump_file.Seek(base::File::FROM_BEGIN, dump_start) == -1) {
    PLOG(ERROR) << "Error seeking file " << coredump_path;
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  ParserState state = ParserState::kParseAssertLine;

  while (state != ParserState::kParseDone) {
    std::string parsed_lines;
    bool parse_status = true;

    switch (state) {
      case ParserState::kParseAssertLine:
        parse_status = ParseAssertLine(dump_file, parsed_lines);
        state = ParserState::kParseProgCounter;
        break;
      case ParserState::kParseProgCounter:
        parse_status = ParseProgCounter(dump_file, parsed_lines, pc);
        state = ParserState::kParseLogRegisters;
        break;
      case ParserState::kParseLogRegisters:
        parse_status = ParseLogRegisters(dump_file, parsed_lines);
        state = ParserState::kParseDone;
        break;
      default:
        LOG(ERROR) << "Incorrect parsing state";
        return false;
    }

    if (!parse_status) {
      // Do not continue if parsing of any of the line fails because once we are
      // out of sync with the dump, parsing further information is going to be
      // erroneous information.
      PLOG(ERROR) << "Error parsing file " << coredump_path;
      if (!ReportParseError(ParseErrorReason::kErrorEventDataParsing,
                            target_file)) {
        PLOG(ERROR) << "Error writing to target file " << target_path;
        return false;
      }
      break;
    }

    if (!parsed_lines.empty() && !target_file.WriteAtCurrentPosAndCheck(
                                     base::as_byte_span(parsed_lines))) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  if (pc->empty()) {
    // If no PC found in the coredump blob, use the default value for PC
    if (!ReportDefaultPC(target_file, pc)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  return true;
}

}  // namespace mediatek

namespace qualcomm {

// More information about Qualcomm telemetry spec: go/cros-bt-qualcomm-telemetry

constexpr char kVendorName[] = "qca";
constexpr int kPCOffset = 0xFEE8;
constexpr int kReasonOffset = 0xFEEC;

enum class ParserState {
  kParsePC,
  kParseReason,
  kParseDone,
};

bool ParsePC(base::File& file,
             int64_t dump_start,
             std::string& out,
             std::string* pc) {
  if (file.Seek(base::File::FROM_BEGIN, dump_start + kPCOffset) == -1) {
    LOG(WARNING) << "Error seeking file";
    return false;
  }

  uint32_t val;

  if (file.ReadAtCurrentPos(reinterpret_cast<char*>(&val), sizeof(val)) <
      sizeof(val)) {
    LOG(WARNING) << "Error reading PC value";
    return false;
  }

  out = CreateDumpEntry("PC", base::HexEncode(&val, sizeof(val)));
  *pc = base::HexEncode(&val, sizeof(val));

  return true;
}

bool ParseReason(base::File& file, int64_t dump_start, std::string& out) {
  if (file.Seek(base::File::FROM_BEGIN, dump_start + kReasonOffset) == -1) {
    LOG(WARNING) << "Error seeking file";
    return false;
  }

  uint32_t val;

  if (file.ReadAtCurrentPos(reinterpret_cast<char*>(&val), sizeof(val)) <
      sizeof(val)) {
    LOG(WARNING) << "Error reading Reason Code value";
    return false;
  }

  out = CreateDumpEntry("Reason Code", base::HexEncode(&val, sizeof(val)));

  return true;
}

bool ParseQualcommDump(const base::FilePath& coredump_path,
                       const base::FilePath& target_path,
                       const int64_t dump_start,
                       std::string* pc) {
  base::File dump_file(coredump_path,
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  base::File target_file(target_path,
                         base::File::FLAG_OPEN | base::File::FLAG_APPEND);

  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  if (!dump_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << coredump_path << " Error: "
               << base::File::ErrorToString(dump_file.error_details());
    // Use the default value for PC and report an empty dump.
    if (!ReportDefaultPC(target_file, pc) ||
        !ReportParseError(ParseErrorReason::kErrorFileIO, target_file)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
    return true;
  }

  ParserState state = ParserState::kParsePC;

  while (state != ParserState::kParseDone) {
    std::string line;
    bool parse_status = true;

    switch (state) {
      case ParserState::kParsePC:
        parse_status = ParsePC(dump_file, dump_start, line, pc);
        state = ParserState::kParseReason;
        break;
      case ParserState::kParseReason:
        parse_status = ParseReason(dump_file, dump_start, line);
        state = ParserState::kParseDone;
        break;
      default:
        LOG(ERROR) << "Incorrect parsing state";
        return false;
    }

    if (!parse_status) {
      // Do not continue if parsing of any of the line fails because once we are
      // out of sync with the dump, parsing further information is going to be
      // erroneous information.
      PLOG(ERROR) << "Error parsing file " << coredump_path;
      if (!ReportParseError(ParseErrorReason::kErrorEventDataParsing,
                            target_file)) {
        PLOG(ERROR) << "Error writing to target file " << target_path;
        return false;
      }
      break;
    }

    if (!line.empty() &&
        !target_file.WriteAtCurrentPosAndCheck(base::as_byte_span(line))) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  if (pc->empty()) {
    // If no PC found in the coredump blob, use the default value for PC
    if (!ReportDefaultPC(target_file, pc)) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  return true;
}

}  // namespace qualcomm

}  // namespace vendor

namespace {

constexpr char kCoredumpMetaHeader[] = "Bluetooth devcoredump";
constexpr char kCoredumpDataHeader[] = "--- Start dump ---";
constexpr char kCoredumpDefaultPC[] = "00000000";
const std::vector<std::string> kCoredumpState = {
    "Devcoredump Idle",  "Devcoredump Active",  "Devcoredump Complete",
    "Devcoredump Abort", "Devcoredump Timeout",
};

std::string CreateDumpEntry(const std::string& key, const std::string& value) {
  return base::StrCat({key, "=", value, "\n"});
}

int64_t GetDumpPos(base::File& file) {
  return file.Seek(base::File::FROM_CURRENT, 0);
}

bool ReportDefaultPC(base::File& file, std::string* pc) {
  *pc = kCoredumpDefaultPC;
  std::string line = CreateDumpEntry("PC", kCoredumpDefaultPC);
  if (!file.WriteAtCurrentPosAndCheck(base::as_byte_span(line))) {
    return false;
  }
  return true;
}

bool ReportParseError(ParseErrorReason error_code, base::File& file) {
  std::string line =
      CreateDumpEntry("Parse Failure Reason",
                      base::StringPrintf("%d", static_cast<int>(error_code)));
  return file.WriteAtCurrentPosAndCheck(base::as_byte_span(line));
}

// Cannot use base::file_util::CopyFile() here as it copies the entire file,
// whereas SaveDumpData() needs to copy only the part of the file.
bool SaveDumpData(const base::FilePath& coredump_path,
                  const base::FilePath& target_path,
                  int64_t dump_start) {
  // Overwrite if the output file already exists. It makes more sense for the
  // parser binary as a standalone tool to overwrite than to fail when a file
  // exists.
  base::File target_file(
      target_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  std::string coredump_content;
  if (!base::ReadFileToString(coredump_path, &coredump_content)) {
    PLOG(ERROR) << "Error reading coredump file " << coredump_path;
    return false;
  }

  if (!target_file.WriteAtCurrentPosAndCheck(
          base::as_bytes(base::span<const char>(
              coredump_content.substr(dump_start, std::string::npos))))) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  LOG(INFO) << "Binary devcoredump data: " << target_path;

  return true;
}

bool ParseDumpHeader(const base::FilePath& coredump_path,
                     const base::FilePath& target_path,
                     int64_t* data_pos,
                     std::string* driver_name,
                     std::string* vendor_name,
                     std::string* controller_name) {
  base::File dump_file(coredump_path,
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  // Overwrite if the output file already exists. It makes more sense for the
  // parser binary as a standalone tool to overwrite than to fail when a file
  // exists.
  base::File target_file(
      target_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  std::string line;

  if (!dump_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << coredump_path << " Error: "
               << base::File::ErrorToString(dump_file.error_details());
    return false;
  }

  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  while (util::GetNextLine(dump_file, line)) {
    if (line[0] == '\0') {
      // After updating the devcoredump state, the Bluetooth HCI Devcoredump
      // API adds a '\0' at the end. Remove it before splitting the line.
      line.erase(0, 1);
    }
    if (line == kCoredumpMetaHeader) {
      // Skip the header
      continue;
    }
    if (line == kCoredumpDataHeader) {
      // End of devcoredump header fields
      break;
    }

    std::vector<std::string> fields = SplitString(
        line, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (fields.size() < 2) {
      LOG(ERROR) << "Invalid bluetooth devcoredump header line: " << line;
      return false;
    }

    std::string& key = fields[0];
    std::string& value = fields[1];

    if (key == "State") {
      int state;
      if (base::StringToInt(value, &state) && state >= 0 &&
          state < kCoredumpState.size()) {
        value = kCoredumpState[state];
      }
    } else if (key == "Driver") {
      *driver_name = value;
    } else if (key == "Vendor") {
      *vendor_name = value;
    } else if (key == "Controller Name") {
      *controller_name = value;
    }

    if (!target_file.WriteAtCurrentPosAndCheck(base::as_bytes(
            base::span<const char>(CreateDumpEntry(key, value))))) {
      PLOG(ERROR) << "Error writing to target file " << target_path;
      return false;
    }
  }

  *data_pos = GetDumpPos(dump_file);

  if (driver_name->empty() || vendor_name->empty() ||
      controller_name->empty()) {
    // If any of the required fields are missing, close the target file and
    // delete it.
    target_file.Close();
    if (!base::DeleteFile(target_path)) {
      LOG(ERROR) << "Error deleting file " << target_path;
    }
    return false;
  }

  return true;
}

// TODO(b/310978711): non-optional output parameter pc should be a reference,
// not a pointer.
bool ParseDumpData(const base::FilePath& coredump_path,
                   const base::FilePath& target_path,
                   const int64_t dump_start,
                   const std::string& vendor_name,
                   std::string* pc,
                   const bool save_dump_data) {
  if (save_dump_data) {
    // Save a copy of dump data on developer image. This is not attached with
    // the crash report, used only for development purpose.
    if (!SaveDumpData(coredump_path, target_path.ReplaceExtension("data"),
                      dump_start)) {
      LOG(ERROR) << "Error saving bluetooth devcoredump data";
    }
  }

  if (vendor_name == vendor::intel::kVendorName) {
    return vendor::intel::ParseIntelDump(coredump_path, target_path, dump_start,
                                         pc);
  } else if (vendor_name == vendor::realtek::kVendorName) {
    return vendor::realtek::ParseRealtekDump(coredump_path, target_path,
                                             dump_start, pc);
  } else if (vendor_name == vendor::mediatek::kVendorName) {
    return vendor::mediatek::ParseMediatekDump(coredump_path, target_path,
                                               dump_start, pc);
  } else if (vendor_name == vendor::qualcomm::kVendorName) {
    return vendor::qualcomm::ParseQualcommDump(coredump_path, target_path,
                                               dump_start, pc);
  }

  LOG(WARNING) << "Unsupported bluetooth devcoredump vendor - " << vendor_name;

  // Since no supported vendor found, use the default value for PC and
  // return true to report the crash event.
  base::File target_file(target_path,
                         base::File::FLAG_OPEN | base::File::FLAG_APPEND);
  if (!target_file.IsValid()) {
    LOG(ERROR) << "Error opening file " << target_path << " Error: "
               << base::File::ErrorToString(target_file.error_details());
    return false;
  }

  if (!ReportDefaultPC(target_file, pc)) {
    PLOG(ERROR) << "Error writing to target file " << target_path;
    return false;
  }

  return true;
}

}  // namespace

namespace bluetooth_util {

bool ParseBluetoothCoredump(const base::FilePath& coredump_path,
                            const base::FilePath& output_dir,
                            const bool save_dump_data,
                            std::string* crash_sig) {
  std::string driver_name;
  std::string vendor_name;
  std::string controller_name;
  int64_t data_pos;
  std::string pc;

  LOG(INFO) << "Input coredump path: " << coredump_path;

  base::FilePath target_path = coredump_path.ReplaceExtension("txt");
  if (!output_dir.empty()) {
    LOG(INFO) << "Output dir: " << output_dir;
    target_path = output_dir.Append(target_path.BaseName());
  }
  LOG(INFO) << "Parsed coredump path: " << target_path;

  if (!ParseDumpHeader(coredump_path, target_path, &data_pos, &driver_name,
                       &vendor_name, &controller_name)) {
    LOG(ERROR) << "Error parsing bluetooth devcoredump header";
    return false;
  }

  if (!ParseDumpData(coredump_path, target_path, data_pos, vendor_name, &pc,
                     save_dump_data)) {
    LOG(ERROR) << "Error parsing bluetooth devcoredump data";
    return false;
  }

  *crash_sig = bluetooth_util::CreateCrashSig(driver_name, vendor_name,
                                              controller_name, pc);

  return true;
}

}  // namespace bluetooth_util
