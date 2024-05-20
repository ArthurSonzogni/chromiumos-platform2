// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The kernel collector reports kernel panics or other kernel-level issues that
// caused machine reboot, like EFI crashes and BIOS crashes.
// The kernel collector runs on boot, via the crash-boot-collect service.

#ifndef CRASH_REPORTER_KERNEL_COLLECTOR_H_
#define CRASH_REPORTER_KERNEL_COLLECTOR_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/types/expected.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"
#include "crash-reporter/kernel_util.h"

enum class CrashCollectionStatus;

// These correspond to strings returned from kmsg_dump_reason_str() in the
// kernel.
enum class PstoreRecordType {
  kPanic,
  kOops,
  kEmergency,
  kShutdown,
  kUnknown,
  // Below this point are for errors while parsing the pstore record.
  kParseFailed,
};

std::ostream& operator<<(std::ostream& out, PstoreRecordType record_type);

// Kernel crash collector.
class KernelCollector : public CrashCollector {
 public:
  explicit KernelCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  KernelCollector(const KernelCollector&) = delete;
  KernelCollector& operator=(const KernelCollector&) = delete;

  ~KernelCollector() override;

  void OverrideEventLogPath(const base::FilePath& file_path);
  void OverrideBiosLogPath(const base::FilePath& file_path);
  void OverridePreservedDumpPath(const base::FilePath& file_path);
  void OverrideWatchdogSysPath(const base::FilePath& file_path);

  // Enable collection.
  bool Enable();

  // Returns true if the kernel collection currently enabled.
  bool is_enabled() const { return is_enabled_; }

  // Collects efi crashes. Returns 1 status per EFI crash found *or*
  // a vector containing the single element kNoCrashFound.
  std::vector<CrashCollectionStatus> CollectEfiCrashes(bool use_saved_lsb);

  // Collects ramoops crash.
  CrashCollectionStatus CollectRamoopsCrash(bool use_saved_lsb);

  // Given the returned results of CollectEfiCrash and CollectRamoopsCrash,
  // was there actually a kernel crash available to collect?
  static bool WasKernelCrash(
      const std::vector<CrashCollectionStatus>& efi_crash_statuses,
      CrashCollectionStatus ramoops_crash_status);

  // Set the architecture of the crash dumps we are looking at.
  void set_arch(kernel_util::ArchKind arch) { arch_ = arch; }
  kernel_util::ArchKind arch() const { return arch_; }

  // Returns the severity level and product group of the crash.
  CrashCollector::ComputedCrashSeverity ComputeSeverity(
      const std::string& exec_name) override;

  // Helper functions for parsing pstore record headers.
  static PstoreRecordType StringToPstoreRecordType(std::string_view record);
  static std::string PstoreRecordTypeToString(PstoreRecordType record_type);

  static constexpr char kDumpDriverEfiName[] = "efi";

 protected:
  // This class represents a single pstore crash record. Depending on the
  // backend used (EFI, ramoops, etc.) a pstore record may be split up across
  // many files, each of which has an ID as the last number (for example
  // dmesg-efi-150989600314002 has an id of 150989600314002). Regardless of the
  // backend used, the pstore filesystem exposes records in a common format
  // where each file has a header indicating the type of record (panic, oops,
  // etc.) and the part of the record the file corresponds to. For example, a
  // header "Panic#2 Part#4" indicates that the file is for the fourth part of
  // the second panic recorded in the backend. The remaining data in the file is
  // the record contents for that part.
  class PstoreCrash {
   public:
    explicit PstoreCrash(uint64_t id,
                         uint32_t max_part,
                         std::string_view backend,
                         const KernelCollector* collector)
        : id_(id),
          max_part_(max_part),
          backend_(std::string(backend)),
          collector_(collector) {}

    PstoreCrash(const PstoreCrash&) = default;
    PstoreCrash& operator=(const PstoreCrash&) = default;

    virtual ~PstoreCrash();

    // Load crash data into |contents|.
    // Returns true iff all parts of crashes are copied to |contents|.
    // In case of failure |contents| might be modified.
    bool Load(std::string& contents) const;
    // Get type of crash.
    PstoreRecordType GetType() const;
    // Remove pstore file(s) for this crash.
    void Remove() const;
    // Collect crash data into |crash| when crash type is a panic.
    CrashCollectionStatus CollectCrash(std::string& crash) const;

    // Returns crash id.
    uint64_t GetId() const { return id_; }

    // Returns id for |part|.
    virtual constexpr uint64_t GetIdForPart(uint32_t part) const { return id_; }

   protected:
    // Returns largest part for a record.
    uint32_t GetMaxPart() const { return max_part_; }
    void SetMaxPart(uint32_t max_part) { max_part_ = max_part; }
    const KernelCollector* GetCollector() const { return collector_; }

    // Returns the filepath in pstore for this record's |part|.
    base::FilePath GetFilePath(uint32_t part) const;

   private:
    uint64_t id_;
    uint32_t max_part_;
    std::string backend_;
    const KernelCollector* collector_;
  };

