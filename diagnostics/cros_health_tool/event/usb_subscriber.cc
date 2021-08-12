// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/usb_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/json/json_writer.h>
#include <base/values.h>

namespace diagnostics {
namespace {

void OutputEventInfo(
    const std::string& event,
    const chromeos::cros_healthd::mojom::UsbEventInfoPtr& info) {
  base::Value output{base::Value::Type::DICTIONARY};

  output.SetStringKey("event", event);
  output.SetStringKey("vendor", info->vendor);
  output.SetStringKey("name", info->name);
  output.SetKey("vid", base::Value(info->vid));
  output.SetKey("pid", base::Value(info->pid));

  auto* categories =
      output.SetKey("categories", base::Value{base::Value::Type::LIST});
  for (const auto& category : info->categories) {
    categories->Append(category);
  }

  std::string json;
  base::JSONWriter::WriteWithOptions(
      output, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);

  std::cout << json << std::endl;
}

}  // namespace

UsbSubscriber::UsbSubscriber(
    mojo::PendingReceiver<chromeos::cros_healthd::mojom::CrosHealthdUsbObserver>
        receiver)
    : receiver_{this /* impl */, std::move(receiver)} {
  DCHECK(receiver_.is_bound());
}

UsbSubscriber::~UsbSubscriber() = default;

void UsbSubscriber::OnAdd(
    const chromeos::cros_healthd::mojom::UsbEventInfoPtr info) {
  OutputEventInfo("Add", info);
}

void UsbSubscriber::OnRemove(
    const chromeos::cros_healthd::mojom::UsbEventInfoPtr info) {
  OutputEventInfo("Remove", info);
}

}  // namespace diagnostics
