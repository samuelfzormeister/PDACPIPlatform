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
const OSSymbol *gACPIPlatformBaseVectorNumberKey;
const OSSymbol *gACPIPlatformAPICIDKey;
const OSSymbol *gACPIPlatformAPICHandleSleepWakeFunction;
const OSSymbol *gACPIPlatformAPICSetVectorPhysicalDestination;
const OSSymbol *gACPIPlatformInterruptSpecifiersKey;
const OSSymbol *gACPIPlatformInterruptControllerName;
const OSSymbol *gACPIPlatformVectorCountKey;

PDACPICPUInterruptController *gCPUInterruptController;
PDACPIPlatformExpert *gACPIPlatformExpert;

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

#ifndef MAX_CPUS
#define MAX_CPUS 64
#endif

extern "C" int cpu_to_lapic[MAX_CPUS];

#pragma mark - PDACPIPlatformExpertGlobals

class PDACPIPlatformExpertGlobals {
public:
    PDACPIPlatformExpertGlobals();
    ~PDACPIPlatformExpertGlobals();
};

static PDACPIPlatformExpertGlobals PDACPIPlatformExpertGlobals;

PDACPIPlatformExpertGlobals::PDACPIPlatformExpertGlobals()
{
    /* Setup IRQ related keys */
    gACPIPlatformBaseVectorNumberKey = OSSymbol::withCString("Base Vector Number");
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

    //
    // TODO: work out if im allowed to do this here
    //
    status = AcpiInstallInterface((char *)"Darwin");
    if (ACPI_FAILURE(status)) {
        kprintf("PDACPIPlatformExpert::start - [WARNING] AcpiInstallInterface failed with status %s\n", AcpiFormatException(status));
    }

    status = AcpiLoadTables();
    if (ACPI_FAILURE(status)) {
        IOLog("PDACPIPlatformExpert::start - [ERROR] AcpiLoadTables failed with status %s\n", AcpiFormatException(status));
        AcpiTerminate(); // Cleanup
        return false;
    }
    
    /* the system-type field is derived from the FADT, i think. */
    m_provider->setProperty("system-type", &AcpiGbl_FADT.PreferredProfile, 1);

    /* Initialized at PDACPIPlatformExpert::enumerateProcessors */
    gCPUInterruptController = OSTypeAlloc(PDACPICPUInterruptController);
    
    enumerateProcessors();

    initACPIPlane();

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
    
    kprintf("ACPI: swicthing to I/O APIC mode...\n");
    
    ACPI_OBJECT obj;
    obj.Integer.Type = ACPI_TYPE_INTEGER;
    obj.Integer.Value = 1;
    ACPI_OBJECT_LIST list = {1, &obj};
    
    status = AcpiEvaluateObject(ACPI_ROOT_OBJECT, "\\_PIC", &list, NULL);
    if (ACPI_FAILURE(status)) {
        panic("ACPI: couldn't enable apic mode... %s", AcpiFormatException(status));
    }
    
    return true;
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
    m_provider->setName("acpi", gIOACPIPlane);
    m_provider->attachToParent(IORegistryEntry::getRegistryRoot(), gIOACPIPlane);
    
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
        dev->attachToParent(m_provider, gIODTPlane);
        dev->registerService();
        
        createCPUNubs(dev);
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
    IOPlatformExpertDevice *peDev;
    UInt32 count;
};

void PDACPIPlatformExpert::createCPUNubs(IOPlatformDevice *nub)
{
    PDACPICPUWalkContext ctx = {this, nub, m_provider, 0};
    
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
    ACPI_OBJECT_TYPE type;
    PDACPICPUWalkContext *ctx = (PDACPICPUWalkContext *)Context;
    
    AcpiGetType(Handle, &type);
    
    if (type == ACPI_TYPE_PROCESSOR) {
        
    }
    
    return AE_OK;
}

