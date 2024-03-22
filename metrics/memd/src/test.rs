// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test code for memd.

use std::fs::{File, OpenOptions};
use std::io::Read;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::str;
use std::time::Duration;

use nix::sys::stat;
use nix::unistd;

// Imported from main program
use crate::DbusEvent;
use crate::Paths;
use crate::Result;
use crate::Sample;
use crate::SampleQueue;
use crate::SampleType;
use crate::Sampler;
use crate::Timer;
use crate::SAMPLE_QUEUE_LENGTH;

// Different levels of emulated available RAM in MB.
const LOW_MEM_LOW_AVAILABLE: u32 = 150;
const LOW_MEM_MEDIUM_AVAILABLE: u32 = 300;
const LOW_MEM_HIGH_AVAILABLE: u32 = 1000;
const LOW_MEM_MARGIN: u32 = 200;
const MOCK_DBUS_FIFO_NAME: &str = "mock-dbus-fifo";

macro_rules! print_to_path {
    ($path:expr, $format:expr $(, $arg:expr)*) => {{
        let r = OpenOptions::new().write(true).create(true).open($path);
        match r {
            Err(e) => Err(e),
            Ok(mut f) => f.write_all(format!($format $(, $arg)*).as_bytes())
        }
    }}
}

// The types of events which are generated internally for testing.  They
// simulate state changes (for instance, change in the memory pressure level),
// chrome events, and kernel events.
#[derive(Clone, Copy, Debug, PartialEq)]
enum TestEventType {
    EnterHighPressure,   // enter low available RAM (below margin) state
    EnterLowPressure,    // enter high available RAM state
    EnterMediumPressure, // set enough memory pressure to trigger fast sampling
    OomKillBrowser,      // fake browser report of OOM kill
    TabDiscard,          // fake browser report of tab discard
}

// Internally generated event for testing.
#[derive(Clone, Copy, Debug)]
struct TestEvent {
    time: i64,
    event_type: TestEventType,
}

// Real time mock, for testing only.  It removes time races (for better or
// worse) and makes it possible to run the test on build machines which may be
// heavily loaded.
//
// Time is mocked by assuming that CPU speed is infinite and time passes only
// when the program is asleep. Time advances in discrete jumps when we call
// either sleep() or select() with a timeout.

struct MockTimer {
    current_time: i64,                                       // the current time
    test_events: Vec<TestEvent>,                             // list events to be delivered
    event_index: usize,                                      // index of next event to be delivered
    dbus_event_sender: crossbeam_channel::Sender<DbusEvent>, // for sending D-Bus events
    quit_request: bool,                                      // for termination
    current_available_mb: u32,                               // the current available memory in MiB
}

impl MockTimer {
    fn new(
        test_events: Vec<TestEvent>,
        dbus_event_sender: crossbeam_channel::Sender<DbusEvent>,
    ) -> MockTimer {
        MockTimer {
            current_time: 0,
            test_events,
            event_index: 0,
            dbus_event_sender,
            quit_request: false,
            current_available_mb: LOW_MEM_HIGH_AVAILABLE,
        }
    }

    fn set_available_mb(&mut self, amount: u32) {
        self.current_available_mb = amount;
    }

    // Returns true if the event is delivered to the D-Bus channel.
    fn deliver_test_event(&mut self, time: i64, test_event: TestEvent) -> bool {
        debug!("delivering {:?}", test_event);
        match test_event.event_type {
            TestEventType::EnterLowPressure => {
                self.set_available_mb(LOW_MEM_HIGH_AVAILABLE);
                false
            }
            TestEventType::EnterMediumPressure => {
                self.set_available_mb(LOW_MEM_MEDIUM_AVAILABLE);
                false
            }
            TestEventType::EnterHighPressure => {
                self.set_available_mb(LOW_MEM_LOW_AVAILABLE);
                self.dbus_event_sender
                    .send(DbusEvent::CriticalMemoryPressure)
                    .unwrap();
                true
            }
            TestEventType::OomKillBrowser => {
                self.dbus_event_sender
                    .send(DbusEvent::OomKill { time })
                    .unwrap();
                true
            }
            TestEventType::TabDiscard => {
                self.dbus_event_sender
                    .send(DbusEvent::TabDiscard { time })
                    .unwrap();
                true
            }
        }
    }
}

