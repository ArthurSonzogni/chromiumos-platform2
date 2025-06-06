// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/files/file_util.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/system/fake_system_utilities.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/system_utilities_constants.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::UnorderedElementsAreArray;
using VulnerabilityInfoMap =
    base::flat_map<std::string, mojom::VulnerabilityInfoPtr>;

// POD struct for ParseCpuArchitectureTest.
struct ParseCpuArchitectureTestParams {
  std::string uname_machine;
  mojom::CpuArchitectureEnum expected_mojo_enum;
};

// No other logical IDs should be used, or the logic for writing C-state files
// will break.
constexpr int kFirstLogicalId = 0;
constexpr int kSecondLogicalId = 1;
constexpr int kThirdLogicalId = 12;

// First C-State directory to be written.
constexpr char kFirstCStateDir[] = "state0";

constexpr char kNonIntegralFileContents[] = "Not an integer!";

constexpr char kHardwareDescriptionCpuinfoContents[] =
    "Hardware\t: Rockchip (Device Tree)\nRevision\t: 0000\nSerial\t: "
    "0000000000000000\n\n";
constexpr char kNoModelNameCpuinfoContents[] = "processor\t: 0\nflags\t:\n\n";
constexpr char kFakeCpuinfoContents[] =
    "processor\t: 0\nmodel name\t: Dank CPU 1 @ 8.90GHz\nflags\t:\n\n"
    "processor\t: 1\nmodel name\t: Dank CPU 1 @ 8.90GHz\nflags\t:\n\n"
    "processor\t: 12\nmodel name\t: Dank CPU 2 @ 2.80GHz\nflags\t:\n\n";
constexpr char kFirstFakeModelName[] = "Dank CPU 1 @ 8.90GHz";
constexpr char kSecondFakeModelName[] = "Dank CPU 2 @ 2.80GHz";

constexpr uint32_t kFirstFakeMaxClockSpeed = 3400000;
constexpr uint32_t kSecondFakeMaxClockSpeed = 1600000;
constexpr uint32_t kThirdFakeMaxClockSpeed = 1800000;

constexpr char kBadPresentContents[] = "Char-7";
constexpr char kFakePresentContents[] = "0-7";
constexpr uint32_t kExpectedNumTotalThreads = 8;

constexpr uint32_t kFirstFakeScalingCurrentFrequency = 859429;
constexpr uint32_t kSecondFakeScalingCurrentFrequency = 637382;
constexpr uint32_t kThirdFakeScalingCurrentFrequency = 737382;

constexpr uint32_t kFirstFakeScalingMaxFrequency = 2800000;
constexpr uint32_t kSecondFakeScalingMaxFrequency = 1400000;
constexpr uint32_t kThirdFakeScalingMaxFrequency = 1700000;

constexpr char kFirstFakeCStateNameContents[] = "C1-SKL";
constexpr uint64_t kFirstFakeCStateTime = 536018855;
constexpr char kSecondFakeCStateNameContents[] = "C10-SKL";
constexpr uint64_t kSecondFakeCStateTime = 473634000891;
constexpr char kThirdFakeCStateNameContents[] = "C7s-SKL";
constexpr uint64_t kThirdFakeCStateTime = 473634000891;
constexpr char kFourthFakeCStateNameContents[] = "C1E-SKL";
constexpr uint64_t kFourthFakeCStateTime = 79901786;

constexpr char kBadStatContents[] =
    "cpu   12389 69724 98732420 420347203\ncpu0  0 10 890 473634000891\n";
constexpr char kMissingLogicalCpuStatContents[] =
    "cpu   12389 69724 98732420 420347203\n"
    "cpu0  69234 98 0 2349\n"
    "cpu12 0 64823 293802 871239\n";
constexpr char kFakeStatContents[] =
    "cpu   12389 69724 98732420 420347203\n"
    "cpu0  69234 98 0 2349\n"
    "cpu1  989 0 4536824 123\n"
    "cpu12 0 64823 293802 871239\n";
constexpr uint64_t kFirstFakeUserTime = 69234 + 98;
constexpr uint64_t kFirstFakeSystemTime = 0;
constexpr uint32_t kFirstFakeIdleTime = 2349;
constexpr uint64_t kSecondFakeUserTime = 989 + 0;
constexpr uint64_t kSecondFakeSystemTime = 4536824;
constexpr uint32_t kSecondFakeIdleTime = 123;
constexpr uint64_t kThirdFakeUserTime = 0 + 64823;
constexpr uint64_t kThirdFakeSystemTime = 293802;
constexpr uint32_t kThirdFakeIdleTime = 871239;

constexpr char kFirstFakeCpuTemperatureDir[] =
    "sys/class/thermal/thermal_zone0";
constexpr int32_t kFirstFakeCpuTemperature = -186;
constexpr int32_t kFirstFakeCpuTemperatureMilliDegrees =
    kFirstFakeCpuTemperature * 1000;
constexpr char kFirstFakeCpuTemperatureLabel[] = "x86_pkg_temp";
constexpr char kSecondFakeCpuTemperatureDir[] =
    "sys/class/thermal/thermal_zone1";
constexpr int32_t kSecondFakeCpuTemperature = 99;
constexpr int32_t kSecondFakeCpuTemperatureMilliDegrees =
    kSecondFakeCpuTemperature * 1000;
constexpr char kSecondFakeCpuTemperatureLabel[] = "x86_pkg_temp";
constexpr char kThirdFakeCpuTemperatureDir[] =
    "sys/class/thermal/thermal_zone2";
constexpr int32_t kThirdFakeCpuTemperature = 25;
constexpr int32_t kThirdFakeCpuTemperatureMilliDegrees =
    kThirdFakeCpuTemperature * 1000;
constexpr char kThirdFakeCpuTemperatureLabel[] = "cpu0-thermal";

class FakeUdevDevice : public brillo::MockUdevDevice {
 public:
  FakeUdevDevice(std::optional<std::string> type,
                 std::string temperature,
                 base::FilePath syspath)
      : type_(type), temperature_(temperature), syspath_(syspath) {
    EXPECT_CALL(*this, GetSysAttributeValue)
        .WillRepeatedly([this](const char* key) {
          if (std::string(key) == kThermalAttributeType) {
            if (type_.has_value()) {
              return type_.value().c_str();
            }
            return (const char*)nullptr;
          } else if (std::string(key) == kThermalAttributeTemperature) {
            return temperature_.c_str();
          } else {
            NOTREACHED() << "Unknown key: " << key;
          }
        });
    ON_CALL(*this, GetSysPath).WillByDefault([this]() {
      return syspath_.value().c_str();
    });
  }
  ~FakeUdevDevice() = default;

 private:
  std::optional<std::string> type_;
  std::string temperature_;
  base::FilePath syspath_;
};

constexpr char kFakeCryptoContents[] =
    "name\t: crypto_name\n"
    "driver\t: driver_name\n"
    "module\t: module_name\n";

constexpr char kSoCIDContents[] = "jep106:0426:8192\n";

// Workaround matchers for UnorderedElementsAreArray not accepting
// move-only types.