IOReturn PDACPIPlatformExpert::configureProcessor(IOACPIPlatformDevice *cpu)
{
    ACPI_BUFFER buf = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };

    PDACPIHandle *hndl = (PDACPIHandle *)cpu->getDeviceHandle();
    
    cpu->setDeviceType(IOACPIPlatformDevice::kTypeProcessor);
    
    ACPI_STATUS stat = AcpiEvaluateObject(hndl->fACPICAHandle, NULL, NULL, &buf);
    if (stat == AE_OK) {
        ACPI_OBJECT *obj = (ACPI_OBJECT *)buf.Pointer;
        
        cpu->setProperty("processor-id", obj->Processor.ProcId, 32);
        cpu->setProperty("device_type", "processor");
        
        //
        // Earlier, we should have walked the MADT and enumerated I/O APICs and CPUs.
        //
        UInt32 size = gAPICTable->Header.Length -= sizeof(ACPI_TABLE_MADT);
        
        ACPI_SUBTABLE_HEADER *sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)gAPICTable) + sizeof(ACPI_TABLE_MADT));
        
        while (0 != size) {
            switch (sub->Type) {
                case ACPI_MADT_TYPE_LOCAL_APIC: {
                    ACPI_MADT_LOCAL_APIC *apic = (ACPI_MADT_LOCAL_APIC *)sub;
                    if (apic->ProcessorId == obj->Processor.ProcId) {
                        cpu->setProperty("processor-lapic", apic->Id, 32);
                    }
                    size -= sub->Length;
                    sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)sub) + sub->Length);
                    break;
                }
                default:
                    size -= sub->Length;
                    sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)sub) + sub->Length);
                    break;
            }
        }
    }
    
    return kIOReturnSuccess;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::createNub
