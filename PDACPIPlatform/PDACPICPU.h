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

#ifndef _PDACPI_CPU_H
#define _PDACPI_CPU_H

#include <IOKit/IOService.h>
#include <libkern/c++/OSArray.h>

#if __has_include(<IOKit/IOCPU.h>)
#include <IOKit/IOCPU.h>
#else
typedef void (*ipi_handler_t)(void);

#include "ExternalHeaders/IOKit/IOCPU.h"
#endif

class PDACPICPU : public IOCPU
{
    OSDeclareDefaultStructors(PDACPICPU)

private:
    uint32_t currentPState;
    OSArray* pStateArray;
    OSArray* cStateArray;

public:
    virtual bool start(IOService* provider) override;
    
    virtual kern_return_t startCPU(vm_offset_t start_paddr, vm_offset_t arg_paddr) override;
    virtual void initCPU(bool boot) override;
    virtual void quiesceCPU(void) override;
    virtual void haltCPU(void) override;
    virtual const OSSymbol *getCPUName(void) override;
    
    void enterC1();
    void enterCState(uint32_t cstateType);
    bool switchToPState(uint32_t index);
    uint32_t getBestCStateForLatency(uint32_t maxAllowedLatencyUs);
    OSArray* getPStateArray() const { return pStateArray; }
    OSArray* getCStateArray() const { return cStateArray; }
};

#endif
