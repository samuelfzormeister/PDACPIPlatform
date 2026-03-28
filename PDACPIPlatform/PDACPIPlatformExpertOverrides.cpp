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

//---------------------------------------------------------------------------
// This file is for virtual overrides in PDACPIPlatformExpert
//---------------------------------------------------------------------------

#include "PDACPIPlatformPrivate.h"
#include "PDACPIPlatformExpert.h"

#define super IOACPIPlatformExpert

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::start
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::start(IOService *provider)
{
    if (!super::start(provider)) {
        IOLog("PDACPIPlatformExpert::start - super::start failed\n");
        return false;
    }
    
    this->m_provider = OSDynamicCast(IOPlatformExpertDevice, provider);

    if (!this->initializeACPICA()) {
        panic("ACPI: ACPI CA layer failed to initialize.\n");
    }

    IOLog("PDACPIPlatformExpert::start - [SUCCESS] ACPICA Initialized successfully.\n");

    // The service should be registered after successful initialization.
    registerService();
    IOLog("PDACPIPlatformExpert::start - Service registered.\n");

    return true;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::stop
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::stop(IOService *provider)
{
    IOLog("PDACPIPlatformExpert::stop\n");

    super::stop(provider);
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::copyProperty
//---------------------------------------------------------------------------
OSObject *PDACPIPlatformExpert::copyProperty(const char *property) const
{
    if (strncmp(property, "ACPI Tables", strlen(property)) == 0) {
        return this->m_tableDict->copyCollection();
    }
    
    return super::copyProperty(property);
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::getACPITableData
//---------------------------------------------------------------------------
const OSData *PDACPIPlatformExpert::getACPITableData(const char *name, 
                                                     UInt32 TableIndex)
{
    char tbl[32];

    if (TableIndex > 0) {
        snprintf(tbl, 32, "%4.4s-%u", name, TableIndex);
    } else {
        snprintf(tbl, 32, "%4.4s", name);
    }

    OSObject *obj = this->m_tableDict->getObject(name);
    if (obj) {
        return OSDynamicCast(OSData, obj);
    }
    
    return nullptr;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::acquireGlobalLock
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::acquireGlobalLock(IOService *client, 
                                                 UInt32 *lockToken,
                                                 const mach_timespec_t *timeout)
{
    return kIOReturnUnsupported;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::releaseGlobalLock
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::releaseGlobalLock(IOService *client,
                                             UInt32 lockToken)
{

}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::validateObject
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::validateObject(IOACPIPlatformDevice *nub,
                                              const OSSymbol *name)
{
    PDACPIHandle *handle = (PDACPIHandle *)nub->getDeviceHandle();
    
    return kIOReturnUnsupported;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::registerAddressSpaceHandler
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::registerAddressSpaceHandler(
                                                IOACPIPlatformDevice *device,
                                                IOACPIAddressSpaceID spaceID,
                                                IOACPIAddressSpaceHandler Handler,
                                                void *context,
                                                IOOptionBits options)
{
    if (spaceID == kIOACPIAddressSpaceIDEmbeddedController) {
        this->m_ecSpaceHandler = Handler;
        this->m_ecSpaceContext = context;
    } else if (spaceID == kIOACPIAddressSpaceIDSMBus) {
        this->m_smbusSpaceHandler = Handler;
        this->m_smbusSpaceContext = context;
    }

    return kIOReturnSuccess;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::unregisterAddressSpaceHandler
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::unregisterAddressSpaceHandler(
                                            IOACPIPlatformDevice *device,
                                            IOACPIAddressSpaceID spaceID,
                                            IOACPIAddressSpaceHandler handler,
                                            IOOptionBits)
{
    if (spaceID == kIOACPIAddressSpaceIDEmbeddedController) {
        this->m_ecSpaceHandler = nullptr;
        this->m_ecSpaceContext = nullptr;
    } else if (spaceID == kIOACPIAddressSpaceIDSMBus) {
        this->m_smbusSpaceHandler = nullptr;
        this->m_smbusSpaceContext = nullptr;
    }
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::readAddressSpace
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::readAddressSpace(UInt64 *value,
                                                IOACPIAddressSpaceID spaceID,
                                                IOACPIAddress address,
                                                UInt32 bitWidth,
                                                UInt32 bitOffset,
                                                IOOptionBits options)
{
    switch (spaceID) {
            /*
        case kIOACPIAddressSpaceIDSystemMemory: {
            return AcpiStatus2IOReturn(AcpiOsReadMemory(address.addr64, value, bitWidth));
        }
        case kIOACPIAddressSpaceIDSystemIO: {
            return AcpiStatus2IOReturn(AcpiOsReadPort((ACPI_IO_ADDRESS)address.addr64, (UInt32 *)value, bitWidth));
        }
        case kIOACPIAddressSpaceIDPCIConfiguration: {
            ACPI_PCI_ID pci;
            
            pci.Segment = address.pci.segment;
            pci.Bus = address.pci.bus;
            pci.Device = address.pci.device;
            pci.Function = address.pci.function;
            
            return AcpiStatus2IOReturn(AcpiOsReadPciConfiguration(&pci, address.pci.offset, value, bitWidth));
        }
             */
        case kIOACPIAddressSpaceIDEmbeddedController:
            if (this->m_ecSpaceHandler && this->m_ecSpaceContext) {
                return this->m_ecSpaceHandler(kIOACPIAddressSpaceOpRead, address, value, bitWidth, bitOffset, this->m_ecSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        case kIOACPIAddressSpaceIDSMBus:
            if (this->m_smbusSpaceHandler && this->m_smbusSpaceContext) {
                return this->m_smbusSpaceHandler(kIOACPIAddressSpaceOpRead, address, value, bitWidth, bitOffset, this->m_smbusSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        default:
            break;
    }

    return kIOReturnInvalid;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::writeAddressSpace
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::writeAddressSpace(UInt64 value,
                                                 IOACPIAddressSpaceID spaceID,
                                                 IOACPIAddress address,
                                                 UInt32 bitWidth,
                                                 UInt32 bitOffset,
                                                 IOOptionBits options)
{
    switch (spaceID) {
            /*
        case kIOACPIAddressSpaceIDSystemMemory: {
            return AcpiStatus2IOReturn(AcpiOsWriteMemory(address.addr64, value, bitWidth));
        }
        case kIOACPIAddressSpaceIDSystemIO: {
            return AcpiStatus2IOReturn(AcpiOsWritePort((ACPI_IO_ADDRESS)address.addr64, (UInt32)value, bitWidth));
        }
        case kIOACPIAddressSpaceIDPCIConfiguration: {
            ACPI_PCI_ID pci;
            
            pci.Segment = address.pci.segment;
            pci.Bus = address.pci.bus;
            pci.Device = address.pci.device;
            pci.Function = address.pci.function;
            
            return AcpiStatus2IOReturn(AcpiOsWritePciConfiguration(&pci, address.pci.offset, value, bitWidth));
        }
             */
        case kIOACPIAddressSpaceIDEmbeddedController:
            if (this->m_ecSpaceHandler && this->m_ecSpaceContext) {
                return this->m_ecSpaceHandler(kIOACPIAddressSpaceOpWrite, address, &value, bitWidth, bitOffset, this->m_ecSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        case kIOACPIAddressSpaceIDSMBus:
            if (this->m_smbusSpaceHandler && this->m_smbusSpaceContext) {
                return this->m_smbusSpaceHandler(kIOACPIAddressSpaceOpWrite, address, &value, bitWidth, bitOffset, this->m_smbusSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        default:
            break;
    }

    return kIOReturnInvalid;
}
