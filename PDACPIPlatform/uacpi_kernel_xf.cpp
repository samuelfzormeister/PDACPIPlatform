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

// --- This is the uACPI interface between PDACPIPlatform and itself. --- //

#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <kern/queue.h>
#include <kern/thread_call.h>
#include <pexpert/i386/efi.h>
#include <stdarg.h>
#include <uacpi/kernel_api.h>

/*
 * TODO:
 *  - uACPI event system
 *  - System I/O and PCI access
 *  - IRQ handler installation
 */

// --- memory mapping variables --- //
static IOLock *gAcpiMemoryMapLock;
static OSCollectionIterator *gAcpiMemoryMapIterator;
static OSSet *gAcpiMemoryMapSet;

// --- Work scheduling variables --- //
IOSimpleLock *gAcpiWorkQueueLock;
queue_t gAcpiFreeWorkQueue;
queue_t gAcpiActiveWorkQueue;

static struct AcpiWorkContext *gAcpiWorkContextBuffer = NULL;

void uacpi_kernel_work_dispatch(thread_call_param_t param0, thread_call_param_t param1);

struct AcpiWorkContext {
    queue_chain_t queue_pos;
    thread_call_t thread;
    uacpi_handle handle;
    uacpi_work_handler handler;
};

#define MAX_THREADS 25

// --- Memory Allocation variables --- //
#define ACPI_MEMORY_MAGIC 0x616D656D

struct AcpiMemoryTag {
    UInt32 magic;
    IOByteCount size;
};

//---------------------------------------------------------------------------
// uacpi_kernel_intialize
//---------------------------------------------------------------------------
uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl)
{
    if (current_init_lvl == UACPI_INIT_LEVEL_EARLY) {
        // --- initialise thread services --- //
        queue_init(gAcpiFreeWorkQueue);
        queue_init(gAcpiActiveWorkQueue);
        gAcpiWorkQueueLock = IOSimpleLockAlloc();
        
        if (gAcpiWorkQueueLock == NULL) {
            return UACPI_STATUS_OUT_OF_MEMORY;
        }
        
        gAcpiWorkContextBuffer = (AcpiWorkContext *)IOMalloc(sizeof(AcpiWorkContext) * MAX_THREADS);
        
        if (gAcpiWorkContextBuffer == NULL) {
            return UACPI_STATUS_OUT_OF_MEMORY;
        }
        
        for (int i = 0; i < MAX_THREADS; i++) {
            AcpiWorkContext *wk = &gAcpiWorkContextBuffer[i];
            
            // --- I believe we want to treat uACPI threads with high priority. --- //
            wk->thread = thread_call_allocate_with_priority(&uacpi_kernel_work_dispatch,
                                                            wk,
                                                            THREAD_CALL_PRIORITY_KERNEL_HIGH);
            wk->handle = NULL;
            wk->handler = NULL;
            
            queue_enter(gAcpiFreeWorkQueue, wk, struct AcpiWorkContext *, queue_pos);
        }
        
        // --- initialise memory map tracking --- //
        gAcpiMemoryMapLock = IOLockAlloc();
        gAcpiMemoryMapSet = OSSet::withCapacity(4);
        gAcpiMemoryMapIterator = OSCollectionIterator::withCollection(gAcpiMemoryMapSet);
        
        
    }
    
    return UACPI_STATUS_OK;
}

#pragma mark - Memory Mapping Interface

// --- prepare for uACPI 5.0 preliminarily. --- //
#ifndef UACPI_MAP_FAILED
#define UACPI_MAP_FAILED UACPI_NULL
#endif

//---------------------------------------------------------------------------
// uacpi_kernel_map
//---------------------------------------------------------------------------
void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
{
    IOMemoryDescriptor *memDesc = IOMemoryDescriptor::withAddressRange(addr, len, kIODirectionIn, kernel_task);
    if (memDesc) {
        IOMemoryMap *map = memDesc->map();
        OSSafeReleaseNULL(memDesc);
        if (map) {
            IOLockLock(gAcpiMemoryMapLock);
            gAcpiMemoryMapSet->setObject(map);
            IOLockUnlock(gAcpiMemoryMapLock);
            return (void *)map->getVirtualAddress();
        } else {
            return UACPI_MAP_FAILED;
        }
    } else {
        return UACPI_MAP_FAILED;
    }
}

