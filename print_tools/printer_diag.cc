// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>

#include <string.h>
#include <curl/curl.h>

#include <brillo/flag_helper.h>
#include <chromeos/libipp/attribute.h>
#include <chromeos/libipp/builder.h>
#include <chromeos/libipp/frame.h>
#include <chromeos/libipp/parser.h>

#include "helpers.h"
#include "ipp_in_json.h"

namespace {

// Help message about the application.
constexpr char app_info[] =
    "This tool tries to send IPP "
    "Get-Printer-Attributes request to given URL and parse obtained "
    "response. If no output files are specified, the obtained response "
    "is printed to stdout as formatted JSON";

struct read_cb_data {
  const std::vector<uint8_t>* input_data;
  size_t read_position;
};

// Read callback for libcurl; reads from a vector in memory.
size_t read_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
  size_t read_size = size * nitems;
  read_cb_data* read_userdata = static_cast<read_cb_data*>(userdata);
  const std::vector<uint8_t>* input_data = read_userdata->input_data;
  size_t input_start_pos = read_userdata->read_position;
  size_t bytes_available = input_data->size() - input_start_pos;
  if (bytes_available < read_size) {
    read_size = bytes_available;
  }
  if (read_size > 0) {
    memcpy(buffer, input_data->data() + input_start_pos, read_size);
    read_userdata->read_position += read_size;
  }
  return read_size;
}

// Write callback for libcurl; writes to a vector in memory.
size_t write_callback(char* buffer,
                      size_t size,
                      size_t nitems,
                      void* userdata) {
  size_t write_size = size * nitems;
  if (!write_size) {
    return 0;
  }

  std::vector<uint8_t>* output_data =
      static_cast<std::vector<uint8_t>*>(userdata);
  const uint8_t* data_to_copy = (const uint8_t*)buffer;
  output_data->insert(output_data->end(), data_to_copy,
                      data_to_copy + write_size);
  return write_size;
}

// Sends IPP frame (in |data| parameter) to given URL. In case of error, it
// prints out error message to stderr and returns nullopt. Otherwise, it returns
// the body from the response.
std::optional<std::vector<uint8_t>> SendIppFrameAndGetResponse(
    std::string url, const std::vector<uint8_t>& input_data) {
  CURL* curl;
  CURLcode curl_result;
  std::vector<uint8_t> output_data;
  std::optional<std::vector<uint8_t>> return_value = std::nullopt;
  read_cb_data read_userdata = {&input_data, 0};

  curl_result = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (curl_result != CURLE_OK) {
    std::cerr << "Error: failed to initialize curl\n";
    return std::nullopt;
  }

  curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Error: failed to initialize curl\n";
    curl_global_cleanup();
    return std::nullopt;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);

  // Add Content-Type header to request.
  curl_slist* header_list =
      curl_slist_append(nullptr, "Content-Type: application/ipp");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

  // Printers usually have self-signed certificates that won't be accepted by
  // any certificate database on the system. Since printer_diag is only a
  // debugging tool for gathering information about a printer, we don't need
  // or want to be strict about it. This unique need is why this function uses
  // libcurl directly instead of going through brillo's HTTP library.
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

  // Follow redirects.
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  // Set our callbacks to read from and write to std::vector<uint8_t>
  curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
  curl_easy_setopt(curl, CURLOPT_READDATA, static_cast<void*>(&read_userdata));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&output_data));

  // Actually do the request.
  curl_result = curl_easy_perform(curl);
  if (curl_result != CURLE_OK) {
    std::cerr << "HTTP error: " << curl_easy_strerror(curl_result) << "\n";
  } else {
    auto response_code = 999;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    // Per RFC 8010 section 3.4.3, any HTTP status code other than 200 means
    // the response does not contain an IPP message body.
    if (response_code != 200) {
      std::cerr << "HTTP error: HTTP response code " << response_code << "\n";
    } else {
      return_value = std::move(output_data);
    }
  }

  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  curl_global_cleanup();

  return return_value;
}

// Write the content of given buffer to given filename ("location"). When
// "location" equals "-", the content is written to stdout. In case of an error,
// it prints out error message to stderr and returns false. The "buffer"
// parameter cannot be nullptr, exactly "size" elements is read from it.
bool WriteBufferToLocation(const char* buffer,
                           unsigned size,
                           const std::string& location) {
  if (location == "-") {
    std::cout.write(buffer, size);
    std::cout << std::endl;
    if (std::cout.bad()) {
      std::cerr << "Error when writing results to standard output.\n";
      return false;
    }
  } else {
    std::ofstream file(location, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
      std::cerr << "Error when opening the file " << location << ".\n";
      return false;
    }
    file.write(buffer, size);
    file.close();
    if (file.bad()) {
      std::cerr << "Error when writing to the file " << location << ".\n";
      return false;
    }
  }
  return true;
}

}  // namespace