impl Timer for MockTimer {
    fn now(&self) -> i64 {
        self.current_time
    }

    fn quit_request(&self) -> bool {
        self.quit_request
    }

    // Mock select first checks if any events are pending, then produces events
    // that would happen during its sleeping time, and checks if those events
    // are delivered.
    fn select(
        &mut self,
        _dbus_receiver: &crossbeam_channel::Receiver<DbusEvent>,
        timeout: Duration,
    ) -> Result<bool> {
        let timeout_ms = timeout.as_millis() as i64;
        let end_time = self.current_time + timeout_ms;
        // Assume no events occur and we hit the timeout.  Fix later as needed.
        self.current_time = end_time;
        loop {
            if self.event_index == self.test_events.len() {
                // No more events to deliver, so no need for further select() calls.
                self.quit_request = true;
                return Ok(false);
            }
            // There are still event to be delivered.
            let first_event_time = self.test_events[self.event_index].time;
            // We interpret the event time to be event.time + epsilon.  Thus if
            // |first_event_time| is equal to |end_time|, we time out.
            if first_event_time >= end_time {
                // No event to deliver before the timeout.
                debug!("returning because fev = {}", first_event_time);
                return Ok(false);
            }
            // Deliver all events with the time stamp of the first event.  (There
            // is at least one.)
            let mut channel_sent = 0;
            while {
                if self.deliver_test_event(first_event_time, self.test_events[self.event_index]) {
                    channel_sent += 1;
                }
                self.event_index += 1;
                self.event_index < self.test_events.len()
                    && self.test_events[self.event_index].time == first_event_time
            } {}
            // One or more events were delivered.
            if channel_sent > 0 {
                debug!(
                    "returning at {} with {} events",
                    first_event_time, channel_sent
                );
                self.current_time = first_event_time;
                return Ok(true);
            }
        }
    }

    fn get_available_mb(&self) -> Result<u32> {
        Ok(self.current_available_mb)
    }
}

pub fn test_loop(_always_poll_fast: bool, paths: &Paths) {
    for test_desc in TEST_DESCRIPTORS.iter() {
        // Every test run requires a (mock) restart of the daemon.
        println!("\n--------------\nrunning test:\n{}", test_desc);
        // Clean up log directory.
        std::fs::remove_dir_all(&paths.log_directory).expect("cannot remove /var/log/memd");
        std::fs::create_dir_all(&paths.log_directory).expect("cannot create /var/log/memd");

        let events = events_from_test_descriptor(test_desc);
        let (send, recv) = crossbeam_channel::unbounded();
        let timer = Box::new(MockTimer::new(events, send));
        let mut sampler = Sampler::new(false, paths, timer, recv, LOW_MEM_MARGIN)
            .expect("sampler creation error");
        loop {
            // Alternate between slow and fast poll.
            sampler.slow_poll().expect("slow poll error");
            if sampler.quit_request {
                break;
            }
            sampler.fast_poll().expect("fast poll error");
            if sampler.quit_request {
                break;
            }
        }
        verify_test_results(test_desc, &paths.log_directory)
            .unwrap_or_else(|_| panic!("test:{}failed.", test_desc));
        println!("test succeeded\n--------------");
    }
}

// ================
// Test Descriptors
// ================
//
// Define events and expected result using "ASCII graphics".
//
// The top lines of the test descriptor (all lines except the last one) define
// sequences of events.  The last line describes the expected result.
//
// Events are single characters:
//
// M = start medium pressure (fast poll)
// H = start high pressure (low-mem notification)
// L = start low pressure (slow poll)
// <digit> = tab discard
// K = kernel OOM kill
// k = chrome notification of OOM kill
// ' ', . = nop (just wait 1 second)
// | = ignored (no delay), cosmetic only
//
// - each character indicates a 1-second slot
// - events (if any) happen at the beginning of their slot
// - multiple events in the same slot are stacked vertically
//
// Example:
//
// ..H.1..L
//     2
//
// means:
//  - wait 2 seconds
//  - signal high-memory pressure, wait 1 second
//  - wait 1 second
//  - signal two tab discard events (named 1 and 2), wait 1 second
//  - wait 2 more seconds
//  - return to low-memory pressure
//
// The last line describes the expected clip logs.  Each log is identified by
// one digit: 0 for memd.clip000.log, 1 for memd.clip001.log etc.  The positions
// of the digits correspond to the time span covered by each clip.  So a clip
// file whose description is 5 characters long is supposed to contain 5 seconds
// worth of samples.
//
// For readability, the descriptor must start and end with newlines, which are
// removed.  Also, indentation (common all-space prefixes) is removed.