//---------------------------------------------------------------------------
// uacpi_kernel_unmap
//---------------------------------------------------------------------------
void uacpi_kernel_unmap(void *addr, uacpi_size len)
{
    IOLockLock(gAcpiMemoryMapLock);
    while (IOMemoryMap *map = OSDynamicCast(IOMemoryMap, gAcpiMemoryMapIterator->getNextObject())) {
        if (map->getVirtualAddress() == (IOVirtualAddress)addr && map->getLength() == len) {
            map->unmap();
            gAcpiMemoryMapSet->removeObject(map);
            break;
        }
    }
    IOLockUnlock(gAcpiMemoryMapLock);
    
    gAcpiMemoryMapIterator->reset();
}

#pragma mark - Memory Allocation Interface

//---------------------------------------------------------------------------
// uacpi_kernel_alloc
//---------------------------------------------------------------------------
void *uacpi_kernel_alloc(uacpi_size size)
{
    uint8_t *alloc = (uint8_t *)IOMalloc(size + sizeof(struct AcpiMemoryTag));
    struct AcpiMemoryTag *mem = (AcpiMemoryTag *)alloc;
    mem->magic = ACPI_MEMORY_MAGIC;
    mem->size = size + sizeof(struct AcpiMemoryTag);
    return (alloc + sizeof(struct AcpiMemoryTag));
}

//---------------------------------------------------------------------------
// uacpi_kernel_alloc_zeroed
//---------------------------------------------------------------------------
void *uacpi_kernel_alloc_zeroed(uacpi_size size)
{
    uint8_t *alloc = (uint8_t *)IOMallocZero(size + sizeof(struct AcpiMemoryTag));
    struct AcpiMemoryTag *mem = (AcpiMemoryTag *)alloc;
    mem->magic = ACPI_MEMORY_MAGIC;
    mem->size = size + sizeof(struct AcpiMemoryTag);
    return (alloc + sizeof(struct AcpiMemoryTag));
}

//---------------------------------------------------------------------------
// uacpi_kernel_free
//---------------------------------------------------------------------------
void uacpi_kernel_free(void *mem)
{
    AcpiMemoryTag *tag = (AcpiMemoryTag *)(((uint8_t *)mem) - sizeof(struct AcpiMemoryTag));
    if (tag->magic == ACPI_MEMORY_MAGIC) {
        IOFree(tag, tag->size);
    } else {
        return;
    }
}

#pragma mark - Time Interface

//---------------------------------------------------------------------------
// uacpi_kernel_get_nanoseconds_since_boot
//---------------------------------------------------------------------------
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    UInt64 absoluteTime;
    UInt64 ns;
    
    clock_get_uptime(&absoluteTime);
    absolutetime_to_nanoseconds(absoluteTime, &ns);
    
    return ns;
}

//---------------------------------------------------------------------------
// uacpi_kernel_stall
//---------------------------------------------------------------------------
void uacpi_kernel_stall(uacpi_u8 usec) {
    IODelay(usec);
}

//---------------------------------------------------------------------------
// uacpi_kernel_sleep
//---------------------------------------------------------------------------
void uacpi_kernel_sleep(uacpi_u64 msec) {
    IOSleep((UInt32)msec);
};

#pragma mark - Thread Management Interface

//---------------------------------------------------------------------------
// uacpi_kernel_work_dispatch
//---------------------------------------------------------------------------
void uacpi_kernel_work_dispatch(thread_call_param_t param0, thread_call_param_t param1)
{
    struct AcpiWorkContext *wk = (struct AcpiWorkContext *)param0;
    
    wk->handler(wk->handle);
    
    IOSimpleLockLock(gAcpiWorkQueueLock);
    queue_remove(gAcpiActiveWorkQueue, wk, struct AcpiWorkContext *, queue_pos);
    queue_enter_first(gAcpiFreeWorkQueue, wk, struct AcpiWorkContext *, queue_pos);
    IOSimpleLockUnlock(gAcpiWorkQueueLock);
}

