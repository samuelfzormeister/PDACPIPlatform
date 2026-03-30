#!/bin/bash -e -x

mkdir -m 0755 -p ${DSTROOT}/usr/local/OpenSourceLicenses ${DSTROOT}/usr/local/OpenSourceVersions
install -m 0444 ${SRCROOT}/uacpi.plist ${DSTROOT}/usr/local/OpenSourceVersions/uacpi.plist
install -m 0444 ${SRCROOT}/uacpi/LICENSE ${DSTROOT}/usr/local/OpenSourceLicenses/uacpi.txt

