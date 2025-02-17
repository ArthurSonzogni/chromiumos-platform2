// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <cstdio>
#include <iostream>
#include <string>

#include <base/check.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/daemons/dbus_daemon.h>

#include "odml/i18n/translator.h"
#include "odml/i18n/translator_impl.h"
#include "odml/utils/odml_shim_loader_impl.h"

namespace {

constexpr const char kSource[] = "source";
constexpr const char kTarget[] = "target";
constexpr const char kInput[] = "input";

void OnInitialize(base::RunLoop* run_loop, bool success) {
  if (!success) {
    LOG(ERROR) << "Translator failed to initilize";
    exit(1);
  }
  run_loop->Quit();
}

void OnDownloadDlc(base::RunLoop* run_loop, bool success) {
  if (!success) {
    LOG(ERROR) << "Translator failed to download DLC";
    exit(1);
  }
  run_loop->Quit();
}

}  // namespace

class TranslatorConsole : public brillo::DBusDaemon {
 protected:
  int OnInit() override {
    base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
    CHECK(cl->HasSwitch(kSource) && cl->HasSwitch(kTarget));
    int exit_code = brillo::DBusDaemon::OnInit();
    if (exit_code != EX_OK) {
      LOG(ERROR) << "DBusDaemon::OnInit() failed";
      return exit_code;
    }

    std::string source = cl->GetSwitchValueASCII(kSource);
    std::string target = cl->GetSwitchValueASCII(kTarget);
    i18n::LangPair lang_pair{source, target};
    std::string input =
        cl->HasSwitch(kInput)
            ? cl->GetSwitchValueASCII(kInput)
            : std::string{std::istreambuf_iterator<char>(std::cin),
                          std::istreambuf_iterator<char>()};

    odml::OdmlShimLoaderImpl shim_loader_impl;
    raw_ref<odml::OdmlShimLoaderImpl> shim_loader(shim_loader_impl);
    i18n::TranslatorImpl translator(shim_loader);

    base::RunLoop init_loop;
    translator.Initialize(base::BindOnce(&OnInitialize, &init_loop));
    init_loop.Run();

    base::RunLoop dlc_loop;
    translator.DownloadDlc(lang_pair,
                           base::BindOnce(&OnDownloadDlc, &dlc_loop));
    dlc_loop.Run();

    auto result = translator.Translate(lang_pair, input);
    if (!result) {
      return 1;
    }
    std::cout << result.value() << std::endl;

    // Exit daemon,
    // https://crsrc.org/o/src/platform2/libbrillo/brillo/daemons/daemon.h;l=69
    return -1;
  }
};

int main(int argc, char** argv) {
  // Setup command line and logging.
  base::CommandLine::Init(argc, argv);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  TranslatorConsole console;
  console.Run();
  return 0;
}
