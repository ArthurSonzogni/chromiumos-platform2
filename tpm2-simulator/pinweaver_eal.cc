/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/file_utils.h>
#include <fcntl.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <tpm2/BaseTypes.h>
#include <tpm2/Capabilities.h>
#include <tpm2/Implementation.h>
#include <tpm2/tpm_types.h>
#include <unistd.h>

#include <pinweaver/pinweaver_eal.h>

#define DEVICE_KEY_SIZE 32
#define PW_OBJ_CONST_SIZE 8
#define PW_NONCE_SIZE (128 / 8)
#define PINWEAVER_EAL_CONST 2
#define RESTART_TIMER_THRESHOLD 10

namespace {
constexpr char kLogPath[] = "log";
constexpr char kTreeDataPath[] = "tree_data";
}  // namespace

extern "C" {

int pinweaver_eal_sha256_init(pinweaver_eal_sha256_ctx_t* ctx) {
  int rv = SHA256_Init(ctx);
  if (rv != 1) {
    PINWEAVER_EAL_INFO("SHA256_Init failed: %d", rv);
  }
  return rv == 1 ? 0 : -1;
}

int pinweaver_eal_sha256_update(pinweaver_eal_sha256_ctx_t* ctx,
                                const void* data,
                                size_t size) {
  int rv = SHA256_Update(ctx, reinterpret_cast<const uint8_t*>(data), size);
  if (rv != 1) {
    PINWEAVER_EAL_INFO("SHA256_Update failed: %d", rv);
  }
  return rv == 1 ? 0 : -1;
}

int pinweaver_eal_sha256_final(pinweaver_eal_sha256_ctx_t* ctx, void* res) {
  int rv = SHA256_Final(reinterpret_cast<uint8_t*>(res), ctx);
  if (rv != 1) {
    PINWEAVER_EAL_INFO("SHA256_Final failed: %d", rv);
  }
  return rv == 1 ? 0 : -1;
}

int pinweaver_eal_hmac_sha256_init(pinweaver_eal_hmac_sha256_ctx_t* ctx,
                                   const void* key,
                                   size_t key_size /* in bytes */) {
  *ctx = HMAC_CTX_new();
  if (!*ctx) {
    PINWEAVER_EAL_INFO("HMAC_CTX_new failed");
    return -1;
  }
  int rv = HMAC_Init_ex(*ctx, reinterpret_cast<const uint8_t*>(key), key_size,
                        EVP_sha256(), NULL);
  if (rv != 1) {
    PINWEAVER_EAL_INFO("HMAC_Init_ex failed: %d", rv);
  }
  return rv == 1 ? 0 : -1;
}
int pinweaver_eal_hmac_sha256_update(pinweaver_eal_hmac_sha256_ctx_t* ctx,
                                     const void* data,
                                     size_t size) {
  int rv = HMAC_Update(*ctx, reinterpret_cast<const uint8_t*>(data), size);
  if (rv != 1) {
    PINWEAVER_EAL_INFO("HMAC_Update failed: %d", rv);
  }
  return rv == 1 ? 0 : -1;
}

int pinweaver_eal_hmac_sha256_final(pinweaver_eal_hmac_sha256_ctx_t* ctx,
                                    void* res) {
  unsigned int len;
  int rv = HMAC_Final(*ctx, reinterpret_cast<uint8_t*>(res), &len);
  HMAC_CTX_free(*ctx);
  *ctx = NULL;
  if (rv != 1) {
    PINWEAVER_EAL_INFO("HMAC_Final failed: %d", rv);
  }
  return rv == 1 ? 0 : -1;
}

int pinweaver_eal_aes256_ctr(const void* key,
                             size_t key_size, /* in bytes */
                             const void* iv,
                             const void* data,
                             size_t size,
                             void* res) {
  EVP_CIPHER_CTX* ctx;
  int rv;
  int len, len_final;

  if (key_size != 256 / 8)
    return -1;
  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;
  rv = EVP_EncryptInit(ctx, EVP_aes_256_ctr(),
                       reinterpret_cast<const uint8_t*>(key),
                       reinterpret_cast<const uint8_t*>(iv));
  if (rv != 1)
    goto out;
  rv = EVP_EncryptUpdate(ctx, reinterpret_cast<uint8_t*>(res), &len,
                         reinterpret_cast<const uint8_t*>(data), size);
  if (rv != 1)
    goto out;
  rv = EVP_EncryptFinal(ctx, reinterpret_cast<uint8_t*>(res) + len, &len_final);
out:
  EVP_CIPHER_CTX_free(ctx);
  return rv == 1 ? 0 : -1;
}

int pinweaver_eal_safe_memcmp(const void* s1, const void* s2, size_t len) {
  const uint8_t* us1 = reinterpret_cast<const uint8_t*>(s1);
  const uint8_t* us2 = reinterpret_cast<const uint8_t*>(s2);
  int result = 0;

  while (len--)
    result |= *us1++ ^ *us2++;

  return result != 0;
}

int pinweaver_eal_rand_bytes(void* buf, size_t size) {
  return RAND_bytes(reinterpret_cast<uint8_t*>(buf), size) == 1 ? 0 : -1;
}

uint64_t pinweaver_eal_seconds_since_boot() {
  struct sysinfo si;
  if (sysinfo(&si))
    return 0;

  return (uint64_t)si.uptime;
}

int pinweaver_eal_memcpy_s(void* dest,
                           size_t destsz,
                           const void* src,
                           size_t count) {
  if (count == 0)
    return 0;

  if (dest == NULL)
    return EINVAL;

  if (src == NULL) {
    memset(dest, 0, destsz);
    return EINVAL;
  }

  if (destsz < count) {
    memset(dest, 0, destsz);
    return ERANGE;
  }

  memcpy(dest, src, count);
  return 0;
}

static int g_device_key_fill[3] = {0x01, 0x00, 0xFF};

static int pinweaver_eal_get_device_key(int kind, void* key /* 256-bit */) {
  if (kind < 0 || kind >= 3)
    return -1;
  memset(key, g_device_key_fill[kind], 256 / 8);
  return 0;
}

static void* secure_memset(void* ptr, int value, size_t num) {
  volatile uint8_t* v_ptr = reinterpret_cast<uint8_t*>(ptr);
  while (num--)
    *(v_ptr++) = value;
  return ptr;
}

static int derive_pw_key(
    const uint8_t* device_key /* DEVICE_KEY_SIZE=256-bit */,
    const uint8_t* object_const /* PW_OBJ_CONST_SIZE */,
    const uint8_t* nonce /* PW_NONCE_SIZE */,
    uint8_t* result /* SHA256_DIGEST_SIZE */) {
  pinweaver_eal_hmac_sha256_ctx_t hash;
  if (pinweaver_eal_hmac_sha256_init(&hash, device_key, DEVICE_KEY_SIZE))
    return -1;
  if (pinweaver_eal_hmac_sha256_update(&hash, object_const,
                                       PW_OBJ_CONST_SIZE)) {
    pinweaver_eal_hmac_sha256_final(&hash, result);
    return -1;
  }
  if (pinweaver_eal_hmac_sha256_update(&hash, nonce, PW_NONCE_SIZE)) {
    pinweaver_eal_hmac_sha256_final(&hash, result);
    return -1;
  }
  return pinweaver_eal_hmac_sha256_final(&hash, result);
}

int pinweaver_eal_derive_keys(struct merkle_tree_t* merkle_tree) {
  const uint8_t kWrapKeyConst[PW_OBJ_CONST_SIZE] = {'W', 'R', 'A', 'P',
                                                    'W', 'R', 'A', 'P'};
  const uint8_t kHmacKeyConst[PW_OBJ_CONST_SIZE] = {'H', 'M', 'A', 'C',
                                                    'H', 'M', 'A', 'C'};
  uint8_t device_key[DEVICE_KEY_SIZE];
  if (pinweaver_eal_get_device_key(PINWEAVER_EAL_CONST, device_key))
    return -1;

  if (derive_pw_key(device_key, kWrapKeyConst,
                    merkle_tree->key_derivation_nonce, merkle_tree->wrap_key))
    return -1;

  if (derive_pw_key(device_key, kHmacKeyConst,
                    merkle_tree->key_derivation_nonce, merkle_tree->hmac_key))
    return -1;

  // Do not leave the content of the device key on the stack.
  secure_memset(device_key, 0, sizeof(device_key));

  return 0;
}

int pinweaver_eal_storage_init_state(uint8_t root_hash[PW_HASH_SIZE],
                                     uint32_t* restart_count) {
  struct pw_log_storage_t log;
  int ret = pinweaver_eal_storage_get_log(&log);
  if (ret != 0)
    return ret;

  memcpy(root_hash, log.entries[0].root, PW_HASH_SIZE);

  /* This forces an NVRAM write for hard reboots for which the
   * timer value gets reset. The TPM restart and reset counters
   * were not used because they do not track the state of the
   * counter.
   *
   * Pinweaver uses the restart_count to know when the time since
   * boot can be used as the elapsed time for the delay schedule,
   * versus when the elapsed time starts from a timestamp.
   */
  if (pinweaver_eal_seconds_since_boot() < RESTART_TIMER_THRESHOLD) {
    ++log.restart_count;
    int ret = pinweaver_eal_storage_set_log(&log);
    if (ret != 0)
      return ret;
  }
  *restart_count = log.restart_count;
  return 0;
}

int pinweaver_eal_storage_set_log(const struct pw_log_storage_t* log) {
  if (!brillo::WriteStringToFile(
          base::FilePath(kLogPath),
          std::string(reinterpret_cast<const char*>(log),
                      sizeof(struct pw_log_storage_t)))) {
    LOG(ERROR) << "Failed to write pinweaver log file.";
    return -1;
  }
  return 0;
}

int pinweaver_eal_storage_get_log(struct pw_log_storage_t* dest) {
  std::string contents;
  if (!base::ReadFileToString(base::FilePath(kLogPath), &contents)) {
    LOG(ERROR) << "Failed to read pinweaver log file.";
    return -1;
  }
  if (contents.size() != sizeof(struct pw_log_storage_t)) {
    LOG(ERROR) << "Mismatched pinweaver log file size.";
    return -1;
  }
  memcpy(dest, contents.data(), sizeof(struct pw_log_storage_t));
  return 0;
}

int pinweaver_eal_storage_set_tree_data(
    const struct pw_long_term_storage_t* data) {
  if (!brillo::WriteStringToFile(
          base::FilePath(kTreeDataPath),
          std::string(reinterpret_cast<const char*>(data),
                      sizeof(struct pw_long_term_storage_t)))) {
    LOG(ERROR) << "Failed to write pinweaver tree data file.";
    return -1;
  }
  return 0;
}

int pinweaver_eal_storage_get_tree_data(struct pw_long_term_storage_t* dest) {
  std::string contents;
  if (!base::ReadFileToString(base::FilePath(kTreeDataPath), &contents)) {
    LOG(ERROR) << "Failed to read pinweaver tree data file.";
    return -1;
  }
  if (contents.size() != sizeof(struct pw_long_term_storage_t)) {
    LOG(ERROR) << "Mismatched pinweaver tree data file size.";
    return -1;
  }
  memcpy(dest, contents.data(), sizeof(struct pw_long_term_storage_t));
  return 0;
}

// Defined in tpm2 library.
void PCRComputeCurrentDigest(TPMI_ALG_HASH, TPML_PCR_SELECTION*, TPM2B_DIGEST*);

uint8_t get_current_pcr_digest(const uint8_t bitmask[2],
                               uint8_t sha256_of_selected_pcr[32]) {
  TPM2B_DIGEST pcr_digest;
  TPML_PCR_SELECTION selection;

  selection.count = 1;
  selection.pcrSelections[0].hash = TPM_ALG_SHA256;
  selection.pcrSelections[0].sizeofSelect = PCR_SELECT_MIN;
  memset(&selection.pcrSelections[0].pcrSelect, 0, PCR_SELECT_MIN);
  memcpy(&selection.pcrSelections[0].pcrSelect, bitmask, 2);

  PCRComputeCurrentDigest(TPM_ALG_SHA256, &selection, &pcr_digest);
  if (memcmp(&selection.pcrSelections[0].pcrSelect, bitmask, 2) != 0)
    return 1;

  memcpy(sha256_of_selected_pcr, &pcr_digest.b.buffer, 32);
  return 0;
}

uint8_t pinweaver_eal_get_current_pcr_digest(
    const uint8_t bitmask[2], uint8_t sha256_of_selected_pcr[32]) {
  return get_current_pcr_digest(bitmask, sha256_of_selected_pcr);
}
}