#[rustfmt::skip]
const TEST_DESCRIPTORS: &[&str] = &[

    // Very simple test: go from slow poll to fast poll and back.  No clips
    // are collected.
    "
    .M.L.
    .....
    ",

    // Simple test: start fast poll, signal low-mem, signal tab discard.
    "
    .M...H..1.....L
    ..00000000001..
    ",

    // Two full disjoint clips.  Also tests kernel-reported and chrome-reported OOM
    // kills.
    "
    .M......k.............k.....
    ...0000000000....1111111111.
    ",

    // Test that clip collection continues for the time span of interest even if
    // memory pressure returns quickly to a low level.  Note that the
    // medium-pressure event (M) is at t = 1s, but the fast poll starts at 2s
    // (multiple of 2s slow-poll period).
    "
    .MH1L.....
    ..000000..
    ",

    // Several discards, which result in three consecutive clips.  Tab discards 1
    // and 2 produce an 8-second clip because the first two seconds of data are
    // missing.  Also see the note above regarding fast poll start.
    "
    ...M.H12..|...3...6..|.7.....L
              |   4      |
              |   5      |
    ....000000|0011111111|112222..
    ",

    // Enter low-mem, then exit, then enter it again.
    "
    .MHM......|......H...|...L
    ..00000...|.111111111|1...
    ",

    // Discard a tab in slow-poll mode.
    "
    ....1.......
    ....00000...
    ",

];

fn trim_descriptor(descriptor: &str) -> Vec<Vec<u8>> {
    // Remove vertical bars.  Don't check for consistent use because it's easy
    // enough to notice visually.
    let barless_descriptor: String = descriptor.chars().filter(|c| *c != '|').collect();
    // Split string into lines.
    let all_lines: Vec<String> = barless_descriptor.split('\n').map(String::from).collect();
    // A test descriptor must start and end with empty lines, and have at least
    // one line of events, and exactly one line to describe the clip files.
    assert!(all_lines.len() >= 4, "invalid test descriptor format");
    // Remove first and last line.
    let valid_lines = all_lines[1..all_lines.len() - 1].to_vec();
    // Find indentation amount.  Unwrap() cannot fail because of previous assert.
    let indent = valid_lines
        .iter()
        .map(|s| s.len() - s.trim_start().len())
        .min()
        .unwrap();
    // Remove indentation.
    let trimmed_lines: Vec<Vec<u8>> = valid_lines
        .iter()
        .map(|s| s[indent..].to_string().into_bytes())
        .collect();
    trimmed_lines
}

fn events_from_test_descriptor(descriptor: &str) -> Vec<TestEvent> {
    let all_descriptors = trim_descriptor(descriptor);
    let event_sequences = &all_descriptors[..all_descriptors.len() - 1];
    let max_length = event_sequences.iter().map(|d| d.len()).max().unwrap();
    let mut events = vec![];
    for i in 0..max_length {
        for seq in event_sequences {
            // Each character represents one second.  Time unit is milliseconds.
            let mut opt_type = None;
            if i < seq.len() {
                match seq[i] {
                    b'0' | b'1' | b'2' | b'3' | b'4' | b'5' | b'6' | b'7' | b'8' | b'9' => {
                        opt_type = Some(TestEventType::TabDiscard)
                    }
                    b'H' => opt_type = Some(TestEventType::EnterHighPressure),
                    b'M' => opt_type = Some(TestEventType::EnterMediumPressure),
                    b'L' => opt_type = Some(TestEventType::EnterLowPressure),
                    b'k' => opt_type = Some(TestEventType::OomKillBrowser),
                    b'.' | b' ' | b'|' => {}
                    x => panic!("unexpected character {} in descriptor '{}'", &x, descriptor),
                }
            }
            if let Some(t) = opt_type {
                events.push(TestEvent {
                    time: i as i64 * 1000,
                    event_type: t,
                });
            }
        }
    }
    events
}

