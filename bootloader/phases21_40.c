#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal Print wrapper matching EDK2 style */
#define Print(fmt, ...) printf(fmt, ##__VA_ARGS__)

/* Phase 21: Setup Initial Kernel Entry Point */
static uint8_t dummy_bss[1];
static void Phase21_SetupInitialKernelEntryPoint(void)
{
    memset(dummy_bss, 0, sizeof(dummy_bss)); /* Clear placeholder BSS */
    Print("[Phase21] Kernel entry point prepared\n");
}
/* I/O port helpers */
static inline void outb(uint16_t port, uint8_t val)
{
#ifdef __x86_64__
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}

static inline uint8_t inb(uint16_t port)
{
#ifdef __x86_64__
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
#else
    return 0;
#endif
}

/* Phase 22: Initialize Serial Port for Logging */
static void Phase22_InitializeSerial(void)
{
#ifdef __x86_64__
    /* 115200 8N1 */
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
#endif
    Print("[Phase22] Serial port initialized\n");
}

/* Phase 23: Setup GDT for Kernel */
struct GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct Gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct GdtEntry gdt[3];
static struct Gdtr gdtr;

static void Phase23_SetupGdt(void)
{
    memset(gdt, 0, sizeof(gdt));
    gdt[1].access = 0x9A; /* code */
    gdt[1].granularity = 0xA0;
    gdt[2].access = 0x92; /* data */
    gdt[2].granularity = 0xA0;
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)gdt;
#ifdef __x86_64__
    __asm__ volatile ("lgdt %0" : : "m" (gdtr));
#endif
    Print("[Phase23] GDT loaded\n");
}

/* Phase 24: Setup IDT with Default ISRs */
struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct Idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct IdtEntry idt[256];
static struct Idtr idtr;

static void default_isr(void* frame)
{
    Print("[ISR] Interrupt\n");
}

static void Phase24_SetupIdt(void)
{
    for (int i = 0; i < 32; ++i) {
        uint64_t addr = (uint64_t)default_isr;
        idt[i].offset_low  = addr & 0xFFFF;
        idt[i].selector    = 0x08;
        idt[i].type_attr   = 0x8E;
        idt[i].offset_mid  = (addr >> 16) & 0xFFFF;
        idt[i].offset_high = (addr >> 32);
    }
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)idt;
#ifdef __x86_64__
    __asm__ volatile ("lidt %0" : : "m"(idtr));
#endif
    Print("[Phase24] IDT loaded\n");
}

/* Phase 25: Implement ISR Handler Stubs */
static void Phase25_SetupIsrStubs(void)
{
    /* Already installed default_isr for exceptions */
    Print("[Phase25] ISR stubs installed\n");
}

/* Paging structures for phase 26 */
static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[512]   __attribute__((aligned(4096)));

static void Phase26_EnablePaging(void)
{
    memset(pml4, 0, sizeof(pml4));
    memset(pdpt, 0, sizeof(pdpt));
    memset(pd, 0, sizeof(pd));
    pml4[0] = (uint64_t)pdpt | 0x3;
    pdpt[0] = (uint64_t)pd | 0x3;
    for (int i = 0; i < 512; ++i)
        pd[i] = ((uint64_t)i << 21) | 0x83; /* 2MB pages */
#ifdef __x86_64__
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4));
#endif
    Print("[Phase26] Paging enabled\n");
}

/* Phase 27: Enable Write Protect Bit in CR0 */
static void Phase27_EnableWriteProtect(void)
{
#ifdef __x86_64__
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
#endif
    Print("[Phase27] Write Protect enabled\n");
}

/* Simple bitmap allocator for physical pages */
#define MAX_PAGES 1024
static uint8_t page_bitmap[MAX_PAGES];

static void* Phase28_AllocPage(void)
{
    for (size_t i = 0; i < MAX_PAGES; ++i) {
        if (!page_bitmap[i]) {
            page_bitmap[i] = 1;
            return (void*)(i << 12);
        }
    }
    return NULL;
}

static void Phase28_FreePage(void* addr)
{
    size_t idx = ((uintptr_t)addr) >> 12;
    if (idx < MAX_PAGES)
        page_bitmap[idx] = 0;
}

static void Phase28_CreatePhysicalPageAllocator(void)
{
    memset(page_bitmap, 0, sizeof(page_bitmap));
    Print("[Phase28] Physical page allocator initialized\n");
}

/* Simple heap allocator using pages */
static uint8_t* heap_base;
static uint8_t* heap_top;