// This matcher expects a std::cref(mojom::CStateInfoPtr) and
// checks each of the fields for equality.
MATCHER_P(MatchesCStateInfoPtr, ptr, "") {
  return arg->name == ptr.get()->name &&
         arg->time_in_state_since_last_boot_us ==
             ptr.get()->time_in_state_since_last_boot_us;
}

// This matcher expects a std::cref(mojom::CpuTemperatureChannelPtr) and
// checks each of the fields for equality.
MATCHER_P(MatchesCpuTemperatureChannelPtr, ptr, "") {
  return arg->label == ptr.get()->label &&
         arg->temperature_celsius == ptr.get()->temperature_celsius;
}

// Note that this function only works for Logical CPUs with one or two C-states.
// Luckily, that's all we need for solid unit tests.
void VerifyLogicalCpu(
    uint32_t expected_max_clock_speed_khz,
    uint32_t expected_scaling_max_frequency_khz,
    uint32_t expected_scaling_current_frequency_khz,
    uint32_t expected_user_time_user_hz,
    uint32_t expected_system_time_user_hz,
    uint64_t expected_idle_time_user_hz,
    const std::vector<std::pair<std::string, uint64_t>>& expected_c_states,
    const mojom::LogicalCpuInfoPtr& actual_data) {
  ASSERT_FALSE(actual_data.is_null());
  EXPECT_EQ(actual_data->max_clock_speed_khz, expected_max_clock_speed_khz);
  EXPECT_EQ(actual_data->scaling_max_frequency_khz,
            expected_scaling_max_frequency_khz);
  EXPECT_EQ(actual_data->scaling_current_frequency_khz,
            expected_scaling_current_frequency_khz);
  EXPECT_EQ(actual_data->user_time_user_hz, expected_user_time_user_hz);
  EXPECT_EQ(actual_data->system_time_user_hz, expected_system_time_user_hz);
  EXPECT_EQ(actual_data->idle_time_user_hz, expected_idle_time_user_hz);

  const auto& c_states = actual_data->c_states;
  int c_state_size = c_states.size();
  int expected_c_state_size = expected_c_states.size();
  ASSERT_TRUE(c_state_size == expected_c_state_size &&
              (c_state_size == 1 || c_state_size == 2));
  if (c_state_size == 1) {
    const auto& c_state = c_states[0];
    ASSERT_FALSE(c_state.is_null());
    const auto& expected_c_state = expected_c_states[0];
    EXPECT_EQ(c_state->name, expected_c_state.first);
    EXPECT_EQ(c_state->time_in_state_since_last_boot_us,
              expected_c_state.second);
  } else {
    // Since fetching C-states uses base::FileEnumerator, we're not guaranteed
    // the order of the two results.
    auto first_expected_c_state = mojom::CpuCStateInfo::New(
        expected_c_states[0].first, expected_c_states[0].second);
    auto second_expected_c_state = mojom::CpuCStateInfo::New(
        expected_c_states[1].first, expected_c_states[1].second);
    EXPECT_THAT(
        c_states,
        UnorderedElementsAreArray(
            {MatchesCStateInfoPtr(std::cref(first_expected_c_state)),
             MatchesCStateInfoPtr(std::cref(second_expected_c_state))}));
  }
}

// Verifies that the two received CPU temperature channels have the correct
// values for X86.
void VerifyCpuTempsX86(
    const std::vector<mojom::CpuTemperatureChannelPtr>& cpu_temps) {
  ASSERT_EQ(cpu_temps.size(), 2);

  // Since fetching temperatures uses base::FileEnumerator, we're not
  // guaranteed the order of the two results.
  auto first_expected_temp = mojom::CpuTemperatureChannel::New(
      kFirstFakeCpuTemperatureLabel, kFirstFakeCpuTemperature);
  auto second_expected_temp = mojom::CpuTemperatureChannel::New(
      kSecondFakeCpuTemperatureLabel, kSecondFakeCpuTemperature);
  EXPECT_THAT(
      cpu_temps,
      UnorderedElementsAreArray(
          {MatchesCpuTemperatureChannelPtr(std::cref(first_expected_temp)),
           MatchesCpuTemperatureChannelPtr(std::cref(second_expected_temp))}));
}

// Verifies that the one received CPU temperature channel have the correct
// values for Arm.
void VerifyCpuTempsArm(
    const std::vector<mojom::CpuTemperatureChannelPtr>& cpu_temps) {
  ASSERT_EQ(cpu_temps.size(), 1);

  // Since fetching temperatures uses base::FileEnumerator, we're not
  // guaranteed the order of the two results.
  ASSERT_EQ(cpu_temps.size(), 1);
  const auto& temp = cpu_temps[0];
  ASSERT_FALSE(temp.is_null());
  ASSERT_TRUE(temp->label.has_value());
  EXPECT_EQ(temp->label.value(), kThirdFakeCpuTemperatureLabel);
  EXPECT_EQ(temp->temperature_celsius, kThirdFakeCpuTemperature);
}

class CpuFetcherTest : public BaseFileTest {
 protected:
  CpuFetcherTest() = default;

