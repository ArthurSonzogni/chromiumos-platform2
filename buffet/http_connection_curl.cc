// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/http_connection_curl.h"

#include <base/logging.h>

#include "buffet/http_request.h"
#include "buffet/http_transport_curl.h"
#include "buffet/string_utils.h"

namespace buffet {
namespace http {
namespace curl {

static int curl_trace(CURL *handle, curl_infotype type,
                      char *data, size_t size, void *userp) {
  std::string msg(data, size);

  switch (type) {
  case CURLINFO_TEXT:
    VLOG(3) << "== Info: " << msg;
    break;
  case CURLINFO_HEADER_OUT:
    VLOG(3) << "=> Send headers:\n" << msg;
    break;
  case CURLINFO_DATA_OUT:
    VLOG(3) << "=> Send data:\n" << msg;
    break;
  case CURLINFO_SSL_DATA_OUT:
    VLOG(3) << "=> Send SSL data" << msg;
    break;
  case CURLINFO_HEADER_IN:
    VLOG(3) << "<= Recv header: " << msg;
    break;
  case CURLINFO_DATA_IN:
    VLOG(3) << "<= Recv data:\n" << msg;
    break;
  case CURLINFO_SSL_DATA_IN:
    VLOG(3) << "<= Recv SSL data" << msg;
    break;
  default:
    break;
  }
  return 0;
}

Connection::Connection(CURL* curl_handle, const std::string& method,
                       std::shared_ptr<http::Transport> transport) :
    http::Connection(transport), method_(method), curl_handle_(curl_handle) {
  VLOG(1) << "curl::Connection created: " << method_;
}

Connection::~Connection() {
  VLOG(1) << "curl::Connection destroyed";
}

bool Connection::SendHeaders(const HeaderList& headers,
                             chromeos::ErrorPtr* error) {
  headers_.insert(headers.begin(), headers.end());
  return true;
}

bool Connection::WriteRequestData(const void* data, size_t size,
                                  chromeos::ErrorPtr* error) {
  if (size > 0) {
    auto data_ptr = reinterpret_cast<const unsigned char*>(data);
    request_data_.insert(request_data_.end(), data_ptr, data_ptr + size);
  }
  return true;
}

bool Connection::FinishRequest(chromeos::ErrorPtr* error) {
  if (VLOG_IS_ON(3)) {
    curl_easy_setopt(curl_handle_, CURLOPT_DEBUGFUNCTION, curl_trace);
    curl_easy_setopt(curl_handle_, CURLOPT_VERBOSE, 1L);
  }

  // Set up HTTP request data.
  if (method_ == request_type::kPut) {
    curl_easy_setopt(curl_handle_, CURLOPT_INFILESIZE_LARGE,
                      curl_off_t(request_data_.size()));
  } else {
    curl_easy_setopt(curl_handle_, CURLOPT_POSTFIELDSIZE_LARGE,
                      curl_off_t(request_data_.size()));
  }
  if (!request_data_.empty()) {
    curl_easy_setopt(curl_handle_,
                     CURLOPT_READFUNCTION, &Connection::read_callback);
    curl_easy_setopt(curl_handle_, CURLOPT_READDATA, this);
    VLOG(2) << "Raw request data: "
        << std::string(reinterpret_cast<const char*>(request_data_.data()),
                       request_data_.size());
  }

  curl_slist* header_list = nullptr;
  if (!headers_.empty()) {
    for (auto pair : headers_) {
      std::string header = string_utils::Join(": ", pair.first, pair.second);
      VLOG(2) << "Request header: " << header;
      header_list = curl_slist_append(header_list, header.c_str());
    }
    curl_easy_setopt(curl_handle_, CURLOPT_HTTPHEADER, header_list);
  }

  headers_.clear();

  // Set up HTTP response data.
  if (method_ != request_type::kHead) {
    curl_easy_setopt(curl_handle_,
                     CURLOPT_WRITEFUNCTION, &Connection::write_callback);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEDATA, this);
  }

