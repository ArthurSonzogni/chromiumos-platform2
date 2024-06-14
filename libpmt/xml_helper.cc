// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libpmt/xml_helper.h"

#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

namespace pmt::xml {

XmlParser::XmlParser() {
  // Initialize libxml2 if needed.
  xmlInitParser();
  LIBXML_TEST_VERSION
  // Do not call xmlCleanupParser() in a destructor on purpose! It's unknown
  // whether someone else is using libxml2 aside of this class.
}

std::optional<std::string> XmlParser::GetAttrValue(
    xmlNodePtr node, const std::string_view& name) {
  xmlAttrPtr attr = node->properties;
  while (attr != nullptr) {
    if (attr->type == XML_ATTRIBUTE_NODE &&
        !xmlStrncmp(attr->name, XmlCharCast(name.data()), name.size()) &&
        attr->children != nullptr && attr->children->type == XML_TEXT_NODE)
      return std::string(XmlCharCast(attr->children->content));
    attr = attr->next;
  }
  return std::optional<std::string>();
}

int XmlParser::ParseFile(base::FilePath& file) {
  std::string buf;
  if (!base::ReadFileToString(file, &buf)) {
    return errno;
  }

  doc_.reset(xmlParseMemory(buf.c_str(), buf.size()));
  if (!doc_)
    return EINVAL;

  return 0;
}

void XmlParser::RegisterNamespace(const std::string_view& ns,
                                  const std::string_view& ns_uri) {
  namespaces_[std::string(ns)] = std::string(ns_uri);
}

ScopedXmlXPathObject XmlParser::XPathEval(const std::string_view& xpath) {
  ScopedXmlPathCtx xpath_ctx(xmlXPathNewContext(doc_.get()));
  for (auto ns : namespaces_) {
    xmlXPathRegisterNs(xpath_ctx.get(), XmlCharCast(ns.first.c_str()),
                       XmlCharCast(ns.second.c_str()));
  }
  return ScopedXmlXPathObject(
      xmlXPathEval(XmlCharCast(xpath.data()), xpath_ctx.get()));
}

ScopedXmlXPathObject XmlParser::XPathNodeEval(xmlNodePtr node,
                                              const std::string_view& xpath) {
  ScopedXmlPathCtx xpath_ctx(xmlXPathNewContext(doc_.get()));
  for (auto ns : namespaces_) {
    xmlXPathRegisterNs(xpath_ctx.get(), XmlCharCast(ns.first.c_str()),
                       XmlCharCast(ns.second.c_str()));
  }
  return ScopedXmlXPathObject(
      xmlXPathNodeEval(node, XmlCharCast(xpath.data()), xpath_ctx.get()));
}

std::optional<std::string> XmlParser::GetXPathNodeTextValue(
    xmlNodePtr node, const std::string_view& xpath) {
  ScopedXmlPathCtx xpath_ctx(xmlXPathNewContext(doc_.get()));
  for (auto ns : namespaces_) {
    xmlXPathRegisterNs(xpath_ctx.get(), XmlCharCast(ns.first.c_str()),
                       XmlCharCast(ns.second.c_str()));
  }
  ScopedXmlXPathObject match_obj(
      xmlXPathNodeEval(node, XmlCharCast(xpath.data()), xpath_ctx.get()));
  if (match_obj && match_obj->nodesetval && match_obj->nodesetval->nodeTab &&
      match_obj->nodesetval->nodeNr == 1 &&
      match_obj->nodesetval->nodeTab[0]->children &&
      match_obj->nodesetval->nodeTab[0]->children->type == XML_TEXT_NODE)
    return std::string(
        XmlCharCast(match_obj->nodesetval->nodeTab[0]->children->content));
  return {};
}

}  // namespace pmt::xml