  void SetUp() override {
    // Set up valid files for two physical CPUs, the first of which has two
    // logical CPUs. Individual tests are expected to override this
    // configuration when necessary.

    // Write /proc/cpuinfo.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                             kFakeCpuinfoContents));
    // Write /proc/stat.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcStatPath(GetRootDir()),
                                             kFakeStatContents));
    // Write /sys/devices/system/cpu/present.
    SetFile({kRelativeCpuDir, kPresentFileName}, kFakePresentContents);
    // Write policy data for the first logical CPU.
    WritePolicyData(base::NumberToString(kFirstFakeMaxClockSpeed),
                    base::NumberToString(kFirstFakeScalingMaxFrequency),
                    base::NumberToString(kFirstFakeScalingCurrentFrequency),
                    kFirstLogicalId);
    // Write policy data for the second logical CPU.
    WritePolicyData(base::NumberToString(kSecondFakeMaxClockSpeed),
                    base::NumberToString(kSecondFakeScalingMaxFrequency),
                    base::NumberToString(kSecondFakeScalingCurrentFrequency),
                    kSecondLogicalId);
    // Write policy data for the third logical CPU.
    WritePolicyData(base::NumberToString(kThirdFakeMaxClockSpeed),
                    base::NumberToString(kThirdFakeScalingMaxFrequency),
                    base::NumberToString(kThirdFakeScalingCurrentFrequency),
                    kThirdLogicalId);
    // Write C-state data for the first logical CPU.
    WriteCStateData(kFirstCStates, kFirstLogicalId);
    // Write C-state data for the second logical CPU.
    WriteCStateData(kSecondCStates, kSecondLogicalId);
    // Write C-state data for the third logical CPU.
    WriteCStateData(kThirdCStates, kThirdLogicalId);

    // Write physical ID data for the first logical CPU.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetPhysicalPackageIdPath(GetRootDir(), kFirstLogicalId), "0"));
    // Write physical ID data for the second logical CPU.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetPhysicalPackageIdPath(GetRootDir(), kSecondLogicalId), "0"));
    // Write physical ID data for the third logical CPU.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetPhysicalPackageIdPath(GetRootDir(), kThirdLogicalId), "1"));

    // Write core ID data for the first logical CPU.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetCoreIdPath(GetRootDir(), kFirstLogicalId), "0"));
    // Write core ID data for the second logical CPU.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetCoreIdPath(GetRootDir(), kSecondLogicalId), "0"));
    // Write core ID data for the third logical CPU.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetCoreIdPath(GetRootDir(), kThirdLogicalId), "0"));

    // Write CPU temperature data.
    SetFile({kFirstFakeCpuTemperatureDir, kThermalAttributeTemperature},
            base::NumberToString(kFirstFakeCpuTemperatureMilliDegrees));
    SetFile({kFirstFakeCpuTemperatureDir, kThermalAttributeType},
            kFirstFakeCpuTemperatureLabel);
    SetFile({kSecondFakeCpuTemperatureDir, kThermalAttributeTemperature},
            base::NumberToString(kSecondFakeCpuTemperatureMilliDegrees));
    SetFile({kSecondFakeCpuTemperatureDir, kThermalAttributeType},
            kSecondFakeCpuTemperatureLabel);
    SetFile({kThirdFakeCpuTemperatureDir, kThermalAttributeTemperature},
            base::NumberToString(kThirdFakeCpuTemperatureMilliDegrees));
    SetFile({kThirdFakeCpuTemperatureDir, kThermalAttributeType},
            kThirdFakeCpuTemperatureLabel);

    // Write /proc/crypto.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCryptoPath(GetRootDir()),
                                             kFakeCryptoContents));
    // Set the fake uname response.
    fake_system_utils()->SetUnameResponse(/*ret_code=*/0, kUnameMachineX86_64);
    // Write the virtualization files.
    SetupDefaultVirtualizationFiles();
    MockUdevDevice();
  }

  void TearDown() override {
    // Wait for all task to be done.
    task_environment_.RunUntilIdle();
  }

  // Write the fake vulnerability files for unit testing.
  void SetVulnerabiility(const std::string& filename,
                         const std::string& content) {
    SetFile({kRelativeCpuDir, kVulnerabilityDirName, filename}, content);
  }

  void SetupDefaultVirtualizationFiles() {
    SetFile({kRelativeCpuDir, kSmtDirName, kSmtActiveFileName}, "1");
    SetFile({kRelativeCpuDir, kSmtDirName, kSmtControlFileName}, "on");
  }

  // Customize UdevDevice's behavior.
  std::function<std::unique_ptr<FakeUdevDevice>(const char*)>
  MockUdevDeviceFunc(bool without_lable = false,
                     bool incorrect_format = false) {
    return [=, this](const char* syspath) {
      base::FilePath sys_file_path = base::FilePath{syspath};
      if (sys_file_path == GetPathUnderRoot(kFirstFakeCpuTemperatureDir)) {
        if (without_lable) {
          // return one thermal zone without device type.
          return std::make_unique<FakeUdevDevice>(
              std::nullopt,
              std::to_string(kFirstFakeCpuTemperatureMilliDegrees),
              sys_file_path);
        } else if (incorrect_format) {
          // return one thermal zone with incorrect format.
          return std::make_unique<FakeUdevDevice>(kFirstFakeCpuTemperatureLabel,
                                                  kNonIntegralFileContents,
                                                  sys_file_path);
        } else {
          return std::make_unique<FakeUdevDevice>(
              kFirstFakeCpuTemperatureLabel,
              std::to_string(kFirstFakeCpuTemperatureMilliDegrees),
              sys_file_path);
        }
      } else if (sys_file_path ==
                 GetPathUnderRoot(kSecondFakeCpuTemperatureDir)) {
        return std::make_unique<FakeUdevDevice>(
            kSecondFakeCpuTemperatureLabel,
            std::to_string(kSecondFakeCpuTemperatureMilliDegrees),
            sys_file_path);
      } else if (sys_file_path ==
                 GetPathUnderRoot(kThirdFakeCpuTemperatureDir)) {
        return std::make_unique<FakeUdevDevice>(
            kThirdFakeCpuTemperatureLabel,
            std::to_string(kThirdFakeCpuTemperatureMilliDegrees),
            sys_file_path);
      } else {
        std::unique_ptr<FakeUdevDevice> udevice(nullptr);
        return udevice;
      }
    };
  }

  void MockUdevDevice() {
    ON_CALL(*mock_context_.mock_udev(), CreateDeviceFromSysPath)
        .WillByDefault(MockUdevDeviceFunc());
  }

  void MockUdevDeviceWithOneIncorrectFormat() {
    EXPECT_CALL(*mock_context_.mock_udev(), CreateDeviceFromSysPath)
        .WillRepeatedly(MockUdevDeviceFunc(false, true));
  }

  void MockUdevDeviceWithOneMissingType() {
    EXPECT_CALL(*mock_context_.mock_udev(), CreateDeviceFromSysPath)
        .WillRepeatedly(MockUdevDeviceFunc(true, false));
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  FakeSystemUtilities* fake_system_utils() const {
    return mock_context_.fake_system_utils();
  }

  mojom::CpuResultPtr FetchCpuInfoSync() {
    base::test::TestFuture<mojom::CpuResultPtr> future;
    FetchCpuInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  const std::vector<std::pair<std::string, uint64_t>>& GetCStateVector(
      int logical_id) {
    if (logical_id == kFirstLogicalId) {
      return kFirstCStates;
    } else if (logical_id == kSecondLogicalId) {
      return kSecondCStates;
    } else if (logical_id == kThirdLogicalId) {
      return kThirdCStates;
    }
    NOTREACHED();
  }

  // Verifies that the received PhysicalCpuInfoPtrs matched the expected default
  // value.
  void VerifyPhysicalCpus(
      const std::vector<mojom::PhysicalCpuInfoPtr>& physical_cpus) {
    ASSERT_EQ(physical_cpus.size(), 2);
    const auto& first_physical_cpu = physical_cpus[0];
    ASSERT_FALSE(first_physical_cpu.is_null());
    EXPECT_EQ(first_physical_cpu->model_name, kFirstFakeModelName);
    const auto& first_logical_cpus = first_physical_cpu->logical_cpus;
    ASSERT_EQ(first_logical_cpus.size(), 2);
    VerifyLogicalCpu(kFirstFakeMaxClockSpeed, kFirstFakeScalingMaxFrequency,
                     kFirstFakeScalingCurrentFrequency, kFirstFakeUserTime,
                     kFirstFakeSystemTime, kFirstFakeIdleTime,
                     GetCStateVector(kFirstLogicalId), first_logical_cpus[0]);
    VerifyLogicalCpu(kSecondFakeMaxClockSpeed, kSecondFakeScalingMaxFrequency,
                     kSecondFakeScalingCurrentFrequency, kSecondFakeUserTime,
                     kSecondFakeSystemTime, kSecondFakeIdleTime,
                     GetCStateVector(kSecondLogicalId), first_logical_cpus[1]);
    const auto& second_physical_cpu = physical_cpus[1];
    ASSERT_FALSE(second_physical_cpu.is_null());
    EXPECT_EQ(second_physical_cpu->model_name, kSecondFakeModelName);
    const auto& second_logical_cpus = second_physical_cpu->logical_cpus;
    ASSERT_EQ(second_logical_cpus.size(), 1);
    VerifyLogicalCpu(kThirdFakeMaxClockSpeed, kThirdFakeScalingMaxFrequency,
                     kThirdFakeScalingCurrentFrequency, kThirdFakeUserTime,
                     kThirdFakeSystemTime, kThirdFakeIdleTime,
                     GetCStateVector(kThirdLogicalId), second_logical_cpus[0]);
  }

  void SetReadMsrResponse(uint32_t expected_msr_reg,
                          uint32_t expected_logical_id,
                          uint64_t expected_val) {
    EXPECT_CALL(*mock_executor(),
                ReadMsr(expected_msr_reg, expected_logical_id, _))
        .WillRepeatedly(
            [expected_val](uint32_t msr_reg, uint32_t cpu_index,
                           mojom::Executor::ReadMsrCallback callback) {
              std::move(callback).Run(expected_val);
            });
  }

 private:
  // Writes pairs of data into the name and time files of the appropriate
  // C-state directory.
  void WriteCStateData(
      const std::vector<std::pair<std::string, uint64_t>>& data,
      int logical_id) {
    for (const auto& [name, time] : data) {
      WriteCStateFiles(logical_id, name, base::NumberToString(time));
    }
  }

  // Writes to cpuinfo_max_freq, scaling_max_freq, and scaling_cur_freq. If any
  // of the optional values are std::nullopt, the corresponding file will not
  // be written.
  void WritePolicyData(const std::string cpuinfo_max_freq_contents,
                       const std::string scaling_max_freq_contents,
                       const std::string scaling_cur_freq_contents,
                       int logical_id) {
    WritePolicyFile(logical_id, kCpuinfoMaxFreqFileName,
                    cpuinfo_max_freq_contents);

    WritePolicyFile(logical_id, kScalingMaxFreqFileName,
                    scaling_max_freq_contents);

    WritePolicyFile(logical_id, kScalingCurFreqFileName,
                    scaling_cur_freq_contents);
  }

  // Helper to write individual C-state files.
  void WriteCStateFiles(int logical_id,
                        const std::string& name_contents,
                        const std::string& time_contents) {
    auto policy_dir = GetCStateDirectoryPath(GetRootDir(), logical_id);
    int state_to_write = c_states_written[logical_id];
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        policy_dir.Append("state" + base::NumberToString(state_to_write))
            .Append(kCStateNameFileName),
        name_contents));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        policy_dir.Append("state" + base::NumberToString(state_to_write))
            .Append(kCStateTimeFileName),
        time_contents));
    c_states_written[logical_id] += 1;
  }

  // Helper to write individual policy files.
  void WritePolicyFile(int logical_id,
                       const std::string& file_name,
                       const std::string& file_contents) {
    auto policy_dir = GetCpuFreqDirectoryPath(GetRootDir(), logical_id);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(policy_dir.Append(file_name),
                                             file_contents));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  // Records the next C-state file to be written.
  std::map<int, int> c_states_written = {
      {kFirstLogicalId, 0}, {kSecondLogicalId, 0}, {kThirdLogicalId, 0}};
  // C-state data for each of the three logical CPUs tested.
  const std::vector<std::pair<std::string, uint64_t>> kFirstCStates = {
      {kFirstFakeCStateNameContents, kFirstFakeCStateTime},
      {kSecondFakeCStateNameContents, kSecondFakeCStateTime}};
  const std::vector<std::pair<std::string, uint64_t>> kSecondCStates = {
      {kThirdFakeCStateNameContents, kThirdFakeCStateTime}};
  const std::vector<std::pair<std::string, uint64_t>> kThirdCStates = {
      {kFourthFakeCStateNameContents, kFourthFakeCStateTime}};
};