  // HTTP response headers
  curl_easy_setopt(curl_handle_,
                   CURLOPT_HEADERFUNCTION, &Connection::header_callback);
  curl_easy_setopt(curl_handle_, CURLOPT_HEADERDATA, this);

  CURLcode ret = curl_easy_perform(curl_handle_);
  if (header_list)
    curl_slist_free_all(header_list);
  if (ret != CURLE_OK) {
    chromeos::Error::AddTo(error, http::curl::kErrorDomain,
                           string_utils::ToString(ret),
                           curl_easy_strerror(ret));
  } else {
    LOG(INFO) << "Response: " << GetResponseStatusCode() << " ("
      << GetResponseStatusText() << ")";
    VLOG(2) << "Response data (" << response_data_.size() << "): "
        << std::string(reinterpret_cast<const char*>(response_data_.data()),
                       response_data_.size());
  }
  return (ret == CURLE_OK);
}

int Connection::GetResponseStatusCode() const {
  long status_code = 0;  // NOLINT(runtime/int) - curl expects a long here.
  curl_easy_getinfo(curl_handle_, CURLINFO_RESPONSE_CODE, &status_code);
  return status_code;
}

std::string Connection::GetResponseStatusText() const {
  return status_text_;
}

std::string Connection::GetProtocolVersion() const {
  return protocol_version_;
}

std::string Connection::GetResponseHeader(
    const std::string& header_name) const {
  auto p = headers_.find(header_name);
  return p != headers_.end() ? p->second : std::string();
}

uint64_t Connection::GetResponseDataSize() const {
  return response_data_.size();
}

bool Connection::ReadResponseData(void* data,
                                  size_t buffer_size,
                                  size_t* size_read,
                                  chromeos::ErrorPtr* error) {
  size_t size_to_read = response_data_.size() - response_data_ptr_;
  if (size_to_read > buffer_size)
    size_to_read = buffer_size;
  memcpy(data, response_data_.data() + response_data_ptr_, size_to_read);
  if (size_read)
    *size_read = size_to_read;
  response_data_ptr_ += size_to_read;
  return true;
}

size_t Connection::write_callback(char* ptr, size_t size,
                                  size_t num, void* data) {
  Connection* me = reinterpret_cast<Connection*>(data);
  size_t data_len = size * num;
  me->response_data_.insert(me->response_data_.end(), ptr, ptr + data_len);
  return data_len;
}

size_t Connection::read_callback(char* ptr, size_t size,
                                 size_t num, void* data) {
  Connection* me = reinterpret_cast<Connection*>(data);
  size_t data_len = size * num;

  if (me->request_data_ptr_ >= me->request_data_.size())
    return 0;

  if (me->request_data_ptr_ + data_len > me->request_data_.size())
    data_len = me->request_data_.size() - me->request_data_ptr_;

  memcpy(ptr, me->request_data_.data() + me->request_data_ptr_, data_len);
  me->request_data_ptr_ += data_len;

  return data_len;
}

size_t Connection::header_callback(char* ptr, size_t size,
                                   size_t num, void* data) {
  Connection* me = reinterpret_cast<Connection*>(data);
  size_t hdr_len = size * num;
  std::string header(ptr, hdr_len);
  // Remove newlines at the end of header line.
  while (!header.empty() && (header.back() == '\r' || header.back() == '\n')) {
    header.pop_back();
  }

  VLOG(2) << "Response header: " << header;

  if (!me->status_text_set_) {
    // First header - response code as "HTTP/1.1 200 OK".
    // Need to extract the OK part
    auto pair = string_utils::SplitAtFirst(header, ' ');
    me->protocol_version_ = pair.first;
    me->status_text_ = string_utils::SplitAtFirst(pair.second, ' ').second;
    me->status_text_set_ = true;
  } else {
    auto pair = string_utils::SplitAtFirst(header, ':');
    if (!pair.second.empty())
      me->headers_.insert(pair);
  }
  return hdr_len;
}

}  // namespace curl
}  // namespace http
}  // namespace buffet
