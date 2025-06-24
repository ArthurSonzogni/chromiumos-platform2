# ANX7625 controller Trusted Application for OPTEE on ARM

This package will produce an OP-TEE trusted application.

This is used for interacting with the ANX7625 device while it is
controlled by OPTEE. Register reading and writing depends on the
Mediatek I2C TA. Writing to a registers is limited in order to
ensure HDCP status is read-only from user-space.