// Test that CPU info can be read when it exists.
TEST_F(CpuFetcherTest, TestFetchCpu) {
  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, kExpectedNumTotalThreads);
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kX86_64);
  VerifyPhysicalCpus(cpu_info->physical_cpus);
}

TEST_F(CpuFetcherTest, TestParseCpuTempX86) {
  fake_system_utils()->SetUnameResponse(/*ret_code=*/0, kUnameMachineX86_64);
  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kX86_64);
  VerifyCpuTempsX86(cpu_info->temperature_channels);
}

TEST_F(CpuFetcherTest, TestParseCpuTempArm) {
  fake_system_utils()->SetUnameResponse(/*ret_code=*/0, kUnameMachineArmv7l);
  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kArmv7l);
  VerifyCpuTempsArm(cpu_info->temperature_channels);
}

// Test that we handle a cpuinfo file for processors without physical_ids.
TEST_F(CpuFetcherTest, NoPhysicalIdFile) {
  ASSERT_TRUE(brillo::DeleteFile(GetPhysicalPackageIdPath(GetRootDir(), 0)));

  auto cpu_result = FetchCpuInfoSync();
  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle a missing cpuinfo file.
TEST_F(CpuFetcherTest, MissingCpuinfoFile) {
  ASSERT_TRUE(brillo::DeleteFile(GetProcCpuInfoPath(GetRootDir())));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle a cpuinfo file with a hardware description block.
TEST_F(CpuFetcherTest, HardwareDescriptionCpuinfoFile) {
  std::string cpu_info_contents = kFakeCpuinfoContents;
  cpu_info_contents += kHardwareDescriptionCpuinfoContents;
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           cpu_info_contents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, kExpectedNumTotalThreads);
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kX86_64);
  VerifyPhysicalCpus(cpu_info->physical_cpus);
  VerifyCpuTempsX86(cpu_info->temperature_channels);
}

// Test that we handle a cpuinfo file without a model name.
TEST_F(CpuFetcherTest, NoModelNameCpuinfoFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);
  EXPECT_FALSE(
      cpu_result->get_cpu_info()->physical_cpus[0]->model_name.has_value());
}

// Test that we handle a cpuinfo file without any CPU Flags.
TEST_F(CpuFetcherTest, NoCpuFlagsCpuinfoFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcCpuInfoPath(GetRootDir()),
      "processor\t: 0\nmodel name\t: Dank CPU 1 @ 8.90GHz\n\n"));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle a cpuinfo file with valid CPU Flags.
TEST_F(CpuFetcherTest, ValidX86CpuFlagsCpuinfoFile) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                   "processor\t: 0\nmodel name\t: Dank CPU 1 @ "
                                   "8.90GHz\nflags\t: f1 f2 f3\n\n"));

  std::vector<std::string> expected{"f1", "f2", "f3"};

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus[0]->flags, expected);
}

// Test that we handle a cpuinfo file with valid CPU Flags.
TEST_F(CpuFetcherTest, ValidArmCpuFlagsCpuinfoFile) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                   "processor\t: 0\nmodel name\t: Dank CPU 1 @ "
                                   "8.90GHz\nFeatures\t: f1 f2 f3\n\n"));

  std::vector<std::string> expected{"f1", "f2", "f3"};

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus[0]->flags, expected);
}