//---------------------------------------------------------------------------
// uacpi_kernel_schedule_work
//---------------------------------------------------------------------------
uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler hndlr, uacpi_handle ctx)
{
    AcpiWorkContext *wk = NULL;
    
    uacpi_kernel_log(UACPI_LOG_DEBUG, "scheduling work of %d type.", type);
    
    IOSimpleLockLock(gAcpiWorkQueueLock);
    
    if (queue_first(gAcpiFreeWorkQueue) != gAcpiFreeWorkQueue) {
        queue_remove_first(gAcpiFreeWorkQueue, wk, struct AcpiWorkContext *, queue_pos);
        queue_enter(gAcpiActiveWorkQueue, wk, struct AcpiWorkContext *, queue_pos);
    } else {
        uacpi_kernel_log(UACPI_LOG_WARN, "We've ran out of free slots in the queue.\n");
    }

    IOSimpleLockUnlock(gAcpiWorkQueueLock);
    
    if (wk == NULL) {
        // --- There's no dedicated overrun status code, so just use Internal Error for now. --- //
        return UACPI_STATUS_INTERNAL_ERROR;
    } else {
        thread_call_enter(wk->thread);
    }
    
    return UACPI_STATUS_OK;
}

//---------------------------------------------------------------------------
// uacpi_kernel_wait_for_work_completion
//---------------------------------------------------------------------------
uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
    while (true) {
        if (queue_first(gAcpiActiveWorkQueue) == gAcpiActiveWorkQueue) {
            return UACPI_STATUS_OK;
        }
    }
}

//---------------------------------------------------------------------------
// uacpi_kernel_get_thread_id
//---------------------------------------------------------------------------
uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
    return (uacpi_thread_id)current_thread();
}

#pragma mark - Lock Interface

//---------------------------------------------------------------------------
// uacpi_kernel_create_mutex
//---------------------------------------------------------------------------
uacpi_handle uacpi_kernel_create_mutex(void) {
    return IOLockAlloc();
}

//---------------------------------------------------------------------------
// uacpi_kernel_free_mutex
//---------------------------------------------------------------------------
void uacpi_kernel_free_mutex(uacpi_handle lock) {
    return IOLockFree((IOLock *)lock);
}

//---------------------------------------------------------------------------
// uacpi_kernel_acquire_mutex
//---------------------------------------------------------------------------
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle lck, uacpi_u16 timeout) {
    if (timeout == 0) {
        if (IOLockTryLock((IOLock *)lck) == TRUE) {
            return UACPI_STATUS_OK;
        }
    } else if (timeout == 0xFFFF) {
        while (true) {
            if (IOLockTryLock((IOLock *)lck) == FALSE) {
                IOSleep(1);
            } else {
                return UACPI_STATUS_OK;
            }
        }
        return UACPI_STATUS_OK;
    } else {
        while (timeout > 0) {
            if (IOLockTryLock((IOLock *)lck) == FALSE) {
                IOSleep(1);
                timeout--;
            } else {
                return UACPI_STATUS_OK;
            }
        }
    }

    return UACPI_STATUS_TIMEOUT;
}

//---------------------------------------------------------------------------
// uacpi_kernel_release_mutex
//---------------------------------------------------------------------------
void uacpi_kernel_release_mutex(uacpi_handle lock) {
    IOLockUnlock((IOLock *)lock);
}

//---------------------------------------------------------------------------
// uacpi_kernel_create_spinlock
//---------------------------------------------------------------------------
uacpi_handle uacpi_kernel_create_spinlock(void) {
    return IOSimpleLockAlloc();
}

//---------------------------------------------------------------------------
// uacpi_kernel_free_spinlock
//---------------------------------------------------------------------------
void uacpi_kernel_free_spinlock(uacpi_handle lock) {
    IOSimpleLockFree((IOSimpleLock *)lock);
}

//---------------------------------------------------------------------------
// uacpi_kernel_lock_spinlock
//---------------------------------------------------------------------------
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle lock) {
    uacpi_cpu_flags flgs = ml_set_interrupts_enabled(FALSE);
    IOSimpleLockLock((IOSimpleLock *)lock);
    return flgs;
}

//---------------------------------------------------------------------------
// uacpi_kernel_unlock_spinlock
//---------------------------------------------------------------------------
void uacpi_kernel_unlock_spinlock(uacpi_handle lock, uacpi_cpu_flags flags) {
    IOSimpleLockUnlock((IOSimpleLock *)lock);
    ml_set_interrupts_enabled((boolean_t)flags);
};