// Given a descriptor string for the expected clips, returns a vector of start
// and end time of each clip.
fn expected_clips(descriptor: &[u8]) -> Vec<(i64, i64)> {
    let mut time = 0;
    let mut clip_start_time = 0;
    let mut previous_clip = b'0' - 1;
    let mut previous_char = 0u8;
    let mut clips = vec![];

    for &c in descriptor {
        if c != previous_char {
            if (previous_char as char).is_ascii_digit() {
                // End of clip.
                clips.push((clip_start_time, time));
            }
            if (c as char).is_ascii_digit() {
                // Start of clip.
                clip_start_time = time;
                assert_eq!(c, previous_clip + 1, "malformed clip descriptor");
                previous_clip = c;
            }
        }
        previous_char = c;
        time += 1000;
    }
    clips
}

// Converts a string starting with a timestamp in seconds (#####.##, with two
// decimal digits) to a timestamp in milliseconds.
fn time_from_sample_string(line: &str) -> Result<i64> {
    let mut tokens = line.split(|c: char| !c.is_ascii_digit());
    let seconds = match tokens.next() {
        Some(digits) => digits.parse::<i64>().unwrap(),
        None => return Err("no digits in string".into()),
    };
    let centiseconds = match tokens.next() {
        Some(digits) => {
            if digits.len() == 2 {
                digits.parse::<i64>().unwrap()
            } else {
                return Err("expecting 2 decimals".into());
            }
        }
        None => return Err("expecting at least two groups of digits".into()),
    };
    Ok(seconds * 1000 + centiseconds * 10)
}

macro_rules! assert_approx_eq {
    ($actual:expr, $expected: expr, $tolerance: expr, $format:expr $(, $arg:expr)*) => {{
        let actual = $actual;
        let expected = $expected;
        let tolerance = $tolerance;
        let expected_min = expected - tolerance;
        let expected_max = expected + tolerance;
        assert!(actual < expected_max && actual > expected_min,
                concat!("(expected: {}, actual: {}) ", $format), expected, actual $(, $arg)*);
    }}
}

fn check_clip(clip_times: (i64, i64), clip_path: PathBuf, events: &[TestEvent]) -> Result<()> {
    let clip_name = clip_path.to_string_lossy();
    let mut clip_file = File::open(&clip_path)?;
    let mut file_content = String::new();
    clip_file.read_to_string(&mut file_content)?;
    debug!("clip {}:\n{}", clip_name, file_content);
    let lines = file_content.lines().collect::<Vec<&str>>();
    // First line is time stamp.  Second line is field names.  Check count of
    // field names and field values in the third line (don't bother to check
    // the other lines).
    let name_count = lines[1].split_whitespace().count();
    let value_count = lines[2].split_whitespace().count();
    assert_eq!(name_count, value_count);

    // Check first and last time stamps.
    let start_time = time_from_sample_string(lines[2]).expect("cannot parse first timestamp");
    let end_time =
        time_from_sample_string(lines[lines.len() - 1]).expect("cannot parse last timestamp");
    let expected_start_time = clip_times.0;
    let expected_end_time = clip_times.1;
    // Milliseconds of slack allowed on start/stop times.  We allow one full
    // fast poll period to take care of edge cases.  The specs don't need to be
    // tight here because it doesn't matter if we collect one fewer sample (or
    // an extra one) at each end.
    let slack_ms = 101i64;
    assert_approx_eq!(
        start_time,
        expected_start_time,
        slack_ms,
        "unexpected start time for {}",
        clip_name
    );
    assert_approx_eq!(
        end_time,
        expected_end_time,
        slack_ms,
        "unexpected end time for {}",
        clip_name
    );

    // Check sample count.  Must keep track of low_mem -> not low_mem transitions.
    let mut in_low_mem = false;
    let expected_sample_count_from_events: usize = events
        .iter()
        .map(|e| {
            let sample_count_for_event = if e.time <= start_time || e.time > end_time {
                0
            } else {
                match e.event_type {
                    // These generate 1 sample only when moving out of high pressure.
                    TestEventType::EnterLowPressure | TestEventType::EnterMediumPressure => {
                        if in_low_mem {
                            1
                        } else {
                            0
                        }
                    }
                    _ => 1,
                }
            };
            match e.event_type {
                TestEventType::EnterHighPressure => in_low_mem = true,
                TestEventType::EnterLowPressure | TestEventType::EnterMediumPressure => {
                    in_low_mem = false
                }
                _ => {}
            }
            sample_count_for_event
        })
        .sum();

    // We include samples both at the beginning and end of the range, so we
    // need to add 1.  Note that here we use the actual sample times, not the
    // expected times.
    let expected_sample_count_from_timer = ((end_time - start_time) / 100) as usize + 1;
    let expected_sample_count =
        expected_sample_count_from_events + expected_sample_count_from_timer;
    let sample_count = lines.len() - 2;
    assert_eq!(
        sample_count, expected_sample_count,
        "unexpected sample count for {}",
        clip_name
    );
    Ok(())
}