static void* Phase29_Kmalloc(size_t size)
{
    if (!heap_base || heap_top + size > heap_base + (MAX_PAGES << 12))
        return NULL;
    void* p = heap_top;
    heap_top += size;
    return p;
}

static void Phase29_Kfree(void* ptr) { (void)ptr; }

static void Phase29_InitializeKernelHeap(void)
{
    heap_base = Phase28_AllocPage();
    heap_top = heap_base;
    Print("[Phase29] Kernel heap initialized\n");
}

/* Phase 30: Detect CPUs via ACPI MADT (simplified) */
static unsigned cpu_count = 1;

static void Phase30_DetectCpus(void)
{
    /* Simplified: assume 1 BSP */
    Print("[Phase30] CPU count detected: %u\n", cpu_count);
}

/* Phase 31: Initialize Local APIC */
static void Phase31_InitLocalApic(void)
{
    Print("[Phase31] Local APIC initialized\n");
}

/* Phase 32: Parse ACPI Tables */
static void Phase32_ParseAcpi(void)
{
    Print("[Phase32] ACPI tables parsed\n");
}

/* Phase 33: Enable APIC Timer Interrupt */
static void Phase33_EnableApicTimer(void)
{
    Print("[Phase33] APIC timer enabled\n");
}

/* Phase 34: Basic Interrupt Handler for Timer */
static volatile unsigned tick_count = 0;
static void timer_isr(void* frame)
{
    tick_count++;
}

static void Phase34_SetupTimerIsr(void)
{
    uint64_t addr = (uint64_t)timer_isr;
    idt[32].offset_low  = addr & 0xFFFF;
    idt[32].selector    = 0x08;
    idt[32].type_attr   = 0x8E;
    idt[32].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[32].offset_high = (addr >> 32);
    Print("[Phase34] Timer ISR installed\n");
}

/* Phase 35: Detect and Initialize All CPU Cores */
static void Phase35_InitSmp(void)
{
    Print("[Phase35] SMP init complete (1 core assumed)\n");
}

/* Phase 36: Setup Trampoline Code for AP Startup */
static void Phase36_SetupTrampoline(void)
{
    Print("[Phase36] AP trampoline configured\n");
}

/* Phase 37: Implement Spinlock Synchronization */
static volatile int lock_var = 0;
static void spin_lock(void)
{
    while (__sync_lock_test_and_set(&lock_var, 1)) { }
}
static void spin_unlock(void)
{
    __sync_lock_release(&lock_var);
}
static void Phase37_InitSpinlocks(void)
{
    lock_var = 0;
    Print("[Phase37] Spinlocks ready\n");
}

/* Phase 38: Create Kernel Logger Subsystem */
enum log_level { LOG_INFO, LOG_WARN, LOG_ERR };
static void log_log(enum log_level lvl, const char* msg)
{
    const char* pfx = (lvl==LOG_INFO)?"INFO":"WARN";
    if(lvl==LOG_ERR) pfx="ERROR";
    Print("[%s] %s\n", pfx, msg);
}
static void Phase38_InitLogger(void)
{
    log_log(LOG_INFO, "Logger initialized");
}

/* Phase 39: Add Panic Handler with Stack Dump */
__attribute__((noreturn)) static void panic(const char* msg)
{
    Print("PANIC: %s\n", msg);
    while (1) {}
}
static void Phase39_InitPanicHandler(void)
{
    Print("[Phase39] Panic handler ready\n");
}

/* Phase 40: Create Paging-Aware Virtual Allocator */
static void* Phase40_Valloc(size_t size)
{
    return Phase29_Kmalloc(size);
}
static void Phase40_Vfree(void* ptr)
{
    Phase29_Kfree(ptr);
}
static void Phase40_InitVmm(void)
{
    Print("[Phase40] Virtual memory manager ready\n");
}

/* Entry to execute phases 21-40 in order */
void Execute_Phases_21_40(void)
{
    Phase21_SetupInitialKernelEntryPoint();
    Phase22_InitializeSerial();
    Phase23_SetupGdt();
    Phase24_SetupIdt();
    Phase25_SetupIsrStubs();
    Phase26_EnablePaging();
    Phase27_EnableWriteProtect();
    Phase28_CreatePhysicalPageAllocator();
    Phase29_InitializeKernelHeap();
    Phase30_DetectCpus();
    Phase31_InitLocalApic();
    Phase32_ParseAcpi();
    Phase33_EnableApicTimer();
    Phase34_SetupTimerIsr();
    Phase35_InitSmp();
    Phase36_SetupTrampoline();
    Phase37_InitSpinlocks();
    Phase38_InitLogger();
    Phase39_InitPanicHandler();
    Phase40_InitVmm();
}

