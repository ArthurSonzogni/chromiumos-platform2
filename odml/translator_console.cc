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

void OnTranslate(base::RunLoop* run_loop, std::optional<std::string> result) {
  if (!result) {
    LOG(ERROR) << "Translator failed to translate";
    exit(1);
  }
  std::cout << result.value() << std::endl;
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
            ? cl->GetSwitchValueNative(kInput)
            : std::string{std::istreambuf_iterator<char>(std::cin),
                          std::istreambuf_iterator<char>()};

    odml::OdmlShimLoaderImpl shim_loader_impl;
    raw_ref<odml::OdmlShimLoaderImpl> shim_loader(shim_loader_impl);
    i18n::TranslatorImpl translator(shim_loader);

    base::RunLoop run_loop;
    translator.Translate(lang_pair, input,
                         base::BindOnce(&OnTranslate, &run_loop));
    run_loop.Run();

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
