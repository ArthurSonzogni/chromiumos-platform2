# Setup GPG Encryption

The keys generated here are strictly for transmission and retrieval of data for
a single Fingerprint Study run.

*See [Typography conventions] to understand what `(outside)`, `(inside)`,
`(in/out)`, and `(device)` mean.*

[Typography conventions]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#typography-conventions

## Generating Keys

Setting `GNUPGHOME` will force gpg to use a completely different keyring/config.
In this case, we set it to an empty directory `/tmp/fpstudygpg`, where we will
build a new keyring with only one key pair.

```bash
# Setup a new empty GNUPG directory.
(in/out) $ export GNUPGHOME=/tmp/fpstudygpg
(in/out) $ gio trash -f "${GNUPGHOME}" \
           && mkdir -p "${GNUPGHOME}/private-keys-v1.d" \
           && chmod -R 700 "${GNUPGHOME}"
# Setup key generation parameters.
# https://www.gnupg.org/documentation/manuals/gnupg/Unattended-GPG-key-generation.html
(in/out) $ cat >keyparams <<EOF
    %echo Generating key.
    Key-Type: RSA
    Key-Length: 4096
    # Disable subkey generation, since this is a one time use key pair anyways.
    # Subkey-Type: RSA
    # Subkey-Length: 4096
    Name-Real: ChromeOSFPStudy
    Name-Comment: Chrome OS Fingerprint Study Key
    Name-Email: <FILL_IN_RECIPIENT_EMAIL>
    Expire-Date: 0
    # Passphrase: <IF_UNCOMMENTED_THIS_IS_THE_PASSWORD>
    %ask-passphrase
    # %no-ask-passphrase
    # %no-protection
    %commit
    %echo Done.
EOF
# Generate a new key pair. Make note of the password used. This password is used
# to protect the private key and will be required when decrypting the captures.
(in/out) $ gpg --verbose --batch --gen-key ./keyparams
# Record the fingerprint/keyid from by the following command.
# The fingerprint is the 40 hex character string grouped into 10 groups of
# 4 characters. Remove the spaces from this fingerprint to form the keyid.
(in/out) $ gpg --fingerprint ChromeOSFPStudy
# Export only the public key for the test device. This key must be copied to the
# test device and will be used as the keyring.
(in/out) $ gpg --verbose --export ChromeOSFPStudy > "${GNUPGHOME}/chromeos-fpstudy-public-device.gpg"
# Export the private key for backup. This key is for the recipient to be able
# to decrypt the fingerprint capture.
# This key must NOT be copied to the test device.
(in/out) $ gpg --verbose --export-secret-keys ChromeOSFPStudy > "${GNUPGHOME}/chromeos-fpstudy-private.gpg"
```

## Install Keys on Device

*   Copy the `chromeos-fpstudy-public-device.gpg` file to the test device.

    ```bash
    scp "${GNUPGHOME}/chromeos-fpstudy-public-device.gpg" \
      dut1:/var/lib/fpstudygnupg
    ssh dut1 chmod u=r,g=,o= \
      /var/lib/fpstudygnupg/chromeos-fpstudy-public-device.gpg
    ```

*   Edit the `/etc/init/fingerprint_study.conf` file to have the following
    additional arguments to `exec study_serve`.

    -   `--gpg-keyring /var/lib/fpstudygnupg/chromeos-fpstudy-public-device.gpg`
    -   `--gpg-recipients KEYID` where KEYID is the keyid recorded in the
        `Generating Keys` section.

## Test Encryption Manually

Follow the `Generating Keys` section and then run the following commands:

```bash
(in/out) $ GNUPGHOME_KEYGEN=/tmp/fpstudygpg
# Unfortunately, you still need a proper homedir for gpg to work.
(in/out) $ export GNUPGHOME=/tmp/fpstudygpg-host
(in/out) $ gio trash -f "${GNUPGHOME}" \
             && mkdir -p "${GNUPGHOME}/private-keys-v1.d" \
             && chmod -R 700 "${GNUPGHOME}"

# Test encrypting a sequence of numbers using only the public key.
(in/out) $ gpg --verbose --no-default-keyring \
             --keyring \
             "${GNUPGHOME_KEYGEN}/chromeos-fpstudy-public-device.gpg" \
             --trust-model always \
             -ear ChromeOSFPStudy > test-output.gpg < <(seq 10)
(in/out) $ file test-output.gpg
(in/out) $ gpg --list-packets test-output.gpg

# We will now import the private key to our clean GNUPHHOME.
# In order to test the above encryption step again, you would need to
# clear the GNUPGHOME directory (run these test instructions from the top).
(in/out) $ gpg --import "${GNUPGHOME_KEYGEN}/chromeos-fpstudy-private.gpg"
# The following should yield a sequence of number from 1 to 10.
(in/out) $ gpg -d test-output.gpg
```

## Test Encryption Using Host

Follow the `Generating Keys` section and then run the following commands:

```bash
(in/out) $ ./host-run.sh \
             --gpg-keyring "${GNUPGHOME}/chromeos-fpstudy-public-device.gpg" \
             --gpg-recipients ChromeOSFPStudy
```

## Decrypting Fingerprint Captures

To decrypt the fingerprint captures on the receiving/host side, you must import
the private key `chromeos-fpstudy-private.gpg` generated above in the
`Generating Keys` section.

<!-- mdformat off(b/139308852) -->
*** note
If you do not want to import the private key into your normal gpg homedir, you
can run the following to create a temporary gpg homedir:

```bash
(in/out) $ export GNUPGHOME=/tmp/fpstudygpg-host
(in/out) $ gio trash -f "${GNUPGHOME}" \
           && mkdir -m 700 -p "${GNUPGHOME}/private-keys-v1.d"
```
***
<!-- mdformat on -->

```bash
# Import the private key into the current gpg homedir.
(in/out) $ gpg --import chromeos-fpstudy-private.gpg
# Decrypt all fingerprint captures, while place the decrypted file version
# alongside the encrypted version.
(in/out) $ find ./fpstudy-fingers -type f -name '*.gpg' | \
             xargs -P $(nproc) gpg --decrypt-files
```