// Test that we have soc_id for Arm devices that don't have a specific driver.
TEST_F(CpuFetcherTest, ModelNameFromJEP106SoCID) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));
  SetFile({kRelativeSoCDevicesDir, "soc0", "soc_id"}, kSoCIDContents);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "MediaTek 8192");
}

// Test that we have soc_id for Qualcomm devices.
TEST_F(CpuFetcherTest, ModelNameFromQualcommSoCID) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));

  // For Arm devices we _should_ just be looking at the "family" and
  // "machine" files, but throw others in there (based on a real device)
  // to make sure it doesn't confuse the parser.
  SetFile({kRelativeSoCDevicesDir, "soc0", "family"}, "jep106:0070\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "soc_id"}, "jep106:0070:01a9\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "family"}, "Snapdragon\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "soc_id"}, "425\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "machine"}, "SC7180\n");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "Qualcomm Snapdragon SC7180");
}

// Test that the jep106 SoC ID doesn't confuse us even after upstream
// commit 3f84aa5ec052 ("base: soc: populate machine name in
// soc_device_register if empty").
TEST_F(CpuFetcherTest, ModelNameFromQualcommSoCIDNew) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));

  SetFile({kRelativeSoCDevicesDir, "soc0", "family"}, "jep106:0070\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "machine"},
          "Google Lazor (rev9+) with LTE\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "soc_id"}, "jep106:0070:01a9\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "family"}, "Snapdragon\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "soc_id"}, "425\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "machine"}, "SC7180\n");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "Qualcomm Snapdragon SC7180");
}

// Test that we're not confused even if some other SoC driver somehow shows up.
TEST_F(CpuFetcherTest, ModelNameFromQualcommSoCIDWithBogus) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));

  SetFile({kRelativeSoCDevicesDir, "soc0", "family"}, "jep106:0070\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "machine"},
          "Google Lazor (rev9+) with LTE\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "soc_id"}, "jep106:0070:01a9\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "family"}, "Imaginary\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "soc_id"}, "1\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "machine"}, "sqrt(-1)\n");
  SetFile({kRelativeSoCDevicesDir, "soc2", "family"}, "Snapdragon\n");
  SetFile({kRelativeSoCDevicesDir, "soc2", "soc_id"}, "425\n");
  SetFile({kRelativeSoCDevicesDir, "soc2", "machine"}, "SC7180\n");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "Qualcomm Snapdragon SC7180");
}

// Test that we have SoC information in legacy theme for MediaTek devices.
TEST_F(CpuFetcherTest, ModelNameFromMediaTekSoCIDLegacy) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));

  // For MediaTek devices with older socinfo driver, "soc_id" is empty in soc0/.
  // In this case, we just check the "family" and "machine" files, but throw
  // others in there (based on a real device) to make sure it doesn't confuse
  // the parser.
  SetFile({kRelativeSoCDevicesDir, "soc0", "family"}, "MediaTek\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "machine"},
          "Kompanio 520 (MT8186)\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "family"}, "jep106:0426\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "soc_id"}, "jep106:0426:8186\n");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "MediaTek Kompanio 520 (MT8186)");
}

// Test that we have SoC information in new theme for MediaTek devices.
TEST_F(CpuFetcherTest, ModelNameFromMediaTekSoCIDNew) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));

  // For MediaTek devices with newer socinfo driver, it's "soc_id" that exposes
  // SoC name.
  // In this case, we check the "family" and "soc_id" files to compose the SoC
  // ID.
  SetFile({kRelativeSoCDevicesDir, "soc0", "family"},
          "MediaTek Kompanio 838\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "machine"},
          "Google Ciri sku2 board\n");
  SetFile({kRelativeSoCDevicesDir, "soc0", "soc_id"}, "MT8188\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "family"}, "jep106:0426\n");
  SetFile({kRelativeSoCDevicesDir, "soc1", "soc_id"}, "jep106:0426:8188\n");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "MediaTek Kompanio 838");
}

// Test that we have device tree compatible string for Arm devices.
TEST_F(CpuFetcherTest, ModelNameFromCompatibleString) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(GetRootDir()),
                                           kNoModelNameCpuinfoContents));
  constexpr uint8_t data[] = {'g', 'o', 'o', 'g',  'l', 'e', ',', 'h', 'a', 'y',
                              'a', 't', 'o', '\0', 'm', 'e', 'd', 'i', 'a', 't',
                              'e', 'k', ',', '8',  '1', '9', '2', '\0'};
  SetFile(kRelativeCompatibleFile, data);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);

  auto model_name = cpu_result->get_cpu_info()->physical_cpus[0]->model_name;
  EXPECT_TRUE(model_name.has_value());
  ASSERT_EQ(model_name.value(), "MediaTek 8192");
}

// Test that we handle a missing stat file.
TEST_F(CpuFetcherTest, MissingStatFile) {
  ASSERT_TRUE(brillo::DeleteFile(GetProcStatPath(GetRootDir())));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle an incorrectly-formatted stat file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedStatFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcStatPath(GetRootDir()),
                                           kBadStatContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle a stat file which is missing an entry for an existing
// logical CPU.
TEST_F(CpuFetcherTest, StatFileMissingLogicalCpuEntry) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcStatPath(GetRootDir()),
                                           kMissingLogicalCpuStatContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle a missing present file.
TEST_F(CpuFetcherTest, MissingPresentFile) {
  UnsetPath({kRelativeCpuDir, kPresentFileName});

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted present file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedPresentFile) {
  SetFile({kRelativeCpuDir, kPresentFileName}, kBadPresentContents);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle a single threaded present file.
TEST_F(CpuFetcherTest, SingleThreadedPresentFile) {
  SetFile({kRelativeCpuDir, kPresentFileName}, "0");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, 1);
}

// Test that we handle a complexly-formatted present file.
TEST_F(CpuFetcherTest, ComplexlyFormattedPresentFile) {
  SetFile({kRelativeCpuDir, kPresentFileName}, "0,2-3,5-7");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, 6);
}

// Test that we handle a missing cpuinfo_freq directory.
TEST_F(CpuFetcherTest, MissingCpuinfoFreqDirectory) {
  ASSERT_TRUE(brillo::DeletePathRecursively(
      GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  const auto& logical_cpu_1 = cpu_info->physical_cpus[0]->logical_cpus[0];
  EXPECT_EQ(logical_cpu_1->max_clock_speed_khz, 0);
  EXPECT_EQ(logical_cpu_1->scaling_max_frequency_khz, 0);
  EXPECT_EQ(logical_cpu_1->scaling_current_frequency_khz, 0);
}

// Test that we handle a missing cpuinfo_max_freq file.
TEST_F(CpuFetcherTest, MissingCpuinfoMaxFreqFile) {
  ASSERT_TRUE(
      brillo::DeleteFile(GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)
                             .Append(kCpuinfoMaxFreqFileName)));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted cpuinfo_max_freq file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedCpuinfoMaxFreqFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)
          .Append(kCpuinfoMaxFreqFileName),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle a missing scaling_max_freq file.
TEST_F(CpuFetcherTest, MissingScalingMaxFreqFile) {
  ASSERT_TRUE(
      brillo::DeleteFile(GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)
                             .Append(kScalingMaxFreqFileName)));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted scaling_max_freq file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedScalingMaxFreqFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)
          .Append(kScalingMaxFreqFileName),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle a missing scaling_cur_freq file.
