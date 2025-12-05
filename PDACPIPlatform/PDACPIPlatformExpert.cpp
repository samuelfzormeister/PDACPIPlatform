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

#include "PDACPIPlatformExpert.h"
#include "PDACPICPUInterruptController.h"
#include "PDACPIPlatformPrivate.h"
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>

#if __has_include(<IOKit/pci/IOPCIPrivate.h>)
#include <IOKit/pci/IOPCIPrivate.h>
#else
extern IOReturn IOPCIPlatformInitialize(void);
#endif

extern "C" {
#include "accommon.h"
#include "actbl.h"
};

/* The following globals are for interactions with the AppleAPIC driver, which has source code! */
/* see https://github.com/apple-oss-distributions/AppleAPIC */
const OSSymbol *gACPIPlatformAPICDestinationIDKey;
const OSSymbol *gACPIPlatformAPICPhysicalAddressKey;
const OSSymbol *gACPIPlatformAPICBaseVectorNumberKey;
const OSSymbol *gACPIPlatformAPICIDKey;
const OSSymbol *gACPIPlatformAPICHandleSleepWakeFunction;
const OSSymbol *gACPIPlatformAPICSetVectorPhysicalDestination;
const OSSymbol *gACPIPlatformInterruptSpecifiersKey;
const OSSymbol *gACPIPlatformInterruptControllerName;

PDACPICPUInterruptController *gCPUInterruptController;

extern IOReturn AcpiStatus2IOReturn(ACPI_STATUS stat);

extern "C" ACPI_TABLE_FADT* getFADT();
extern "C" void outw(uint16_t port, uint16_t val);
extern "C" void IOSleep(uint32_t ms);
extern "C" vm_offset_t ml_static_ptovirt(vm_offset_t);
extern "C" kern_return_t
ml_processor_register(
    cpu_id_t        cpu_id,
    uint32_t        lapic_id,
    processor_t     *processor_out,
    boolean_t       boot_cpu,
    boolean_t       start);

#pragma mark - PDACPIPlatformExpertGlobals

class PDACPIPlatformExpertGlobals {
public:
    PDACPIPlatformExpertGlobals();
    ~PDACPIPlatformExpertGlobals();
};

static PDACPIPlatformExpertGlobals PDACPIPlatformExpertGlobals;

PDACPIPlatformExpertGlobals::PDACPIPlatformExpertGlobals()
{
    /* Setup APIC keys */
    gACPIPlatformAPICBaseVectorNumberKey = OSSymbol::withCString("Base Vector Number");
    gACPIPlatformAPICDestinationIDKey = OSSymbol::withCString("Destination APIC ID");
    gACPIPlatformAPICIDKey = OSSymbol::withCString("APIC ID");
    gACPIPlatformAPICPhysicalAddressKey = OSSymbol::withCString("Physical Address");

    /* AppleAPICInterruptController::callPlatformFunction interfaces */
    gACPIPlatformAPICHandleSleepWakeFunction = OSSymbol::withCString("HandleSleepWake");
    gACPIPlatformAPICSetVectorPhysicalDestination = OSSymbol::withCString("SetVectorPhysicalDestination");
    
    gACPIPlatformInterruptSpecifiersKey = OSSymbol::withCString("IOInterruptSpecifiers");
    gACPIPlatformInterruptControllerName = OSSymbol::withCString("IOPlatformInterruptController");
}

PDACPIPlatformExpertGlobals::~PDACPIPlatformExpertGlobals()
{
    OSSafeReleaseNULL(gACPIPlatformInterruptControllerName);
    OSSafeReleaseNULL(gACPIPlatformInterruptSpecifiersKey);
    OSSafeReleaseNULL(gACPIPlatformAPICSetVectorPhysicalDestination);
    OSSafeReleaseNULL(gACPIPlatformAPICHandleSleepWakeFunction);
    OSSafeReleaseNULL(gACPIPlatformAPICPhysicalAddressKey);
    OSSafeReleaseNULL(gACPIPlatformAPICIDKey);
    OSSafeReleaseNULL(gACPIPlatformAPICDestinationIDKey);
    OSSafeReleaseNULL(gACPIPlatformAPICBaseVectorNumberKey);
}

#pragma mark - PDACPIPlatformExpert

#define super IOACPIPlatformExpert
OSDefineMetaClassAndStructors(PDACPIPlatformExpert, IOACPIPlatformExpert);

