/*
*
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
* PDACPIPlatform Open Source Version of Apples AppleACPIPlatform
* Created by github.com/csekel (InSaneDarwin)
*
*/

#include <IOKit/IOLib.h>
#include "PDACPICPU.h"
#include "PDACPICPUInterruptController.h"
#include "PDACPIPlatformPrivate.h"

#define super IOCPUInterruptController

OSDefineMetaClassAndStructors(PDACPICPUInterruptController, IOCPUInterruptController);

IOReturn PDACPICPUInterruptController::initCPUInterruptController(int sources)
{
    /* pexpert lets IOKit handle the perfmon LAPIC vector so we'll handle it here. */
    m_perfmonVectors = (IOInterruptVector *)IOMalloc(sources * sizeof(IOInterruptVector));

    return super::initCPUInterruptController(sources);
}

void PDACPICPUInterruptController::setCPUInterruptProperties(IOService *service)
{
    OSArray *irqArray = OSArray::withCapacity(2);
    OSArray *irqConArray = OSArray::withCapacity(2);
    auto obj = service->getProvider()->getProperty("processor-number");

    OSNumber *cpu = OSDynamicCast(OSNumber, obj);

    if (cpu == nullptr) {
        panic("ACPI: i uh, what, huh, how- how is this possible.");
    }
    
    /*
     * XNU's PE + OSFMK is so weird it's not even funny.
     */
    
    uint32_t irq1 = cpu->unsigned32BitValue();
    uint32_t irq2 = irq1 | (1 << 8);
    
    OSData *d1 = OSData::withBytes(&irq1, 4);
    OSData *d2 = OSData::withBytes(&irq2, 4);
    
    irqArray->setObject(d1);
    irqArray->setObject(d2);
    
    irqConArray->setObject(gACPIPlatformInterruptControllerName);
    irqConArray->setObject(gACPIPlatformInterruptControllerName);
    
    service->setProperty(gIOInterruptSpecifiersKey, irqArray);
    service->setProperty(gIOInterruptControllersKey, irqConArray);
}

//
// NOTES:
//
// XNU encodes bit 8 and higher with the logical processor number.
//
// XNU has claimed IRQs D0 - DF for internal functions.
//
// I still don't know why AppleACPICPUInterruptController claims 64 IRQs though?
//
// Is that to cover the initial reserved range?
//
IOReturn PDACPICPUInterruptController::handleInterrupt(void *refCon, IOService *nub, int source)
{
    /* unserious kprintf = funny */
    kprintf("ACPI: SHOTS FIRED!!! IRQ 0x%08X", source);
    
    // now preferrably this should ask PE to nicely dispatch the irq

    return kIOReturnSuccess;
}

IOReturn PDACPICPUInterruptController::getInterruptType(IOService *nub, int source, int *type)
{
    if (type == nullptr) {
        return kIOReturnBadArgument;
    } else {
        //
        // LINT0 interrupts are always edge IRQs.
        //
        *type = kIOInterruptTypeEdge;
        return kIOReturnSuccess;
    }
}