TEST_F(CpuFetcherTest, MissingScalingCurFreqFile) {
  ASSERT_TRUE(
      brillo::DeleteFile(GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)
                             .Append(kScalingCurFreqFileName)));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted scaling_cur_freq file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedScalingCurFreqFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuFreqDirectoryPath(GetRootDir(), kFirstLogicalId)
          .Append(kScalingCurFreqFileName),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle a missing C-state name file.
TEST_F(CpuFetcherTest, MissingCStateNameFile) {
  ASSERT_TRUE(
      brillo::DeleteFile(GetCStateDirectoryPath(GetRootDir(), kFirstLogicalId)
                             .Append(kFirstCStateDir)
                             .Append(kCStateNameFileName)));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle a missing C-state time file.
TEST_F(CpuFetcherTest, MissingCStateTimeFile) {
  ASSERT_TRUE(
      brillo::DeleteFile(GetCStateDirectoryPath(GetRootDir(), kFirstLogicalId)
                             .Append(kFirstCStateDir)
                             .Append(kCStateTimeFileName)));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted C-state time file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedCStateTimeFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCStateDirectoryPath(GetRootDir(), kFirstLogicalId)
          .Append(kFirstCStateDir)
          .Append(kCStateTimeFileName),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle missing crypto file.
TEST_F(CpuFetcherTest, MissingCryptoFile) {
  ASSERT_TRUE(brillo::DeleteFile(GetProcCryptoPath(GetRootDir())));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle CPU temperatures without type.
TEST_F(CpuFetcherTest, CpuTemperatureWithoutType) {
  UnsetPath({kFirstFakeCpuTemperatureDir, kThermalAttributeType});
  MockUdevDeviceWithOneMissingType();
  // Use unknown architecture so that we will parse all thermal zones, including
  // the one without device type.
  fake_system_utils()->SetUnameResponse(0, "Unknown uname machine");
  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, kExpectedNumTotalThreads);
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kUnknown);
  VerifyPhysicalCpus(cpu_info->physical_cpus);

  const auto& cpu_temps = cpu_info->temperature_channels;
  ASSERT_EQ(cpu_temps.size(), 3);

  // Since fetching temperatures uses base::FileEnumerator, we're not
  // guaranteed the order of the three results.
  auto first_expected_temp =
      mojom::CpuTemperatureChannel::New(std::nullopt, kFirstFakeCpuTemperature);
  auto second_expected_temp = mojom::CpuTemperatureChannel::New(
      kSecondFakeCpuTemperatureLabel, kSecondFakeCpuTemperature);
  auto third_expected_temp = mojom::CpuTemperatureChannel::New(
      kThirdFakeCpuTemperatureLabel, kThirdFakeCpuTemperature);
  EXPECT_THAT(
      cpu_temps,
      UnorderedElementsAreArray(
          {MatchesCpuTemperatureChannelPtr(std::cref(first_expected_temp)),
           MatchesCpuTemperatureChannelPtr(std::cref(second_expected_temp)),
           MatchesCpuTemperatureChannelPtr(std::cref(third_expected_temp))}));
}

// Test that we handle incorrectly-formatted CPU temperature files.
TEST_F(CpuFetcherTest, IncorrectlyFormattedTemperature) {
  SetFile({kFirstFakeCpuTemperatureDir, kThermalAttributeTemperature},
          kNonIntegralFileContents);
  MockUdevDeviceWithOneIncorrectFormat();
  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, kExpectedNumTotalThreads);
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kX86_64);
  VerifyPhysicalCpus(cpu_info->physical_cpus);

  // We shouldn't have data corresponding to the first fake temperature values,
  // because it was formatted incorrectly.
  const auto& cpu_temps = cpu_info->temperature_channels;
  ASSERT_EQ(cpu_temps.size(), 1);
  const auto& second_temp = cpu_temps[0];
  ASSERT_FALSE(second_temp.is_null());
  ASSERT_TRUE(second_temp->label.has_value());
  EXPECT_EQ(second_temp->label.value(), kSecondFakeCpuTemperatureLabel);
  EXPECT_EQ(second_temp->temperature_celsius, kSecondFakeCpuTemperature);
}

// Test that we fall back to return all thermal zones data when there is
// no matching device type.
TEST_F(CpuFetcherTest, MissingCorrespondingThermalZone) {
  fake_system_utils()->SetUnameResponse(/*ret_code=*/0, kUnameMachineArmv7l);
  // Unset the thermal zone for Arm CPU.
  UnsetPath(kThirdFakeCpuTemperatureDir);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->architecture, mojom::CpuArchitectureEnum::kArmv7l);

  // We should have data of all existing thermal zones since there is no
  // thermal zone has the corresponding device type name for the CPU.
  const auto& cpu_temps = cpu_info->temperature_channels;
  ASSERT_EQ(cpu_temps.size(), 2);

  auto first_expected_temp = mojom::CpuTemperatureChannel::New(
      kFirstFakeCpuTemperatureLabel, kFirstFakeCpuTemperature);
  auto second_expected_temp = mojom::CpuTemperatureChannel::New(
      kSecondFakeCpuTemperatureLabel, kSecondFakeCpuTemperature);
  EXPECT_THAT(
      cpu_temps,
      UnorderedElementsAreArray(
          {MatchesCpuTemperatureChannelPtr(std::cref(first_expected_temp)),
           MatchesCpuTemperatureChannelPtr(std::cref(second_expected_temp))}));
}

// Test that we handle uname failing.
TEST_F(CpuFetcherTest, UnameFailure) {
  fake_system_utils()->SetUnameResponse(-1, std::nullopt);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  EXPECT_EQ(cpu_result->get_cpu_info()->architecture,
            mojom::CpuArchitectureEnum::kUnknown);
}

// Test that we handle normal vulnerability files.
TEST_F(CpuFetcherTest, NormalVulnerabilityFile) {
  VulnerabilityInfoMap expected;
  SetVulnerabiility("Vulnerability1", "Not affected");
  expected["Vulnerability1"] = mojom::VulnerabilityInfo::New(
      mojom::VulnerabilityInfo::Status::kNotAffected, "Not affected");
  SetVulnerabiility("Vulnerability2", "Vulnerable");
  expected["Vulnerability2"] = mojom::VulnerabilityInfo::New(
      mojom::VulnerabilityInfo::Status::kVulnerable, "Vulnerable");
  SetVulnerabiility("Vulnerability3", "Mitigation: Fake Mitigation Effect");
  expected["Vulnerability3"] = mojom::VulnerabilityInfo::New(
      mojom::VulnerabilityInfo::Status::kMitigation,
      "Mitigation: Fake Mitigation Effect");

  auto cpu_result = FetchCpuInfoSync();
  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  ASSERT_TRUE(cpu_info->vulnerabilities.has_value());
  EXPECT_EQ(cpu_info->vulnerabilities, expected);
}