// Return codes:
// * EX_USAGE or EX_DATAERR: incorrect command line parameters
// * -1: cannot build IPP request (libipp error)
// * -2: HTTP exchange error (brillo/http or HTTP error)
// * -3: cannot save an output to given file (I/O error?)
// * -4: cannot build JSON output (base/json error).
// * -5: cannot parse IPP response (incorrect frame was received)
int main(int argc, char** argv) {
  // Define and parse command line parameters, exit if incorrect.
  DEFINE_string(
      url, "", "Address to query, supported protocols: http, https, ipp, ipps");
  DEFINE_string(version, "1.1", "IPP version (default 1.1)");
  DEFINE_string(
      jsonf, "",
      "Save the response as formatted JSON to given file (use - for stdout)");
  DEFINE_string(
      jsonc, "",
      "Save the response as compressed JSON to given file (use - for stdout)");
  DEFINE_string(
      binary, "",
      "Dump the response to given file as a binary content (use - for stdout)");
  brillo::FlagHelper::Init(argc, argv, app_info);
  auto free_params = base::CommandLine::ForCurrentProcess()->GetArgs();
  if (!free_params.empty()) {
    std::cerr << "Unknown parameters:";
    for (auto param : free_params) {
      std::cerr << " " << param;
    }
    std::cerr << std::endl;
    return EX_USAGE;
  }
  // Replace ipp/ipps protocol in the given URL to http/https (if needed).
  if (!ConvertIppToHttp(FLAGS_url)) {
    return EX_USAGE;
  }
  std::cerr << "URL: " << FLAGS_url << std::endl;
  // Parse the IPP version.
  ipp::Version version;
  if (!ipp::FromString(FLAGS_version, &version)) {
    std::cerr << "Unknown version: " << FLAGS_version << ". ";
    std::cerr << "Allowed values: 1.0, 1.1, 2.0, 2.1, 2.2." << std::endl;
    return EX_USAGE;
  }
  std::cerr << "IPP version: " << ipp::ToString(version) << std::endl;
  // If no output files were specified, set the default settings.
  if (FLAGS_binary.empty() && FLAGS_jsonc.empty() && FLAGS_jsonf.empty())
    FLAGS_jsonf = "-";

  // Send IPP request and get a response.
  ipp::Frame request(ipp::Operation::Get_Printer_Attributes, version);
  ipp::Collection& grp = request.Groups(ipp::GroupTag::operation_attributes)[0];
  grp.AddAttr("printer-uri", ipp::ValueTag::uri, FLAGS_url);
  grp.AddAttr("requested-attributes", ipp::ValueTag::keyword,
              std::vector<std::string>{"all", "media-col-database"});
  std::vector<uint8_t> data = ipp::BuildBinaryFrame(request);
  // Resolve the IP after setting printer-uri so the printer can see the
  // original name.
  if (!ResolveZeroconfHostname(FLAGS_url)) {
    return EX_DATAERR;
  }
  auto data_optional = SendIppFrameAndGetResponse(FLAGS_url, data);
  if (!data_optional)
    return -2;
  data = std::move(*data_optional);
  // Write raw frame to file if needed.
  if (!FLAGS_binary.empty()) {
    if (!WriteBufferToLocation(reinterpret_cast<const char*>(data.data()),
                               data.size(), FLAGS_binary)) {
      return -3;
    }
  }

  // Parse the IPP response and save results.
  int return_code = 0;
  ipp::SimpleParserLog log;
  ipp::Frame response = ipp::Parse(data.data(), data.size(), log);
  if (!log.CriticalErrors().empty()) {
    std::cerr << "Parsing of an obtained response was not completed."
              << std::endl;
    return_code = -5;
    // Let's continue, we can still return some data (it is not our error).
  }
  if (!FLAGS_jsonc.empty()) {
    std::string json;
    if (!ConvertToJson(response, log, true, &json)) {
      std::cerr << "Error when preparing a report in JSON (compressed)."
                << std::endl;
      return -4;
    }
    if (!WriteBufferToLocation(json.data(), json.size(), FLAGS_jsonc)) {
      return -3;
    }
  }
  if (!FLAGS_jsonf.empty()) {
    std::string json;
    if (!ConvertToJson(response, log, false, &json)) {
      std::cerr << "Error when preparing a report in JSON (formatted)."
                << std::endl;
      return -4;
    }
    if (!WriteBufferToLocation(json.data(), json.size(), FLAGS_jsonf)) {
      return -3;
    }
  }

  return return_code;
}
