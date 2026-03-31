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
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>

// --- HACK: uACPI doesn't currently export a table iteration function. (Remove this when 5.0 releases) --- //
#include <uacpi/internal/tables.h>

#if __has_include(<IOKit/pci/IOPCIPrivate.h>)
#include <IOKit/pci/IOPCIPrivate.h>
#else
extern IOReturn IOPCIPlatformInitialize(void);
#endif



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

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::initializeACPI
//---------------------------------------------------------------------------
IOReturn PDACPIPlatformExpert::initializeACPI(void)
{
    // --- allocate a page for uacpi's early access --- //
    void *storage = IOMalloc(PAGE_SIZE);
    
    uacpi_setup_early_table_access(storage, PAGE_SIZE);
    
    if (catalogACPITables() == false) {
        kprintf("ACPI: We failed to catalog the local ACPI tables.\n");
    }
    
    if (initPCI() == false) {
        kprintf("ACPI: Failed to init PCI.\n");
    }
    
    // --- begin uACPI initialisation at this point --- //
    uacpi_status stat = uacpi_initialize(0);
    if (stat != UACPI_STATUS_OK) {
        kprintf("ACPI: uACPI failed initilisation step #1: %s", uacpi_status_to_string(stat));
        return uAcpiStatus2IOKit(stat);
    }
    
    stat = uacpi_namespace_load();
    if (stat != UACPI_STATUS_OK) {
        kprintf("ACPI: uACPI failed initilisation step #2: %s", uacpi_status_to_string(stat));
        return uAcpiStatus2IOKit(stat);
    }
    
    // --- at this point we have what we need to create the IOACPIPlane. --- //
    initACPIPlane();
    
    return kIOReturnSuccess;
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
        dev->attach(this);
        dev->registerService();
        
        
        this->createCPUNubs(dev);
    }
    
    return false;
}

static uacpi_iteration_decision uacpiTableIteration(void *user, struct uacpi_installed_table *tbl, uacpi_size idx)
{
    static UInt32 ssdt_count = 0;
    char buf[8];
    
    PDACPIPlatformExpert *expert = (PDACPIPlatformExpert *)user;
    
    kprintf("ACPI: processing %s", tbl->hdr.signature);
    
    bzero(buf, sizeof(buf));
    
    if (strncmp("SSDT", tbl->hdr.signature, 4) == 0) {
        snprintf(buf, sizeof(buf), "%4.4s-%u", tbl->hdr.signature, ssdt_count);
    } else {
        snprintf(buf, sizeof(buf), "%4.4s", tbl->hdr.signature);
    }
    
    OSData *dat = OSData::withBytes(tbl->ptr, sizeof(void *));
    expert->m_acpiTables->setObject(buf, dat);
    
    OSSafeReleaseNULL(dat);
    
    return UACPI_ITERATION_DECISION_CONTINUE;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::catalogACPITables
//---------------------------------------------------------------------------
bool PDACPIPlatformExpert::catalogACPITables()
{
    if (uacpi_table_subsystem_available() == false) {
        kprintf("ACPI: uACPI table subsystem is unavailable -- what?\n");
        return false;
    }
    
    // --- Update this when uACPI 5.0 releases --- //
    uacpi_for_each_table(0, &uacpiTableIteration, this);
    
    return true;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::enumerateProcessors
//---------------------------------------------------------------------------
uacpi_iteration_decision PDACPIPlatformExpert::enumerateProcessors(uacpi_namespace_node *node, uacpi_u32 depth)
{
    bool val;
    
    if (uacpi_namespace_node_is(node, UACPI_OBJECT_PROCESSOR, &val) == UACPI_STATUS_OK) {
        if (val == true) {
            uacpi_processor_info pi;
            
            uacpi_object *obj = uacpi_namespace_node_get_object(node);
            
            uacpi_object_get_processor_info(obj, &pi);
            
            kprintf("ACPI: found Processor %d", pi.id);
            
            // --- This is where we would create the processor node (and map it to it's Local APIC ID). --- //
        } else {
            if (uacpi_namespace_node_is(node, UACPI_OBJECT_DEVICE, &val) == UACPI_STATUS_OK) {
                if (val == true) {
                    const char *hids[] = {"ACPI0007"};
                    if (uacpi_device_matches_pnp_id(node, hids)) {
                        uacpi_id_string *uid;
                        uacpi_eval_uid(node, &uid);
                        
                        // --- another TODO... convert 2 integer & build processor node. --- //
                    }
                }
            }
        }
    }
    
    
    return UACPI_ITERATION_DECISION_CONTINUE;
}

//---------------------------------------------------------------------------
// PDACPIPlatformExpert::createCPUNubs
//---------------------------------------------------------------------------
void PDACPIPlatformExpert::createCPUNubs(IOPlatformDevice *nub)
{
    uacpi_object_type_bits bits = (uacpi_object_type_bits)(UACPI_OBJECT_PROCESSOR_BIT | UACPI_OBJECT_DEVICE_BIT);
    
    OSMemberFunctionCast(uacpi_iteration_callback, this, &PDACPIPlatformExpert::enumerateProcessors);
    uacpi_namespace_for_each_child(uacpi_namespace_root(),
                                   OSMemberFunctionCast(uacpi_iteration_callback, this, &PDACPIPlatformExpert::enumerateProcessors),
                                   NULL,
                                   bits,
                                   UACPI_MAX_DEPTH_ANY, this);
    
    
}
