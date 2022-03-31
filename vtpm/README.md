# Virtual TPM service

***vtpm*** is the system service that provides virtualized TPM interface. It
provides a D-Bus interface like a TPM daemon while the backend of the TPM
impleentation is virtualized, and can be backed by SW or the real TPM.
