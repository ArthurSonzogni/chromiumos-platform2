// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of common HTTP related functions.

#include "update_engine/common/http_common.h"

#include <cstdlib>

namespace chromeos_update_engine {

const char* GetHttpResponseDescription(HttpResponseCode code) {
  static const struct {
    HttpResponseCode code;
    const char* description;
  } http_response_table[] = {
      {kHttpResponseOk, "OK"},
      {kHttpResponseCreated, "Created"},
      {kHttpResponseAccepted, "Accepted"},
      {kHttpResponseNonAuthInfo, "Non-Authoritative Information"},
      {kHttpResponseNoContent, "No Content"},
      {kHttpResponseResetContent, "Reset Content"},
      {kHttpResponsePartialContent, "Partial Content"},
      {kHttpResponseMultipleChoices, "Multiple Choices"},
      {kHttpResponseMovedPermanently, "Moved Permanently"},
      {kHttpResponseFound, "Found"},
      {kHttpResponseSeeOther, "See Other"},
      {kHttpResponseNotModified, "Not Modified"},
      {kHttpResponseUseProxy, "Use Proxy"},
      {kHttpResponseTempRedirect, "Temporary Redirect"},
      {kHttpResponseBadRequest, "Bad Request"},
      {kHttpResponseUnauth, "Unauthorized"},
      {kHttpResponseForbidden, "Forbidden"},
      {kHttpResponseNotFound, "Not Found"},
      {kHttpResponseRequestTimeout, "Request Timeout"},
      {kHttpResponseInternalServerError, "Internal Server Error"},
      {kHttpResponseNotImplemented, "Not Implemented"},
      {kHttpResponseServiceUnavailable, "Service Unavailable"},
      {kHttpResponseVersionNotSupported, "HTTP Version Not Supported"},
  };

  for (const auto& response : http_response_table) {
    if (response.code == code)
      return response.description;
  }

  return "(unsupported)";
}

HttpResponseCode StringToHttpResponseCode(const char* s) {
  return static_cast<HttpResponseCode>(strtoul(s, nullptr, 10));
}

const char* GetHttpContentTypeString(HttpContentType type) {
  static const struct {
    HttpContentType type;
    const char* str;
  } http_content_type_table[] = {
      {kHttpContentTypeTextXml, "text/xml"},
  };

  for (const auto& content_type : http_content_type_table) {
    if (content_type.type == type)
      return content_type.str;
  }

  return nullptr;
}

}  // namespace chromeos_update_engine