//---------------------------------------------------------------------------
IOACPIPlatformDevice *PDACPIPlatformExpert::createNub(IOService *parent, ACPI_HANDLE handle)
{
    ACPI_OBJECT_TYPE type;
    IOACPIPlatformDevice *nub = OSTypeAlloc(IOACPIPlatformDevice);
    PDACPIHandle *hndl = (PDACPIHandle *)IOMallocZero(sizeof(PDACPIHandle));

    hndl->sig = PDACPI_HANDLE_SIG;
    hndl->fACPICAHandle = handle;
    
    nub->init(this, hndl, NULL);
    nub->attach(parent);
    
    switch (type) {
        case ACPI_TYPE_THERMAL:
            configureThermalZone(nub);
            break;
        case ACPI_TYPE_PROCESSOR:
            configureProcessor(nub);
            break;
        case ACPI_TYPE_DEVICE:
            configureDevice(nub);
        default:
            break;
    }
    
    return nub;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::enumerateProcessors
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::enumerateProcessors()
{
    UInt32 processorCount;
    UInt32 ioapicCount;
    processor_t proc;       /* This is to avoid anything going wrong. */

    /*
     * This function aims to provide the i386 machine routines subsystem with an accurate count of
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
                sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)sub) + sub->Length);
                break;
            }
            case ACPI_MADT_TYPE_IO_APIC: {
                //
                // TODO: Create I/O APIC nubs now or later?
                //
                ACPI_MADT_IO_APIC *ioapic = (ACPI_MADT_IO_APIC *)sub;
                kprintf("ACPI: I/O APIC: ID: %d, 0x%08X", ioapic->Id, ioapic->Address);
                createApicNub(ioapic);
                break;
            }
            default:
                size -= sub->Length;
                sub = (ACPI_SUBTABLE_HEADER *)(((uint8_t *)sub) + sub->Length);
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
    AcpiEnterSleepStatePrep(ACPI_STATE_S5);
    AcpiEnterSleepState(ACPI_STATE_S5);
}

#pragma mark - I/O APIC nub creation!!

//
// IOPCIMessagedInterruptController's base is IRQ 70h.
//
// Which is 112 in decimal.
//
// The first I/O APIC starts from IRQ0 at LAPIC vector 32.
//
// LAPIC vectors 0xD0 - 0xDF are reserved by OSFMK.
//
#define kDefaultVectorLimit 0x70

void PDACPIPlatformExpert::createApicNub(ACPI_MADT_IO_APIC *ioapic)
{
    char name[32];
    UInt32 destApic = 0;
    
    IOPlatformDevice *nub = OSTypeAlloc(IOPlatformDevice);
    
    bzero(name, sizeof(name));
    snprintf(name, 32, "io-apic-%d", m_ioApicCount);
    
    if (ioapic->GlobalIrqBase >= 0x70) {
        kprintf("ACPI: Skipping I/O APIC (ID: %d) to avoid IOPCIFamily conflict", ioapic->Id);
        return;
    }
    
    nub->setName("io-apic");
    nub->setProperty("APIC ID", ioapic->Id, 32);
    nub->setProperty("Base Vector Number", m_lastIOAPICMax + ioapic->GlobalIrqBase);
    nub->setProperty("Phyiscal Address", ioapic->Address, 32);
    nub->setProperty("Destination APIC ID", destApic, 32);
    nub->setProperty("InterruptControllerName", name);
}

#pragma mark - Interrupt Handling

//
// LAPIC: 0 - 0xFF
//
// assign gsis and msi as need be... ffs.
//
#define MAX_INTERRUPTS 512

struct {
    IOInterruptVectorNumber src;            // source
    IOInterruptController *controller;      // one of the i/o apics, or msi.
    int flags;                              // active high or active low
    int type;                               // kIOInterruptTypeXXX
    bool assigned;                          // if assignInterrupt has been called using this vector before.
} gInterruptTable[MAX_INTERRUPTS];

IOReturn PDACPIPlatformExpert::assignInterrupt(IOService *toService, int source, int type, int flags)
{
    kprintf("ACPI: we have been asked to assign an interrupt; source: %d", source);
    
    if (!(gInterruptTable[source].flags & kIODTInterruptShared) &&
        gInterruptTable[source].assigned == true) {
        kprintf("ACPI: !!! ASSIGNING NON SHARED INTERRUPT TO ANOTHER DEVICE !!!\n");
    }
    
    //
    // We actually just directly map our IRQ source to the global dispatch table, in
    // PDACPIPlatformExpert::registerInterruptController, so, lookup stuff here.
    //
    IOInterruptController *con = gInterruptTable[source].controller;
    OSArray *controllers = OSDynamicCast(OSArray, toService->getProperty(gIOInterruptControllersKey));
    OSArray *specifiers = OSDynamicCast(OSArray, toService->getProperty(gIOInterruptSpecifiersKey));

    if (controllers == NULL || specifiers == NULL) {
        controllers = OSArray::withCapacity(1);
        specifiers = OSArray::withCapacity(1);
    } else {
        controllers->ensureCapacity(controllers->getCapacity() + 1);
        specifiers->ensureCapacity(specifiers->getCapacity() + 1);
    }
    
    OSData *spec = OSData::withCapacity(8);
    
    //
    // Violate the buffer.
    //
    UInt32 *sources = (UInt32 *)spec->getBytesNoCopy();
    *sources = source;
    
    controllers->setObject(con->getProperty(gACPIPlatformInterruptControllerName));
    specifiers->setObject(spec);
    
    gInterruptTable[source].type = type;
    gInterruptTable[source].flags = flags;
    gInterruptTable[source].assigned = true;
    
    return kIOReturnSuccess;
}

#pragma mark - dispatchInterrupt

extern "C" void lapic_end_of_interrupt(void);

IOReturn PDACPIPlatformExpert::dispatchInterrupt(int source)
{
    if (gInterruptTable[source].controller == NULL) {
        lapic_end_of_interrupt();
        return kIOReturnSuccess;
    }
    
    return gInterruptTable[source].controller->handleInterrupt(NULL, NULL, source);
}

#pragma mark - PDACPIPlatformExpert::registerInterruptController

IOReturn PDACPIPlatformExpert::registerInterruptController(OSSymbol *name, IOInterruptController *interruptController)
{
    IOReturn ret = kIOReturnInvalid;
    
    kprintf("ACPI: registering interrupt controller: %s", name->getCStringNoCopy());
    
    ret = super::registerInterruptController(name, interruptController);
    if (ret != kIOReturnSuccess) {
        return ret;
    }
    
    OSNumber *vectorBase = OSDynamicCast(OSNumber, interruptController->getProperty(gACPIPlatformAPICBaseVectorNumberKey));
    OSNumber *numVectors = OSDynamicCast(OSNumber, interruptController->getProperty(gACPIPlatformAPICIDKey));
    
    for (UInt32 n = vectorBase->unsigned32BitValue(); n < vectorBase->unsigned32BitValue() + numVectors->unsigned32BitValue(); n++) {
        if (n >= 0xD0 && n <= 0xE0) {
            //
            // We can't assign these IDs, they're controlled used osfmk.
            //
            continue;
        }
        gInterruptTable[n].controller = interruptController;
        gInterruptTable[n].src = n;
    }
    
    return ret;
}


#pragma mark - handlePEHaltRestart

int PDACPIPlatformExpert::handlePEHaltRestart(UInt32 type)
{
    return gACPIPlatformExpert->platformHaltRestart(type);
}

int PDACPIPlatformExpert::platformHaltRestart(UInt32 type)
{
    if (type == kPERestartCPU) {
        kprintf("ACPI: rebooting :)\n");
        ACPI_STATUS stat = AcpiReset();
        kprintf("ACPI: we... didn't... reboot? (%s)\n", AcpiFormatException(stat));
        IODelay(10000);
        panic("ACPI: WE WERE SUPPOSED TO REBOOT.");
    } else if (type == kPEHaltCPU) {
        kprintf("ACPI: shutting down...\n");
        AcpiEnterSleepStatePrep(ACPI_STATE_S5);
        AcpiEnterSleepState(ACPI_STATE_S5);
        kprintf("ACPI: i.. uh, wuh???\n");
        panic("ACPI: we didn't shut down!!!");
    }
    
    return -1;
}
