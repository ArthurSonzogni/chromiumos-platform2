// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWSEC_OPTEE_PLUGIN_HWSEC_OPTEE_PLUGIN_H_
#define HWSEC_OPTEE_PLUGIN_HWSEC_OPTEE_PLUGIN_H_

#include "hwsec-optee-plugin/hwsec_optee_plugin_export.h"

HWSEC_OPTEE_PLUGIN_EXPORT extern "C" struct plugin_method plugin_method;

#if defined(UNIT_TEST)
namespace hwsec {
class OpteePluginFrontend;
}  // namespace hwsec
HWSEC_OPTEE_PLUGIN_EXPORT void SetHwsecForTesting(
    const hwsec::OpteePluginFrontend* hwsec);
#endif

#endif  // HWSEC_OPTEE_PLUGIN_HWSEC_OPTEE_PLUGIN_H_
