// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_CLIENT_PRINTER_H_
#define DEVICE_MANAGEMENT_CLIENT_PRINTER_H_

#include "device_management/common/print_device_management_interface_proto.h"

#include <openssl/sha.h>
#include <string>

#include <base/command_line.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

namespace device_management {
// Defines the output format to use for display.
enum class OutputFormat {
  // The default format used, geared towards human readability. This will use
  // the proto_print generated libraries for formatting any protobuf output, and
  // will also include informational text. It is not reliably machine-parsable.
  kDefault,
  // Binary protobuf format. The result of the underlying dbus request will be
  // written to standard output, in serialized binary format. Any other
  // informational output will be written to standard error.
  kBinaryProtobuf,
};
class Printer {
 public:
  explicit Printer(OutputFormat output_format)
      : output_format_(output_format) {}
  ~Printer() = default;

  // No copying. Share the printer by pointer or reference.
  Printer(Printer&) = delete;
  Printer& operator=(Printer&) = delete;
  Printer(Printer&&) = delete;
  Printer& operator=(Printer&&) = delete;

  // Print the reply protobuf from a command request.
  template <typename T>
  void PrintReplyProtobuf(const T& protobuf) {
    switch (output_format_) {
      case OutputFormat::kDefault:
        std::cout << device_management::GetProtoDebugString(protobuf);
        return;
      case OutputFormat::kBinaryProtobuf:
        protobuf.SerializeToOstream(&std::cout);
        return;
    }
  }
  // Print a human-oriented text string to output.
  void PrintHumanOutput(const std::string& str) {
    switch (output_format_) {
      case OutputFormat::kDefault:
        std::cout << str;
        return;
      case OutputFormat::kBinaryProtobuf:
        std::cerr << str;
        return;
    }
  }

  // A version of PrintHumanOutput that uses printf-style formatting.
  void PrintFormattedHumanOutput(const char* format, ...) PRINTF_FORMAT(2, 3) {
    va_list ap;
    va_start(ap, format);
    std::string output;
    base::StringAppendV(&output, format, ap);
    va_end(ap);
    PrintHumanOutput(output);
  }

  // Force a write of any of the buffers in the underlying streams.
  void Flush() {
    switch (output_format_) {
      case OutputFormat::kDefault:
        std::cout.flush();
        return;
      case OutputFormat::kBinaryProtobuf:
        std::cout.flush();
        std::cerr.flush();
        return;
    }
  }

 private:
  const OutputFormat output_format_;
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_CLIENT_PRINTER_H_
