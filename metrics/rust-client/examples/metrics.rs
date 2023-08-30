// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::Error;

use metrics_rs::MetricsLibrary;

fn print_result(name: &str, result: Result<(), Error>) {
    match result {
        Ok(()) => println!("{} succeeded", name),
        Err(e) => println!("{} failed: {}", name, e),
    }
}

fn main() {
    let metrics_mutex = MetricsLibrary::get().unwrap();

    let mut metrics = metrics_mutex.lock().unwrap();

    print_result(
        "send_to_uma",
        metrics.send_to_uma("MetricsLibraryTestSendToUMA", 1, 0, 100, 50),
    );
    print_result(
        "send_enum_to_uma",
        metrics.send_enum_to_uma("MetricsLibraryTestSendEnumToUMA", 1, 3),
    );
    print_result(
        "send_repeated_enum_to_uma",
        metrics.send_repeated_enum_to_uma("MetricsLibraryTestSendRepeatedEnumToUMA", 2, 3, 5),
    );
    print_result(
        "send_linear_to_uma",
        metrics.send_linear_to_uma("MetricsLibraryTestSendLinearToUMA", 20, 100),
    );
    print_result(
        "send_percentage_to_uma",
        metrics.send_percentage_to_uma("MetricsLibraryTestSendPercentageToUMA", 30),
    );
    print_result(
        "send_sparse_to_uma",
        metrics.send_sparse_to_uma("MetricsLibraryTestSendSparseToUMA", 120),
    );
    print_result(
        "send_user_action_to_uma",
        metrics.send_user_action_to_uma("MetricsLibraryTestSendUserActionToUMA"),
    );
    print_result(
        "send_crash_to_uma",
        metrics.send_crash_to_uma("MetricsLibraryTestSendCrashToUMA"),
    );
    print_result(
        "send_cros_event_to_uma",
        metrics.send_cros_event_to_uma("Vm.VmcStart"),
    );

    println!(
        "are_metrics_enabled returned {}",
        metrics.are_metrics_enabled()
    );
}
