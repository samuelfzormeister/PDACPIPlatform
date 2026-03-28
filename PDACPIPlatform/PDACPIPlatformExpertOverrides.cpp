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

extern "C" {
#include "accommon.h"
};

#define super IOACPIPlatformExpert

/* External declarations */
extern IOReturn AcpiStatus2IOReturn(ACPI_STATUS stat);

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::start
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::start(IOService *provider)
{
    if (!super::start(provider)) {
        IOLog("PDACPIPlatformExpert::start - super::start failed\n");
        return false;
    }
    
    m_provider = OSDynamicCast(IOPlatformExpertDevice, provider);
    
    //
    // initialize this here for later
    //
    m_lastIOAPICMax = 32;
    m_ioApicCount = 0;
    
    /* Respond to certain boot arguemnts */
    PE_parse_boot_argn("acpi_layer", &AcpiDbgLayer, 4);
    PE_parse_boot_argn("acpi_level", &AcpiDbgLevel, 4);

    if (!initializeACPICA()) {
        //
        // Realistically, if we returned false IOKit would panic anyways.
        //
        // It's better for debugging if we panic here though.
        //
        panic("ACPI: ACPI CA layer failed to initialize.\n");
    }

    kprintf("PDACPIPlatformExpert::start - [SUCCESS] ACPICA Initialized successfully.\n");
    
    /* Document all of our available tables into an OSDictionary, this will be used by IOPCIFamily (and potentially future clients?) */
    kprintf("PDACPIPlatformExpert::start - Taking the time now to process all available tables!\n");

    catalogACPITables();
    
    /* Initialize IOPCIFamily and the IOMMU mapper */
    if (!initPCI()) {
        //
        // Realistically, if we returned false IOKit would panic anyways.
        //
        // It's better for debugging if we panic here though.
        //
        panic("ACPI: Failed to initialize PCI.");
    }
    
    /* By this point, we should have all CPUs defined in both IODeviceTree:/cpus and the IOACPIPlane, and the IOService plane of course. */
    

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

    /* Terminate the ACPICA layer. */
    AcpiTerminate();

    super::stop(provider);
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::copyProperty
//---------------------------------------------------------------------------
OSObject *PDACPIPlatformExpert::copyProperty(const char *property) const
{
    if (strncmp(property, "ACPI Tables", strlen(property)) == 0) {
        return m_tableDict->copyCollection();
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
    UInt64 time = 0;
    
    /* Use milliseconds over any other unit, nanoseconds and microseconds are too precise and seconds are too long for a lock. */
    time += timeout->tv_nsec / NSEC_PER_MSEC;
    time += timeout->tv_sec * ACPI_MSEC_PER_SEC;
    
    return AcpiStatus2IOReturn(AcpiAcquireGlobalLock(time, lockToken));
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::releaseGlobalLock
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::releaseGlobalLock(IOService *client,
                                             UInt32 lockToken)
{
    AcpiReleaseGlobalLock(lockToken);
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::validateObject
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::validateObject(IOACPIPlatformDevice *nub,
                                              const OSSymbol *name)
{
    PDACPIHandle *handle = (PDACPIHandle *)nub->getDeviceHandle();
    ACPI_HANDLE caHandle;
    
    ACPI_STATUS status = AcpiGetHandle(handle->fACPICAHandle, name->getCStringNoCopy(), &caHandle);
    
    if (AcpiStatus2IOReturn(status) != kIOReturnSuccess || caHandle == NULL) {
        return kIOReturnNotFound;
    }
    
    return kIOReturnSuccess;
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
    ACPI_STATUS status = AcpiInstallAddressSpaceHandler(device->getDeviceHandle(), spaceID, (ACPI_ADR_SPACE_HANDLER)Handler, NULL, context);

    if (spaceID == kIOACPIAddressSpaceIDEmbeddedController) {
        this->m_ecSpaceHandler = Handler;
        this->m_ecSpaceContext = context;
    } else if (spaceID == kIOACPIAddressSpaceIDSMBus) {
        this->m_smbusSpaceHandler = Handler;
        this->m_smbusSpaceContext = context;
    }

    return AcpiStatus2IOReturn(status);
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
    ACPI_STATUS status = AcpiRemoveAddressSpaceHandler(device->getDeviceHandle(), spaceID, (ACPI_ADR_SPACE_HANDLER)handler);

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
        case kIOACPIAddressSpaceIDEmbeddedController:
            if (m_ecSpaceHandler && m_ecSpaceContext) {
                return m_ecSpaceHandler(kIOACPIAddressSpaceOpRead, address, value, bitWidth, bitOffset, m_ecSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        case kIOACPIAddressSpaceIDSMBus:
            if (this->m_smbusSpaceHandler && m_smbusSpaceContext) {
                return this->m_smbusSpaceHandler(kIOACPIAddressSpaceOpRead, address, value, bitWidth, bitOffset, m_smbusSpaceContext);
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
        case kIOACPIAddressSpaceIDEmbeddedController:
            if (m_ecSpaceHandler && m_ecSpaceContext) {
                return m_ecSpaceHandler(kIOACPIAddressSpaceOpWrite, address, &value, bitWidth, bitOffset, m_ecSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        case kIOACPIAddressSpaceIDSMBus:
            if (m_smbusSpaceHandler && m_smbusSpaceContext) {
                return m_smbusSpaceHandler(kIOACPIAddressSpaceOpWrite, address, &value, bitWidth, bitOffset, m_smbusSpaceContext);
            } else {
                return kIOReturnNotReady;
            }
        default:
            break;
    }

    return kIOReturnInvalid;
}
