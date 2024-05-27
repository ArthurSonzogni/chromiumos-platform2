// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_URL_MOJOM_URL_MOJOM_TRAITS_H_
#define SHILL_MOJOM_URL_MOJOM_URL_MOJOM_TRAITS_H_

#include <string>

#include <mojo/public/cpp/bindings/struct_traits.h>
#include <net-base/http_url.h>

#include "url/mojom/url.mojom.h"

namespace mojo {

template <>
struct StructTraits<url::mojom::UrlDataView, net_base::HttpUrl> {
  static bool IsNull(const net_base::HttpUrl& url) {
    return url.protocol() == net_base::HttpUrl::Protocol::kUnknown;
  }

  static void SetToNull(net_base::HttpUrl* url) { *url = net_base::HttpUrl(); }

  static std::string url(const net_base::HttpUrl& url) {
    return url.ToString();
  }

  static bool Read(url::mojom::UrlDataView data, net_base::HttpUrl* out_url) {
    mojo::StringDataView string_view;
    data.GetUrlDataView(&string_view);

    const std::optional<net_base::HttpUrl> url =
        net_base::HttpUrl::CreateFromString(
            {string_view.storage(), string_view.size()});
    if (!url.has_value()) {
      return false;
    }

    *out_url = *url;
    return true;
  }
};

}  // namespace mojo
#endif  // SHILL_MOJOM_URL_MOJOM_URL_MOJOM_TRAITS_H_