// Test that we can parse status from vulnerability messages correctly.
TEST_F(CpuFetcherTest, ParseVulnerabilityMessageForStatus) {
  std::vector<std::pair<std::string, mojom::VulnerabilityInfo::Status>>
      message_to_expected_status = {
          {"Not affected", mojom::VulnerabilityInfo::Status::kNotAffected},
          {"Vulnerable", mojom::VulnerabilityInfo::Status::kVulnerable},
          {"Mitigation: Fake Mitigation Effect",
           mojom::VulnerabilityInfo::Status::kMitigation},
          {"Vulnerable: Vulnerable with message",
           mojom::VulnerabilityInfo::Status::kVulnerable},
          {"Unknown: Unknown status",
           mojom::VulnerabilityInfo::Status::kUnknown},
          {"KVM: Vulnerable: KVM vulnerability",
           mojom::VulnerabilityInfo::Status::kVulnerable},
          {"KVM: Mitigation: KVM mitigation",
           mojom::VulnerabilityInfo::Status::kMitigation},
          {"Processor vulnerable",
           mojom::VulnerabilityInfo::Status::kVulnerable},
          {"Random unrecognized message",
           mojom::VulnerabilityInfo::Status::kUnrecognized}};

  for (const auto& message_status : message_to_expected_status) {
    ASSERT_EQ(GetVulnerabilityStatusFromMessage(message_status.first),
              message_status.second);
  }
}

// Test that we handle missing kvm file.
TEST_F(CpuFetcherTest, MissingKvmFile) {
  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  ASSERT_EQ(cpu_info->virtualization->has_kvm_device, false);
}

// Test that we handle missing kvm file.
TEST_F(CpuFetcherTest, ExistingKvmFile) {
  SetFile(kRelativeKvmFilePath, "");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  ASSERT_EQ(cpu_info->virtualization->has_kvm_device, true);
}

// Test that we handle missing SMT Active file.
TEST_F(CpuFetcherTest, MissingSmtActiveFile) {
  UnsetPath({kRelativeCpuDir, kSmtDirName, kSmtActiveFileName});

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle Incorrectly Formatted SMT Active file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedSMTActiveFile) {
  SetFile({kRelativeCpuDir, kSmtDirName, kSmtActiveFileName}, "1000");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle Active SMT Active file.
TEST_F(CpuFetcherTest, ActiveSMTActiveFile) {
  SetFile({kRelativeCpuDir, kSmtDirName, kSmtActiveFileName}, "1");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  ASSERT_EQ(cpu_info->virtualization->is_smt_active, true);
}

// Test that we handle Inactive SMT Active file.
TEST_F(CpuFetcherTest, InactiveSMTActiveFile) {
  SetFile({kRelativeCpuDir, kSmtDirName, kSmtActiveFileName}, "0");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  ASSERT_EQ(cpu_info->virtualization->is_smt_active, false);
}

// Test that we handle missing SMT Control file.
TEST_F(CpuFetcherTest, MissingSmtControlFile) {
  UnsetPath({kRelativeCpuDir, kSmtDirName, kSmtControlFileName});

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that we handle Incorrectly Formatted SMT Control file.
TEST_F(CpuFetcherTest, IncorrectlyFormattedSMTControlFile) {
  SetFile({kRelativeCpuDir, kSmtDirName, kSmtControlFileName}, "WRONG");

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// POD struct for ParseSmtControlTest.
struct ParseSmtControlTestParams {
  std::string smt_control_content;
  mojom::VirtualizationInfo::SMTControl expected_mojo_enum;
};

// Tests that CpuFetcher can correctly parse each known SMT Control.
//
// This is a parameterized test with the following parameters (accessed
// through the ParseSmtControlTestParams POD struct):
// * |raw_state| - written to /proc/|kPid|/stat's process state field.
// * |expected_mojo_state| - expected value of the returned ProcessInfo's state
//                           field.
class ParseSmtControlTest
    : public CpuFetcherTest,
      public testing::WithParamInterface<ParseSmtControlTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  ParseSmtControlTestParams params() const { return GetParam(); }
};

// Test that we can parse the given uname response for CPU architecture.
TEST_P(ParseSmtControlTest, ParseSmtControl) {
  SetFile({kRelativeCpuDir, kSmtDirName, kSmtControlFileName},
          params().smt_control_content);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  EXPECT_EQ(cpu_result->get_cpu_info()->virtualization->smt_control,
            params().expected_mojo_enum);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ParseSmtControlTest,
    testing::Values(
        ParseSmtControlTestParams{"on",
                                  mojom::VirtualizationInfo::SMTControl::kOn},
        ParseSmtControlTestParams{"off",
                                  mojom::VirtualizationInfo::SMTControl::kOff},
        ParseSmtControlTestParams{
            "forceoff", mojom::VirtualizationInfo::SMTControl::kForceOff},
        ParseSmtControlTestParams{
            "notsupported",
            mojom::VirtualizationInfo::SMTControl::kNotSupported},
        ParseSmtControlTestParams{
            "notimplemented",
            mojom::VirtualizationInfo::SMTControl::kNotImplemented}));

// Tests that CpuFetcher can correctly parse each known architecture.
//
// This is a parameterized test with the following parameters (accessed
// through the ParseCpuArchitectureTestParams POD struct):
// * |raw_state| - written to /proc/|kPid|/stat's process state field.
// * |expected_mojo_state| - expected value of the returned ProcessInfo's state
//                           field.
class ParseCpuArchitectureTest
    : public CpuFetcherTest,
      public testing::WithParamInterface<ParseCpuArchitectureTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  ParseCpuArchitectureTestParams params() const { return GetParam(); }
};

// Test that we can parse the given uname response for CPU architecture.
TEST_P(ParseCpuArchitectureTest, ParseUnameResponse) {
  fake_system_utils()->SetUnameResponse(0, params().uname_machine);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  EXPECT_EQ(cpu_result->get_cpu_info()->architecture,
            params().expected_mojo_enum);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ParseCpuArchitectureTest,
    testing::Values(
        ParseCpuArchitectureTestParams{kUnameMachineX86_64,
                                       mojom::CpuArchitectureEnum::kX86_64},
        ParseCpuArchitectureTestParams{kUnameMachineAArch64,
                                       mojom::CpuArchitectureEnum::kAArch64},
        ParseCpuArchitectureTestParams{kUnameMachineArmv7l,
                                       mojom::CpuArchitectureEnum::kArmv7l},
        ParseCpuArchitectureTestParams{"Unknown uname machine",
                                       mojom::CpuArchitectureEnum::kUnknown}));

// Test that we handle cpu with no virtualization.
TEST_F(CpuFetcherTest, NoVirtualizationEnabled) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcCpuInfoPath(GetRootDir()),
      "processor\t: 0\nmodel name\t: model\nflags\t: \n\n"));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 1);
  ASSERT_TRUE(
      cpu_result->get_cpu_info()->physical_cpus[0]->virtualization.is_null());
}