#pragma mark - Event Interface

//---------------------------------------------------------------------------
// uacpi_kernel_create_event
//---------------------------------------------------------------------------
uacpi_handle uacpi_kernel_create_event(void)
{
    semaphore_t sem;
    
    semaphore_create(current_task(), &sem, 0, 0);
    
    return sem;
}

//---------------------------------------------------------------------------
// uacpi_kernel_free_event
//---------------------------------------------------------------------------
void uacpi_kernel_free_event(uacpi_handle sem) {
    semaphore_destroy(current_task(), (semaphore_t)sem);
}

//---------------------------------------------------------------------------
// uacpi_kernel_wait_for_event
//---------------------------------------------------------------------------
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle sem, uacpi_u16 ms)
{
    uint64_t abs;
    nanoseconds_to_absolutetime(ms * NSEC_PER_MSEC, &abs);
    clock_absolutetime_interval_to_deadline(abs, &abs);
    
    auto res = semaphore_wait_deadline((semaphore_t)sem, abs);
    
    return (res == KERN_SUCCESS);
}

//---------------------------------------------------------------------------
// uacpi_kernel_signal_event
//---------------------------------------------------------------------------
void uacpi_kernel_signal_event(uacpi_handle sem)
{
    semaphore_signal((semaphore_t)sem);
}

//---------------------------------------------------------------------------
// uacpi_kernel_reset_event
//---------------------------------------------------------------------------
void uacpi_kernel_reset_event(uacpi_handle sem) {
    // --- I take that back, we can't really 'reset' a Mach semaphore. Not without modifying it. --- //
}

#pragma mark - Other Interfaces

//---------------------------------------------------------------------------
// uacpi_kernel_get_rsdp
//---------------------------------------------------------------------------
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *phys)
{
    IOPhysicalAddress addr;
    
    // --- Bootloader should populate these for us. --- //
    static const char *guids[] = {
        "/efi/configuration-table/8868E871-E4F1-11D3-BC22-0080C73C8881",
        "/efi/configuration-table/EB9D2D30-2D88-11D3-9A16-0090273FC14D"
    };
    
    for (int i = 0; i < 2; i++) {
        IORegistryEntry *registry = IORegistryEntry::fromPath(guids[i], gIODTPlane);
        
        if (registry) {
            OSData *tableAddrData = OSDynamicCast(OSData, registry->getProperty("table"));
            
            if (tableAddrData && tableAddrData->getLength() <= sizeof(IOPhysicalAddress64)) {
                addr = *(IOPhysicalAddress *)tableAddrData->getBytesNoCopy();
            } else {
                uacpi_kernel_log(UACPI_LOG_WARN, "%s hasn't been correctly populated.\n", guids[i]);
                continue;
            }
        } else {
            continue;
        }

        registry->release();
    }
    
    return UACPI_STATUS_OK;
}

//---------------------------------------------------------------------------
// uacpi_kernel_log
//---------------------------------------------------------------------------
void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char* format, ...)
{
    va_list list;
    va_start(list, format);
    uacpi_kernel_vlog(lvl, format, list);
    va_end(list);
}

//---------------------------------------------------------------------------
// uacpi_kernel_vlog
//---------------------------------------------------------------------------
void uacpi_kernel_vlog(uacpi_log_level level, const uacpi_char* format, uacpi_va_list list)
{
    char buffer[512];
    static const char *lvls[] = {
        "INVALID",
        "ERROR",
        "WARNING",
        "INFO",
        "TRACE",
        "DEBUG",
    };
    
    bzero(buffer, sizeof(buffer));
    vsnprintf(buffer, sizeof(buffer), format, list);
    kprintf("[uACPI][%s]: %s", lvls[level], buffer);
}

#pragma mark - Interrupt Interfaces

//---------------------------------------------------------------------------
// uacpi_kernel_disable_interrupts
//---------------------------------------------------------------------------
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void)
{
    return ml_set_interrupts_enabled(FALSE);
}

//---------------------------------------------------------------------------
// uacpi_kernel_restore_interrupts
//---------------------------------------------------------------------------
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state)
{
    ml_set_interrupts_enabled((boolean_t)state);
}
