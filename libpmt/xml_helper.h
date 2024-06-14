// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_XML_HELPER_H_
#define LIBPMT_XML_HELPER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <base/files/file_path.h>
#include <libxml/parser.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlstring.h>
#include <libxml/xpath.h>

namespace pmt::xml {

// Generic destroyer object which calls a function instead of a delete.
template <typename Type, void (*Fn)(Type*)>
struct Destroyer {
  void operator()(Type* obj) { Fn(obj); }
};

// Scoped pointer for the purpose of calling libxml2 object destructors.
template <typename Type, void (*Destructor)(Type*)>
using ScopedXmlPtr = std::unique_ptr<Type, Destroyer<Type, Destructor>>;

// Smart pointers to libxml2 objects.
using ScopedXmlDoc = ScopedXmlPtr<xmlDoc, xmlFreeDoc>;
using ScopedXmlPathCtx = ScopedXmlPtr<xmlXPathContext, xmlXPathFreeContext>;
using ScopedXmlXPathObject = ScopedXmlPtr<xmlXPathObject, xmlXPathFreeObject>;

// A thin layer over libxml2 providing helper functions for browsing the DOM
// tree.
class XmlParser {
 public:
  // Initialize the underlying libxml2 and any structures needed by the parser.
  XmlParser();

  // Parse a given XML file and return 0 if succeeded or else an error code.
  int ParseFile(base::FilePath& file);
  // Register a XML namespace to use in XPath calls.
  void RegisterNamespace(const std::string_view& ns,
                         const std::string_view& ns_uri);
  // Evaluate XPath expression at the document level.
  ScopedXmlXPathObject XPathEval(const std::string_view& xpath);
  // Evaluate XPath expression at the node level.
  ScopedXmlXPathObject XPathNodeEval(xmlNodePtr node,
                                     const std::string_view& xpath);
  // Get a text value of an attribute in a given node if it exists.
  std::optional<std::string> GetAttrValue(xmlNodePtr node,
                                          const std::string_view& name);
  // Get the contents of a child node uniquely identified by an xpath.
  std::optional<std::string> GetXPathNodeTextValue(
      xmlNodePtr node, const std::string_view& xpath);

 private:
  // Parsed document.
  ScopedXmlDoc doc_;
  // Map of known namespaces to initialize the XPath context with.
  std::unordered_map<std::string, std::string> namespaces_;
};

// Convert a C string into a libxml2 string.
//
// A very ugly cast that has to be done since libxml2 uses unsigned char for
// character strings.
static inline const xmlChar* XmlCharCast(const char* v) {
  return reinterpret_cast<const xmlChar*>(v);
}

// Convert a libxml2 string into a C string.
//
// A very ugly cast that has to be done since libxml2 uses unsigned char for
// character strings.
static inline const char* XmlCharCast(const xmlChar* v) {
  return reinterpret_cast<const char*>(v);
}

}  // namespace pmt::xml

#endif  // LIBPMT_XML_HELPER_H_
