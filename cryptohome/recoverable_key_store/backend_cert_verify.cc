// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_verify.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

namespace cryptohome {

namespace {

// These are not OpenSSL types, but the libchrome ScopedOpenSSL is generalized
// enough to be used with any type that we want to keep a pointer and free it
// with some existing free function when it goes out of scope. Use those
// wrappers instead of duplicating the utils.
using ScopedXmlDoc = ::crypto::ScopedOpenSSL<xmlDoc, xmlFreeDoc>;
using ScopedXmlXPathContext =
    ::crypto::ScopedOpenSSL<xmlXPathContext, xmlXPathFreeContext>;
using ScopedXmlXPathObject =
    ::crypto::ScopedOpenSSL<xmlXPathObject, xmlXPathFreeObject>;

constexpr xmlChar kSignatureXmlIntermediateCertsXPath[] =
    "/signature/intermediates/cert";
constexpr xmlChar kSignatureXmlSigningCertXPath[] = "/signature/certificate";
constexpr xmlChar kSignatureXmlSignatureXPath[] = "/signature/value";

std::string_view XmlCharArrayToString(const xmlChar* str) {
  // xmlChar* represents null-terminated UTF-8 string, so it can be casted
  // to char* safely.
  static_assert(std::is_same<xmlChar, unsigned char>());
  return std::string_view(reinterpret_cast<const char*>(str));
}

std::optional<brillo::Blob> Base64DecodeFromXmlCharArray(const xmlChar* str) {
  return base::Base64Decode(XmlCharArrayToString(str));
}

std::optional<std::vector<brillo::Blob>> ParseMultipleBase64Nodes(
    xmlXPathContextPtr ctx, const xmlChar* x_path) {
  std::vector<brillo::Blob> ret;
  ScopedXmlXPathObject obj(xmlXPathEvalExpression(x_path, ctx));
  if (!obj) {
    LOG(ERROR) << "Failed to create XPath object.";
    return std::nullopt;
  }

  xmlNodeSetPtr nodes = obj->nodesetval;
  if (!nodes || !nodes->nodeTab) {
    LOG(ERROR) << "XPath object's node set value is null.";
    return std::nullopt;
  }
  for (size_t i = 0; i < nodes->nodeNr; i++) {
    if (!nodes->nodeTab[i]) {
      LOG(DFATAL) << "Node tab within nodeNr index shouldn't be null.";
      return std::nullopt;
    }
    xmlNodePtr node = nodes->nodeTab[i]->children;
    if (!node || !node->content) {
      LOG(ERROR) << "Node has no content.";
      return std::nullopt;
    }
    std::optional<brillo::Blob> decoded =
        Base64DecodeFromXmlCharArray(node->content);
    if (!decoded.has_value()) {
      LOG(ERROR) << "Node content isn't valid Base64.";
      return std::nullopt;
    }
    ret.push_back(std::move(*decoded));
  }
  return ret;
}

std::optional<brillo::Blob> ParseSingleBase64Node(xmlXPathContextPtr ctx,
                                                  const xmlChar* x_path) {
  ScopedXmlXPathObject obj(xmlXPathEvalExpression(x_path, ctx));
  if (!obj) {
    LOG(ERROR) << "Failed to create XPath object.";
    return std::nullopt;
  }

  xmlNodeSetPtr nodes = obj->nodesetval;
  if (!nodes || !nodes->nodeTab) {
    LOG(ERROR) << "XPath object's node set value is null.";
    return std::nullopt;
  }
  if (nodes->nodeNr != 1 || !nodes->nodeTab[0]) {
    LOG(ERROR) << "Number of nodes isn't exactly 1.";
    return std::nullopt;
  }
  xmlNodePtr node = nodes->nodeTab[0]->children;
  if (!node || !node->content) {
    LOG(ERROR) << "Node has no content.";
    return std::nullopt;
  }
  std::optional<brillo::Blob> decoded =
      Base64DecodeFromXmlCharArray(node->content);
  if (!decoded.has_value()) {
    LOG(ERROR) << "Node content isn't valid Base64.";
  }
  return decoded;
}

struct SignatureXmlParseResult {
  std::vector<brillo::Blob> intermediate_certs;
  brillo::Blob signing_cert;
  brillo::Blob signature;
};

// Check example xml format from
// https://www.gstatic.com/cryptauthvault/v0/cert.sig.xml.
std::optional<SignatureXmlParseResult> ParseSignatureXml(
    const std::string& signature_xml) {
  ScopedXmlDoc doc(xmlParseMemory(signature_xml.data(), signature_xml.size()));
  if (!doc) {
    LOG(ERROR) << "Failed to parse xml.";
    return std::nullopt;
  }

  ScopedXmlXPathContext xpath_ctx(xmlXPathNewContext(doc.get()));
  if (!xpath_ctx) {
    LOG(ERROR) << "Failed to create XPath context.";
    return std::nullopt;
  }

  // Parse the intermediate certs.
  std::optional<std::vector<brillo::Blob>> intermediate_certs =
      ParseMultipleBase64Nodes(xpath_ctx.get(),
                               kSignatureXmlIntermediateCertsXPath);
  if (!intermediate_certs.has_value()) {
    LOG(ERROR) << "Failed to parse the intermediate certs.";
    return std::nullopt;
  }

  // Parse the signing cert.
  std::optional<brillo::Blob> signing_cert =
      ParseSingleBase64Node(xpath_ctx.get(), kSignatureXmlSigningCertXPath);
  if (!signing_cert.has_value()) {
    LOG(ERROR) << "Failed to parse the signing cert.";
    return std::nullopt;
  }

  // Parse the signature.
  std::optional<brillo::Blob> signature =
      ParseSingleBase64Node(xpath_ctx.get(), kSignatureXmlSignatureXPath);
  if (!signature.has_value()) {
    LOG(ERROR) << "Failed to parse the signature.";
    return std::nullopt;
  }

  return SignatureXmlParseResult{
      .intermediate_certs = std::move(*intermediate_certs),
      .signing_cert = std::move(*signing_cert),
      .signature = std::move(*signature),
  };
}

}  // namespace

std::optional<RecoverableKeyStoreCertList>
VerifyAndParseRecoverableKeyStoreBackendCertXmls(
    const std::string& cert_xml, const std::string& signature_xml) {
  // TODO(b/309734008): Record metrics of the verification and parsing result.
  // 1. Parse the signature XML.
  std::optional<SignatureXmlParseResult> signature_result =
      ParseSignatureXml(signature_xml);
  if (!signature_result.has_value()) {
    LOG(ERROR) << "Failed to parse signature xml.";
    return std::nullopt;
  }
  // TODO(b/309734008): Implement the remaining steps.
  // 2. Verify the signature XML's certificates.
  // 3. Verify the certificate XML's integrity using the signature.
  // 4. Parse the certificate XML.
  // 5. Verify the certificate XML's certificates.
  return std::nullopt;
}

}  // namespace cryptohome
