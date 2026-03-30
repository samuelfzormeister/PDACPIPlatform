/*
 * Copyright (c) 2026-Present The PureDarwin Project.
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
 */

#include "PDACPIPlatformPrivate.h"

IOReturn uAcpiStatus2IOKit(uacpi_status status) {
    switch (status) {
        case UACPI_STATUS_OK:
            return kIOReturnSuccess;
    
        case UACPI_STATUS_MAPPING_FAILED:
            // --- close enough. --- //
            return kIOReturnCannotWire;
        case UACPI_STATUS_OUT_OF_MEMORY:
            return kIOReturnNoMemory;

        case UACPI_STATUS_BAD_CHECKSUM:
            return kIOReturnError;
            
        case UACPI_STATUS_INVALID_SIGNATURE:
        case UACPI_STATUS_INVALID_TABLE_LENGTH:
        case UACPI_STATUS_INVALID_ARGUMENT:
            return kIOReturnBadArgument;
            
        case UACPI_STATUS_NOT_FOUND:
            return kIOReturnNotFound;
            
        case UACPI_STATUS_UNIMPLEMENTED:
        case UACPI_STATUS_COMPILED_OUT:
            return kIOReturnUnsupported;
            
        case UACPI_STATUS_NAMESPACE_NODE_DANGLING:
        case UACPI_STATUS_NO_HANDLER:
        case UACPI_STATUS_NO_RESOURCE_END_TAG:
        case UACPI_STATUS_TYPE_MISMATCH:
        case UACPI_STATUS_INIT_LEVEL_MISMATCH:
        case UACPI_STATUS_INTERNAL_ERROR:
            return kIOReturnInternalError;
            
        case UACPI_STATUS_HARDWARE_TIMEOUT:
        case UACPI_STATUS_TIMEOUT:
            return kIOReturnTimeout;
            
        case UACPI_STATUS_DENIED:
            return kIOReturnNotPermitted;
        
        default:
            kprintf("ACPI: unhandled uACPI status 0x%x", status);
            return kIOReturnInvalid;
    }
}
