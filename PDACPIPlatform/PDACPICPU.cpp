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

#include "PDACPICPU.h"
#include <IOKit/IOLib.h>
#include <i386/machine_routines.h>
#include "PDACPICPUInterruptController.h"

#ifndef SDK_IS_PRIVATE

/* ISTG. */

extern kern_return_t
ml_processor_register(
    cpu_id_t        cpu_id,
    uint32_t        lapic_id,
    processor_t     *processor_out,
    boolean_t       boot_cpu,
    boolean_t       start);
#endif

extern PDACPICPUInterruptController *gCPUInterruptController;

#define super IOService
OSDefineMetaClassAndStructors(PDACPICPU, IOCPU)

/*
 *     | +-o P000@0  <class IOACPIPlatformDevice, id 0x100000136, registered, matched, active, busy 0 (748 ms), retain 8>
 *     | | | {
 *     | | |   "processor-number" = 0
 *     | | |   "IOGeneralInterest" = "IOCommand is not serializable"
 *     | | |   "cpu-type" = <0106>
 *     | | |   "device_type" = <"processor">
 *     | | |   "timebase-frequency" = <00ca9a3b>
 *     | | |   "clock-frequency" = <00371789>
 *     | | |   "plugin-type" = 1
 *     | | |   "processor-lapic" = 0                                  <--- LAPIC ID, not set if the CPU in the MADT is disabled.
 *     | | |   "processor-index" = 0
 *     | | |   "bus-frequency" = <00a8cb18>
 *     | | |   "name" = <"P000">
 *     | | |   "processor-id" = 1                                     <--- This is equal to the MADT's ProcessorId field
 *     | | | }
 *
 *     | +-o io-apic@fec00000  <class IOACPIPlatformDevice, id 0x100000146, registered, matched, active, busy 0 (11 ms), retain 7>
 *     | | | {
 *     | | |   "Physical Address" = 18446744073688580096                     <--- Phys Addr as reported in MADT
 *     | | |   "Vector Limit" = 112                                          <--- This is 0x70. The max before we hit MSI territory.
 *     | | |   "InterruptControllerName" = "io-apic-0"
 *     | | |   "Destination APIC ID" = 0
 *     | | |   "Base Vector Number" = 64                                     <--- IRQ vector 'base'? Value is 64 at IOAPIC 0 then
 *     | | |   "IOInterruptControllers" = ("IOPlatformInterruptController")
 *     | | |   "IOInterruptSpecifiers" = (<00000000>)
 *     | | |   "APIC ID" = 3                                                 <--- As reported in the MADT.
 *     | | | }
 */

bool PDACPICPU::start(IOService *provider)
{
    IOLog("PDACPICPU::start\n");
    if (!super::start(provider))
        return false;

    /* get our freaky stuff going */

    /* ACPIPE should hopefully feed us these values. */
    OSNumber *lapic = OSDynamicCast(OSNumber, provider->getProperty("processor-lapic"));
    OSNumber *id = OSDynamicCast(OSNumber, provider->getProperty("processor-id"));

    /* ZORMEISTER: this is a nightmare. */
    ml_processor_register(NULL, lapic->unsigned32BitValue(), &machProcessor, true, true);
    
    /* ^ so when the hell do i 'boot' the CPU? when do i 'start' the CPU? */
    /* do i call ml_processor_register again? what */

    registerService();
    return true;
}

void PDACPICPU::initCPU(bool boot)
{
    /* mmm... */
    if (boot) {
        gCPUInterruptController->enableCPUInterrupt(this);
    }

    this->setCPUState(kIOCPUStateRunning);
}

void PDACPICPU::haltCPU()
{
    IOLog("ACPICPU: halt\n");
    this->setCPUState(kIOCPUStateStopped);
    
    if (this->getCPUNumber() > 0) {
        processor_exit(this->machProcessor);
    } else {
        /* TODO: here, we should call into ACPICA to initiate the S<X> transaition */
        
    }
}

void PDACPICPU::quiesceCPU() {
    IOLog("ACPICPU: quiesce\n");
}

const OSSymbol *PDACPICPU::getCPUName()
{
    return getProvider()->copyName();
}

kern_return_t PDACPICPU::startCPU(vm_offset_t, vm_offset_t)
{
    IOLog("ACPICPU: CPU start requested\n");
    return KERN_SUCCESS;
}

void PDACPICPU::enterC1()
{
    asm volatile("hlt");
}

void PDACPICPU::enterCState(uint32_t cstateType)
{
    switch (cstateType)
    {
        case 1:
            enterC1();
            break;
        default:
            break;
    }
}

bool PDACPICPU::switchToPState(uint32_t index)
{
    if (index == currentPState)
        return true;

    IOLog("PDACPICPU: Switching to P-State %u\n", index);
    currentPState = index;
    return true;
}

uint32_t PDACPICPU::getBestCStateForLatency(uint32_t maxAllowedLatencyUs)
{
    // Simulated: always return C1
    return 1;
}