  // This class represents a single EFI crash record. An EFI crash record may be
  // split across multiple files, each for a different part of the record.
  class EfiCrash : public PstoreCrash {
   public:
    explicit EfiCrash(uint64_t id, const KernelCollector* collector)
        : PstoreCrash(id, GetPart(id), kDumpDriverEfiName, collector),
          timestamp_(GetTimestamp(id)),
          crash_count_(GetCrashCount(id)) {}

    EfiCrash(const EfiCrash&) = default;
    EfiCrash& operator=(const EfiCrash&) = default;

    ~EfiCrash() override;

    // Updates part from crash id iff it's greater.
    void UpdateMaxPart(uint64_t id) {
      uint32_t part = GetPart(id);
      if (part > GetMaxPart()) {
        SetMaxPart(part);
      }
    }

    constexpr uint64_t GetIdForPart(uint32_t part) const override {
      return GenerateId(timestamp_, part, crash_count_);
    }

    // Helper functions for parsing and generating efi crash id.

    // Get efi crash id for given part.
    static constexpr uint64_t GetIdForPart(uint64_t id, uint32_t part) {
      return GenerateId(GetTimestamp(id), part, GetCrashCount(id));
    }
    // Get crash count from efi crash id.
    static constexpr uint32_t GetCrashCount(uint64_t id) {
      return id % kMaxDumpRecord;
    }

    // Get part number from efi crash id.
    static constexpr uint32_t GetPart(uint64_t id) {
      return (id / kMaxDumpRecord) % kMaxPart;
    }

    // Get timestamp from efi crash id.
    static constexpr uint64_t GetTimestamp(uint64_t id) {
      return (id / (kMaxDumpRecord * kMaxPart));
    }

    // Generates efi crash id from timestamp, part, crash count.
    // EFI File name is of format dmesg-efi-<crash_id>. Since one kernel crash
    // is split into multiple parts, <crash_id> is derived by
    // crash_id = (timestamp * 100 + part) * 1000 + crash_count.
    // See efi-pstore driver (https://goo.gl/1YBeCD) for more information.
    // e.g. File "dmesg-efi-150989600314002" represents part 14 of crash 2.
    static constexpr uint64_t GenerateId(uint64_t timestamp,
                                         uint32_t part,
                                         uint32_t crash_count) {
      return (timestamp * kMaxPart + part) * kMaxDumpRecord + crash_count;
    }

    static constexpr size_t kMaxDumpRecord = 1000;
    static constexpr size_t kMaxPart = 100;

   private:
    uint64_t timestamp_;
    uint32_t crash_count_;
  };

 private:
  friend class KernelCollectorTest;
  FRIEND_TEST(KernelCollectorTest, LoadCorruptDump);
  FRIEND_TEST(KernelCollectorTest, LoadBiosLog);
  FRIEND_TEST(KernelCollectorTest, CollectOK);
  FRIEND_TEST(KernelCollectorTest, ParseEfiCrashId);
  FRIEND_TEST(KernelCollectorTest, GetEfiCrashType);
  FRIEND_TEST(KernelCollectorTest, LoadEfiCrash);
  FRIEND_TEST(KernelCollectorTest, RemoveEfiCrash);
  FRIEND_TEST(KernelCollectorTest, LastRebootWasNoCError);

  virtual bool DumpDirMounted();

  bool LoadPreservedDump(std::string* contents);
  bool LoadLastBootBiosLog(std::string* contents);

  bool LastRebootWasBiosCrash(const std::string& dump);
  bool LastRebootWasNoCError(const std::string& dump);
  base::expected<bool, CrashCollectionStatus> LastRebootWasWatchdog(
      std::string& signature);
  bool LoadConsoleRamoops(std::string* contents);

  base::FilePath GetDumpRecordPath(const char* type,
                                   const char* driver,
                                   size_t record);

  bool LoadParameters();

  // Read a record to string, modified from file_utils since that didn't
  // provide a way to restrict the read length.
  // Return value indicates (only) error state:
  //  * false when we get an error (can't read from dump location).
  //  * true if no error occured.
  // Not finding a valid record is not an error state and is signaled by the
  // record_found output parameter.
  bool ReadRecordToString(std::string* contents,
                          size_t current_record,
                          bool* record_found);

  void AddLogFile(const char* log_name,
                  const std::string& log_data,
                  const base::FilePath& log_path);

  CrashCollectionStatus HandleCrash(const std::string& kernel_dump,
                                    const std::string& bios_dump,
                                    const std::string& hypervisor_dump,
                                    const std::string& signature);

  std::vector<EfiCrash> FindEfiCrashes() const;

  bool is_enabled_;
  base::FilePath eventlog_path_;
  base::FilePath dump_path_;
  base::FilePath bios_log_path_;
  base::FilePath watchdogsys_path_;
  size_t records_;

  // The architecture of kernel dump strings we are working with.
  kernel_util::ArchKind arch_;
};

#endif  // CRASH_REPORTER_KERNEL_COLLECTOR_H_
