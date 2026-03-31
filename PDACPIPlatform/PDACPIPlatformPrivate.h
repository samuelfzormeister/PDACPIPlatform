/*
 * Copyright (c) 2007-Present The PureDarwin Project.
 * All rights reserved.
 *
 * @PUREDARWIN_LICENSE_HEADER_START@
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @PUREDARWIN_LICENSE_HEADER_END@
 *
 * PDACPIPlatform Open Source Version of Apple's AppleACPIPlatform
 * Created by github.com/csekel (InSaneDarwin)
 */

#ifndef _PDACPIPLATFORM_PRIVATE_H
#define _PDACPIPLATFORM_PRIVATE_H

#include <IOKit/IOLib.h>

#if __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
};
#endif

#if KERNEL && PDACPI_BUILDING_PLATFORM

#include <libkern/c++/OSSymbol.h>
#include <uacpi/acpi.h>
#include <uacpi/uacpi.h>

extern const OSSymbol *gACPIPlatformAPICDestinationIDKey;
extern const OSSymbol *gACPIPlatformAPICPhysicalAddressKey;
extern const OSSymbol *gACPIPlatformAPICBaseVectorNumberKey;
extern const OSSymbol *gACPIPlatformAPICIDKey;
extern const OSSymbol *gACPIPlatformAPICHandleSleepWakeFunction;
extern const OSSymbol *gACPIPlatformAPICSetVectorPhysicalDestination;
extern const OSSymbol *gACPIPlatformInterruptSpecifiersKey;
extern const OSSymbol *gACPIPlatformInterruptControllerName;

extern class PDACPIPlatformExpert *gACPIPlatformExpert;

extern IOReturn uAcpiStatus2IOKit(uacpi_status status);

/* This is so we can store extra data in the future */
#define PDACPI_HANDLE_SIG 'pdah'

struct PDACPIHandle {
    UInt32 sig;
    
    /* Add any additional data as needed, like resources, etc. */
    uacpi_handle layerHandle;
};

#else

#endif

#endif /* _PDACPIPLATFORM_PRIVATE_H */