fn verify_test_results(descriptor: &str, log_directory: &Path) -> Result<()> {
    let all_descriptors = trim_descriptor(descriptor);
    let result_descriptor = &all_descriptors[all_descriptors.len() - 1];
    let clips = expected_clips(result_descriptor);
    let events = events_from_test_descriptor(descriptor);

    // Check that there are no more clips than expected.
    let files_count = std::fs::read_dir(log_directory)?.count();
    // Subtract one for the memd.parameters file.
    assert_eq!(clips.len(), files_count - 1, "wrong number of clip files");

    for (clip_number, clip) in clips.iter().enumerate() {
        let clip_path = log_directory.join(format!("memd.clip{:03}.log", clip_number));
        check_clip(*clip, clip_path, &events)?;
    }
    Ok(())
}

fn create_dir_all(path: &Path) -> Result<()> {
    let result = std::fs::create_dir_all(path);
    match result {
        Ok(_) => Ok(()),
        Err(e) => Err(format!("create_dir_all: {}: {:?}", path.to_string_lossy(), e).into()),
    }
}

pub fn teardown_test_environment(paths: &Paths) {
    std::fs::remove_dir_all(&paths.testing_root).unwrap_or_else(|_| {
        panic!(
            "teardown: could not remove {}",
            paths.testing_root.to_str().unwrap()
        )
    });
}

pub fn setup_test_environment(paths: &Paths) {
    std::fs::create_dir(&paths.testing_root)
        .unwrap_or_else(|_| panic!("cannot create {}", paths.testing_root.to_str().unwrap()));
    unistd::mkfifo(
        &paths.testing_root.join(MOCK_DBUS_FIFO_NAME),
        stat::Mode::S_IRWXU,
    )
    .expect("failed to make mock dbus fifo");
    let sys_vm = paths.testing_root.join("proc/sys/vm");
    create_dir_all(&sys_vm).expect("cannot create /proc/sys/vm");

    let zoneinfo_content = include_str!("zoneinfo_content");
    print_to_path!(&paths.zoneinfo, "{}", zoneinfo_content).expect("cannot initialize zoneinfo");

    print_to_path!(sys_vm.join("min_filelist_kbytes"), "100000\n")
        .expect("cannot initialize min_filelist_kbytes");
    print_to_path!(sys_vm.join("min_free_kbytes"), "80000\n")
        .expect("cannot initialize min_free_kbytes");
    print_to_path!(sys_vm.join("extra_free_kbytes"), "60000\n")
        .expect("cannot initialize extra_free_kbytes");
}

pub fn queue_loop() {
    let mut sq = SampleQueue::new();
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(false)
        .open("/dev/null")
        .unwrap();
    // We'll compare this uptime against the start_time of 0 in |output_from_time|, to ensure that
    // we don't stop looping in the array due to uptime.
    let s = Sample {
        uptime: 1,
        sample_type: SampleType::EnterLowMem,
        ..Default::default()
    };

    sq.samples = [s; SAMPLE_QUEUE_LENGTH];
    sq.head = 30;
    sq.count = 30;
    sq.output_from_time(&mut file, /*start_time=*/ 0).unwrap();
}
