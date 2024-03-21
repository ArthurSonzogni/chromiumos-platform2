# Attestation

Attestationd is a ChromeOS daemon responsible for attestation of identity and
device state by interacting with HSM (Hardware Security Module), verified boot,
PCA (Privacy Certification Authority) and VA (Verified Access).

## Device state

ChromeOS firmware, coreboot, at booting updates the current boot mode (Normal/
Dev/...) and the HWID (Hardware ID) in HSM, and ensures they could not be
changed afterwards.

When using TPM (Trusted Platform Module), this is implemented by extending PCR
(Platform Configuration Register): new PCR = Digest(old PCR || data to extend).
Once the PCR is extended, it's infeasible to be extended to another valid
value.

In this way, we can ensure the integrity of the current boot mode and HWID of
the device.

## Identity and enrollment

In OOBE, Attestationd prepares several things for enrollment. By using HSM,
Attestationd fetches the Endorsement Certificate and creates AIK (Attestation
Identity Key) and quotes (signs) the PCRs by AIK.

When enrolling, the followings are sent to PCA in general:
-   AIK public key: representing the identity of the device which is created
    and protected by HSM and using to verify the Quotes
-   Quoted data: PCR of the boot mode and HWID of the device.
-   Quotes: Signature of quoted data signed by AIK.
-   Endorsement Certificate signed by PCA: to ensure the device is a valid
    Chromebook. This is encrypted by the RSA public key of PCA.

PCA verifies the above and then gives the AIK certification back to
attestationd. Thus, the enrollment is completed.