// Test that we handle different flag values of vmx cpu virtualization.
TEST_F(CpuFetcherTest, TestVmxVirtualizationFlags) {
  // Add two CPUs, with the second CPU having a different physical ID compared
  // to logical ID.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcCpuInfoPath(GetRootDir()),
      "processor\t: 0\nmodel name\t: model\nphysical id\t: 0\nflags\t:\n\n"
      "processor\t: 12\nmodel name\t: model\nphysical id\t: 1\nflags\t: "
      "vmx\n\n"));

  std::vector<
      std::tuple</*val*/ uint64_t, /*is_locked*/ bool, /*is_enabled*/ bool>>
      vmx_msr_tests = {{0, false, false},
                       {kIA32FeatureLocked, true, false},
                       {kIA32FeatureEnableVmxInsideSmx, false, true},
                       {kIA32FeatureEnableVmxOutsideSmx, false, true}};

  for (const auto& msr_test : vmx_msr_tests) {
    // Set the mock executor response for ReadMsr calls. Make sure that the call
    // uses logical ID instead of physical ID.
    SetReadMsrResponse(cpu_msr::kIA32FeatureControl, 12, std::get<0>(msr_test));

    auto cpu_result = FetchCpuInfoSync();

    ASSERT_TRUE(cpu_result->is_cpu_info());
    ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 2);
    ASSERT_EQ(
        cpu_result->get_cpu_info()->physical_cpus[1]->virtualization->type,
        mojom::CpuVirtualizationInfo::Type::kVMX);
    ASSERT_EQ(
        cpu_result->get_cpu_info()->physical_cpus[1]->virtualization->is_locked,
        std::get<1>(msr_test));
    ASSERT_EQ(cpu_result->get_cpu_info()
                  ->physical_cpus[1]
                  ->virtualization->is_enabled,
              std::get<2>(msr_test));
  }
}

// Test that we handle different flag values of svm cpu virtualization.
TEST_F(CpuFetcherTest, TestSvmVirtualizationFlags) {
  // Add two CPUs, with the second CPU having a different physical ID compared
  // to logical ID.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcCpuInfoPath(GetRootDir()),
      "processor\t: 0\nmodel name\t: model\nphysical id\t: 0\nflags\t:\n\n"
      "processor\t: 12\nmodel name\t: model\nphysical id\t: 1\nflags\t: "
      "svm\n\n"));

  std::vector<
      std::tuple</*val*/ uint64_t, /*is_locked*/ bool, /*is_enabled*/ bool>>
      svm_msr_tests = {{0, false, true},
                       {kVmCrLockedBit, true, true},
                       {kVmCrSvmeDisabledBit, false, false}};

  for (const auto& msr_test : svm_msr_tests) {
    // Set the mock executor response for ReadMsr calls. Make sure that the call
    // uses logical ID instead of physical ID.
    SetReadMsrResponse(cpu_msr::kVmCr, 12, std::get<0>(msr_test));

    auto cpu_result = FetchCpuInfoSync();

    ASSERT_TRUE(cpu_result->is_cpu_info());
    ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 2);
    ASSERT_EQ(
        cpu_result->get_cpu_info()->physical_cpus[1]->virtualization->type,
        mojom::CpuVirtualizationInfo::Type::kSVM);
    ASSERT_EQ(
        cpu_result->get_cpu_info()->physical_cpus[1]->virtualization->is_locked,
        std::get<1>(msr_test));
    ASSERT_EQ(cpu_result->get_cpu_info()
                  ->physical_cpus[1]
                  ->virtualization->is_enabled,
              std::get<2>(msr_test));
  }
}

// Test that we handle different types of vmx cpu virtualization based on
// different physical CPU.
TEST_F(CpuFetcherTest, TestMultipleCpuVirtualization) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcCpuInfoPath(GetRootDir()),
      "processor\t: 0\nmodel name\t: model\nphysical id\t: 0\nflags\t: vmx\n\n"
      "processor\t: 12\nmodel name\t: model\nphysical id\t: 1\nflags\t: "
      "svm\n\n"));

  // Set the mock executor response for ReadMsr calls.
  SetReadMsrResponse(cpu_msr::kIA32FeatureControl, 0, 0);
  SetReadMsrResponse(cpu_msr::kVmCr, 12, 0);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus.size(), 2);
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus[0]->virtualization->type,
            mojom::CpuVirtualizationInfo::Type::kVMX);
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus[1]->virtualization->type,
            mojom::CpuVirtualizationInfo::Type::kSVM);
}

TEST_F(CpuFetcherTest, TestParseCpuFlags) {
  // Test that "vmx flags" won't be treated as "flags".
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcCpuInfoPath(GetRootDir()),
      "processor\t: 0\nmodel name\t: model\nphysical id\t: 0\n"
      "flags\t: cpu_flags\nvmx flags\t:vmx_flags\n\n"));

  // Set the mock executor response for ReadMsr calls.
  SetReadMsrResponse(cpu_msr::kIA32FeatureControl, 0, 0);

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  ASSERT_EQ(cpu_result->get_cpu_info()->physical_cpus[0]->flags,
            std::vector<std::string>{"cpu_flags"});
}

TEST_F(CpuFetcherTest, ValidCoreIdFile) {
  // Write core ID data for the first logical CPU.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCoreIdPath(GetRootDir(), kFirstLogicalId), "10"));
  // Write core ID data for the second logical CPU.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCoreIdPath(GetRootDir(), kSecondLogicalId), "11"));
  // Write core ID data for the third logical CPU.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCoreIdPath(GetRootDir(), kThirdLogicalId), "12"));

  auto cpu_result = FetchCpuInfoSync();

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();

  ASSERT_EQ(cpu_info->physical_cpus.size(), 2);
  ASSERT_EQ(cpu_info->physical_cpus[0]->logical_cpus.size(), 2);
  ASSERT_EQ(cpu_info->physical_cpus[1]->logical_cpus.size(), 1);
  EXPECT_EQ(cpu_info->physical_cpus[0]->logical_cpus[0]->core_id, 10);
  EXPECT_EQ(cpu_info->physical_cpus[0]->logical_cpus[1]->core_id, 11);
  EXPECT_EQ(cpu_info->physical_cpus[1]->logical_cpus[0]->core_id, 12);
}

TEST_F(CpuFetcherTest, InvalidCoreIdFile) {
  // Write core ID data for the first logical CPU.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCoreIdPath(GetRootDir(), kFirstLogicalId), "InvalidContent"));

  auto cpu_result = FetchCpuInfoSync();
  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that we handle a cpuinfo file for processors without core_id.
TEST_F(CpuFetcherTest, NoCoreIdFile) {
  ASSERT_TRUE(brillo::DeleteFile(GetCoreIdPath(GetRootDir(), 0)));

  auto cpu_result = FetchCpuInfoSync();
  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojom::ErrorType::kParseError);
}
}  // namespace
}  // namespace diagnostics