ACPI_TABLE_MADT *gAPICTable;
ACPI_TABLE_MCFG *gMCFGTable;

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::initializeACPICA
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::initializeACPICA()
{
    /* No need to init OSL seperately. AcpiInitializeSubsystem calls it as one of it's first calls. */
    kprintf("ACPI: ACPI CA %8X\n", ACPI_CA_VERSION);
    kprintf("ACPI: AcpiDbgLayer=%x, AcpiDbgLevel=%x", AcpiDbgLayer, AcpiDbgLevel);

    ACPI_STATUS status = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(status)) {
        IOLog("PDACPIPlatformExpert::start - [ERROR] AcpiInitializeSubsystem failed with status %s\n", AcpiFormatException(status));
        AcpiTerminate(); // Cleanup
        return false;
    }

    // For UEFI, passing NULL for InitialTableArray relies on AcpiOsGetRootPointer
    // to find the XSDT from the EFI System Table.
    // InitialTableCount (second param) is ignored by ACPICA when InitialTableArray (first param) is NULL.
    // AllowResize (third param) FALSE is typical.
    status = AcpiInitializeTables(NULL, 0, FALSE);
    if (ACPI_FAILURE(status)) {
        IOLog("PDACPIPlatformExpert::start - [ERROR] AcpiInitializeTables failed with status %s\n", AcpiFormatException(status));
        AcpiTerminate(); // Cleanup
        return false;
    }

    status = AcpiLoadTables();
    if (ACPI_FAILURE(status)) {
        IOLog("PDACPIPlatformExpert::start - [ERROR] AcpiLoadTables failed with status %s\n", AcpiFormatException(status));
        AcpiTerminate(); // Cleanup
        return false;
    }
    
    /* the system-type field is derived from the FADT, i think. */
    this->m_provider->setProperty("system-type", &AcpiGbl_FADT.PreferredProfile, 1);
    
    /* Document all of our available tables into an OSDictionary, this will be used by IOPCIFamily (and potentially future clients?) */
    this->catalogACPITables();
    
    /* Initialize IOPCIFamily and the IOMMU mapper */
    if (!this->initPCI()) {
        panic("ACPI: Failed to initialize PCI.");
    }
    
    /* Initialized at PDACPIPlatformExpert::enumerateProcessors */
    gCPUInterruptController = OSTypeAlloc(PDACPICPUInterruptController);

    /* We can't enable the Events subsystem or IRQ subsystem yet; we need IOCPU subclasses */
    status = AcpiEnableSubsystem(ACPI_NO_EVENT_INIT | ACPI_NO_HANDLER_INIT);
    if (ACPI_FAILURE(status)) {
        IOLog("PDACPIPlatformExpert::start - [ERROR] AcpiEnableSubsystem failed with status %s\n", AcpiFormatException(status));
        AcpiTerminate(); // Cleanup
        return false;
    }

    /* This is a critical point in time, and we should be cautious as to what we do before kickstarting the whole subsystem. */
    status = AcpiInitializeObjects(ACPI_NO_DEVICE_INIT);
    if (ACPI_FAILURE(status)) {
        IOLog("PDACPIPlatformExpert::start - [ERROR] AcpiInitializeObjects failed with status %s\n", AcpiFormatException(status));
        AcpiTerminate(); // Cleanup
        return false;
    }
    
    /* First, enumerate the Processor namespace to get the number of available CPUs in ACPI. */
    this->initACPIPlane();
    
    /* By this point, we should have all CPUs defined in both IODeviceTree:/cpus and the IOACPIPlane, and the IOService plane of course. */
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::initPCI
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::initPCI()
{
    /* Kindly tell IOPCIFamily to initialize MMIO mapping services. */
    /* If AppleVTD is integrated with IOPCIFamily should IOPCIFamily be extended with an AMD IOMMU driver? Or should another kext do that job? */
    if (IOPCIPlatformInitialize() == kIOReturnSuccess) {
        return true;
    }
    
    return false;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::initACPIPlane
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::initACPIPlane()
{
    /* As I have discovered, objects can be set to have different names on a per-plane basis. */
    this->m_provider->setName("acpi", gIOACPIPlane);
    this->m_provider->attachToParent(IORegistryEntry::getRegistryRoot(), gIOACPIPlane);
    
    /* Create the CPUs set of entries for IODeviceTree + IOACPIPlane */
    IOPlatformDevice *dev = OSTypeAlloc(IOPlatformDevice);
    
    if (dev) {
        if (!dev->init()) {
            OSSafeReleaseNULL(dev);
            return false;
        }
        
        dev->setName("cpus");

        /* HACK: trick setProperty into creating an OSData */
        dev->setProperty("name", (void *)"cpus", sizeof("cpus"));
        dev->attachToParent(this->m_provider, gIODTPlane);
        dev->attach(this);
        dev->registerService();
        
        
        this->createCPUNubs(dev);
    }
    
    return false;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::catalogACPITables
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::catalogACPITables()
{
    /* This was based off of osbsdtbl.c's shenanigans */
    struct AcpiTableMap {
        char Signature[4];
        UInt8 instance;
        ACPI_TABLE_HEADER *Tbl; /* well we've already mapped the damn thing so, might as well reuse the pointer. */
    };
    
    char name[32];
    ACPI_TABLE_HEADER *Table;
    UInt32 tables = AcpiGbl_RootTableList.CurrentTableCount;
    this->m_tableDict = OSDictionary::withCapacity(tables + 1);

    AcpiTableMap *tmp = (AcpiTableMap *)IOMalloc(sizeof(AcpiTableMap) * tables);

    /* ZORMEISTER: God this is such a hack... */
    for (UInt32 i = 0; i < tables; i++) {
        AcpiGetTableByIndex(i, &Table);
        for (UInt32 j = i; 0 > j; j--) { /* ZORMEISTER: walk backwards from our current position. */
            if (strncmp(tmp[j].Signature, Table->Signature, 4) == 0) {
                if (tmp[j].instance == 0) {
                    tmp[j].instance++;
                }

                tmp[i].instance = tmp[j].instance + 1;
            }
        }
    }

    /* ZORMEISTER: now do it again. */

    for (UInt32 k = 0; k < tables; k++) {
        /* Allocate an OSData using the ACPI table length. */
        OSData *data = OSData::withBytesNoCopy(tmp[k].Tbl, tmp[k].Tbl->Length);
        memset(name, 0, 32); /* clear out the stack variable */
        if (tmp[k].instance > 0) {
            snprintf(name, 32, "%4.4s-%u", tmp[k].Tbl->Signature, tmp[k].instance);
        } else {
            snprintf(name, 32, "%4.4s", tmp[k].Tbl->Signature);
        }
        
        /* Store these tables as we find them; they'll be used later. */
        if (strncmp(name, ACPI_SIG_MADT, 4) == 0) {
            gAPICTable = (ACPI_TABLE_MADT *)tmp[k].Tbl;
        } else if (strncmp(name, ACPI_SIG_MCFG, 4) == 0) {
            gMCFGTable = (ACPI_TABLE_MCFG *)tmp[k].Tbl;
        }

        this->m_tableDict->setObject(name, data);
        OSSafeReleaseNULL(data);
    }
    
    IOFree(tmp, sizeof(AcpiTableMap) * tables);

    return true;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::createCPUNubs
//---------------------------------------------------------------------------

struct PDACPICPUWalkContext {
    PDACPIPlatformExpert *platformExpert;
    IOPlatformDevice *parent;
    UInt32 count;
};

void PDACPIPlatformExpert::createCPUNubs(IOPlatformDevice *nub)
{
    PDACPICPUWalkContext ctx = {this, nub};
    
    AcpiWalkNamespace(ACPI_TYPE_PROCESSOR,
                      ACPI_ROOT_OBJECT, 1,
                      &processorNamespaceWalk,
                      NULL, &ctx, NULL);

    /* processorNamespaceWalk should check for ACPI0007 devices, and only create CPU objects. */
    AcpiWalkNamespace(ACPI_TYPE_DEVICE,
                      ACPI_ROOT_OBJECT, 1,
                      &processorNamespaceWalk,
                      NULL, &ctx, NULL);
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::processorNamespaceWalk
//---------------------------------------------------------------------------
ACPI_STATUS PDACPIPlatformExpert::processorNamespaceWalk(ACPI_HANDLE Handle,
                                                         UInt32 NestingLevel,
                                                         void *Context,
                                                         void **ReturnValue)
{
    ACPI_DEVICE_INFO *deviceInfo;
    PDACPICPUWalkContext *ctx = (PDACPICPUWalkContext *)Context;
    
    AcpiGetObjectInfo(Handle, &deviceInfo);
    
    switch (deviceInfo->Type) {
        case ACPI_TYPE_PROCESSOR: {
            break;
        }
        case ACPI_TYPE_DEVICE:
            break;
        default:
            break;
    }
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::createNub
//---------------------------------------------------------------------------
IOACPIPlatformDevice *PDACPIPlatformExpert::createNub(IOService *parent, ACPI_HANDLE handle)
{
    ACPI_DEVICE_INFO *info;
    IOACPIPlatformDevice *nub = OSTypeAlloc(IOACPIPlatformDevice);
    PDACPIHandle *hndl = (PDACPIHandle *)IOMallocZero(sizeof(PDACPIHandle));
    
    hndl->sig = PDACPI_HANDLE_SIG;
    hndl->fACPICAHandle = handle;
    
    nub->init(this, hndl, nullptr);
    
    return nub;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::enumerateProcessors
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::enumerateProcessors()
{
    UInt32 processorCount;
    processor_t proc; /* This is to avoid anything going wrong. */

    /*
     * This function aims to provide the i386 machine_routines subsystem with an accurate count of
     * logical processors that are available. eg: the Enabled bit is set.
     *
     * Later down the track during IOKit matching, PDACPICPU will call into ml_register_processor again to boot and start the CPUs.
     *
     * PDACPIPlatformExpert::start is the beginning of initialising the system, PDACPICPU picks up from where it leaves off and finalises
     * the IOKit Platform Expert initialisation as IOKit will panic if it can't find any CPUs.
     *
     * This will ALSO be backed by the AppleAPIC driver, as it manages the I/O APICs.
     */
    
    /* Assume that the host has only one logical processor if there's no MADT. */
    if (!gAPICTable) {
        kprintf("ACPI: No APIC table, assuming one logical CPU.\n");
        // ml_processor_register(NULL, 0, &proc, false, false);
        return;
    }
    
    UInt32 size = gAPICTable->Header.Length -= sizeof(ACPI_TABLE_MADT);
    
    kprintf("ACPI: LAPIC Base: 0x%X\n", gAPICTable->Address);
    
    ACPI_SUBTABLE_HEADER *sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)gAPICTable) + sizeof(ACPI_TABLE_MADT));
    
    while (0 != size) {
        switch (sub->Type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                ACPI_MADT_LOCAL_APIC *apic = (ACPI_MADT_LOCAL_APIC *)sub;
                kprintf("ACPI: ProcessorId=%d LocalApicId=%d %s", apic->ProcessorId,
                            apic->Id,
                            apic->LapicFlags & ACPI_MADT_ENABLED ? "Enabled" : "Disabled");
                if (apic->LapicFlags & ACPI_MADT_ENABLED) {
                    ml_processor_register(NULL, apic->Id, &proc, false, false);
                    processorCount++;
                }
                size -= sub->Length;
                sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)sub) + apic->Header.Length);
                break;
            }
                
            default:
                break;
        }
    }
    
    /* Prepare the CPUInterruptController for PDACPICPU spam */
    gCPUInterruptController->initCPUInterruptController(processorCount);
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::performACPIPowerOff
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::performACPIPowerOff()
{
    ACPI_TABLE_FADT* fadt = getFADT();
    if (!fadt || fadt->Pm1aControlBlock == 0)
        return;

    ACPI_OBJECT* s5Obj;
    ACPI_BUFFER buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    if (ACPI_FAILURE(AcpiEvaluateObject(NULL, (char*)"_S5", NULL, &buffer)))
        return;

    s5Obj = (ACPI_OBJECT*)buffer.Pointer;
    if (!s5Obj || s5Obj->Type != ACPI_TYPE_PACKAGE || s5Obj->Package.Count < 2)
        return;

    ACPI_OBJECT elem1 = s5Obj->Package.Elements[0];
    uint16_t slp_typ = elem1.Integer.Value & 0x7;
    uint16_t slp_en = 1 << 13;
    uint16_t value = (slp_typ << 10) | slp_en;

    outw(fadt->Pm1aControlBlock, value);
    if (fadt->Pm1bControlBlock)
        outw(fadt->Pm1bControlBlock, value);

    IOSleep(10000);
    while (1) asm volatile("hlt");
}
