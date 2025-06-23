#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/File.h>
#include <Protocol/Tcg2Protocol.h>
#include <Protocol/Hash2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Security2.h>
#include <Protocol/GraphicsOutput.h>
#include "loader_structs.h"

#define KERNEL_PATH L"kernel.elf"

static EFI_HANDLE mImageHandle;
static EFI_SYSTEM_TABLE *mSystemTable;

// Phase 1: Print boot start message
static EFI_STATUS Phase01_PrintBootMessage(void)
{
    Print(L"AiOS Bootloader Started!\n");
    return EFI_SUCCESS;
}

// Phase 3: Open kernel.elf via filesystem protocol
static EFI_STATUS Phase03_OpenKernel(EFI_FILE_PROTOCOL **File)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_PROTOCOL *Root;

    Status = gBS->HandleProtocol(mImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status))
        return Status;

    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
    if (EFI_ERROR(Status))
        return Status;

    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status))
        return Status;

    Status = Root->Open(Root, File, KERNEL_PATH, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status))
        Print(L"Opened %s successfully\n", KERNEL_PATH);
    return Status;
}

// Phase 4: Read and validate ELF header
typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} ELF64_EHDR;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} ELF64_PHDR;

#define PT_LOAD 1

static EFI_STATUS Phase04_ReadElfHeader(EFI_FILE_PROTOCOL *File, ELF64_EHDR *Hdr)
{
    EFI_STATUS Status;
    UINTN Size = sizeof(ELF64_EHDR);
    Status = File->Read(File, &Size, Hdr);
    if (EFI_ERROR(Status) || Size != sizeof(ELF64_EHDR))
        return EFI_LOAD_ERROR;

    if (Hdr->e_ident[0] != 0x7f || Hdr->e_ident[1] != 'E' || Hdr->e_ident[2] != 'L' || Hdr->e_ident[3] != 'F')
        return EFI_LOAD_ERROR;
    if (Hdr->e_ident[4] != 2) // ELFCLASS64
        return EFI_LOAD_ERROR;
    if (Hdr->e_machine != 0x3E) // x86_64
        return EFI_LOAD_ERROR;

    Print(L"Valid ELF64 detected\n");
    return EFI_SUCCESS;
}

// Phase 5: Parse program headers and count PT_LOAD
static EFI_STATUS Phase05_ParseProgramHeaders(EFI_FILE_PROTOCOL *File, const ELF64_EHDR *Hdr, ELF64_PHDR **Headers)
{
    EFI_STATUS Status;
    UINTN Size = Hdr->e_phentsize * Hdr->e_phnum;
    Status = gBS->AllocatePool(EfiLoaderData, Size, (VOID**)Headers);
    if (EFI_ERROR(Status))
        return Status;
    Status = File->SetPosition(File, Hdr->e_phoff);
    if (EFI_ERROR(Status))
        return Status;
    Status = File->Read(File, &Size, *Headers);
    if (EFI_ERROR(Status))
        return Status;

    UINTN Count = 0;
    for (UINTN i = 0; i < Hdr->e_phnum; ++i)
        if ((*Headers)[i].p_type == PT_LOAD)
            Count++;
    Print(L"%u loadable segments\n", Count);
    return EFI_SUCCESS;
}

// Phase 6: Allocate memory for kernel PT_LOAD segments
static EFI_STATUS Phase06_AllocateSegments(const ELF64_EHDR *Hdr, const ELF64_PHDR *Phdrs)
{
    for (UINTN i = 0; i < Hdr->e_phnum; ++i) {
        if (Phdrs[i].p_type != PT_LOAD)
            continue;
        UINTN Pages = EFI_SIZE_TO_PAGES(Phdrs[i].p_memsz);
        EFI_PHYSICAL_ADDRESS Addr = Phdrs[i].p_paddr;
        EFI_STATUS Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, Pages, &Addr);
        if (EFI_ERROR(Status))
            return Status;
        Print(L"Allocated %u pages at %lx\n", Pages, Addr);
    }
    return EFI_SUCCESS;
}

// Phase 7: Load segment data from file
static EFI_STATUS Phase07_LoadSegments(EFI_FILE_PROTOCOL *File, const ELF64_EHDR *Hdr, const ELF64_PHDR *Phdrs)
{
    EFI_STATUS Status;
    for (UINTN i = 0; i < Hdr->e_phnum; ++i) {
        if (Phdrs[i].p_type != PT_LOAD)
            continue;
        UINTN Size = Phdrs[i].p_filesz;
        Status = File->SetPosition(File, Phdrs[i].p_offset);
        if (EFI_ERROR(Status))
            return Status;
        Status = File->Read(File, &Size, (VOID*)(UINTN)Phdrs[i].p_paddr);
        if (EFI_ERROR(Status))
            return Status;
        if (Phdrs[i].p_memsz > Phdrs[i].p_filesz)
            SetMem((VOID*)(UINTN)(Phdrs[i].p_paddr + Phdrs[i].p_filesz), (UINTN)(Phdrs[i].p_memsz - Phdrs[i].p_filesz), 0);
    }
    return EFI_SUCCESS;
}

// Phase 9: Get UEFI memory map
static EFI_STATUS Phase09_GetMemoryMap(LOADER_PARAMS *Params)
{
    EFI_STATUS Status;
    UINTN MapSize = 0;
    EFI_MEMORY_DESCRIPTOR *Map = NULL;
    UINTN MapKey;
    UINTN DescSize;
    UINT32 DescVer;

    Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (Status != EFI_BUFFER_TOO_SMALL)
        return Status;

    Status = gBS->AllocatePool(EfiLoaderData, MapSize, (VOID**)&Map);
    if (EFI_ERROR(Status))
        return Status;

    Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR(Status))
        return Status;

    Params->MemoryMap = Map;
    Params->MemoryMapSize = MapSize;
    Params->MapKey = MapKey;
    Params->DescriptorSize = DescSize;
    Params->DescriptorVersion = DescVer;

    Print(L"Memory map retrieved (%u entries)\n", MapSize / DescSize);
    return EFI_SUCCESS;
}

// Phase 11: Compute SHA-256 and extend to PCR[4]
static EFI_STATUS Phase11_HashAndExtend(const VOID *Data, UINTN Size)
{
    EFI_STATUS Status;
    EFI_TCG2_PROTOCOL *Tcg2;
    Status = gBS->LocateProtocol(&gEfiTcg2ProtocolGuid, NULL, (VOID**)&Tcg2);
    if (EFI_ERROR(Status))
        return Status;

    EFI_TCG2_EVENT *Event;
    UINT32 EventSize = sizeof(EFI_TCG2_EVENT) - sizeof(UINT8) + sizeof("KernelLoad");
    Status = gBS->AllocatePool(EfiBootServicesData, EventSize, (VOID**)&Event);
    if (EFI_ERROR(Status))
        return Status;

    Event->Size = EventSize;
    Event->Header.HeaderSize = sizeof(Event->Header);
    Event->Header.HeaderVersion = EFI_TCG2_EVENT_HEADER_VERSION;
    Event->Header.PCRIndex = 4;
    Event->Header.EventType = EV_IPL;
    CopyMem(Event->Event, "KernelLoad", sizeof("KernelLoad"));

    Status = Tcg2->HashLogExtendEvent(Tcg2, mImageHandle, (UINT8*)Data, Size, Event);
    if (!EFI_ERROR(Status))
        Print(L"Kernel hash extended to PCR[4]\n");
    return Status;
}

// Phase 13: Verify signature via Secure Boot db
static EFI_STATUS Phase13_VerifySignature(EFI_FILE_PROTOCOL *File)
{
    EFI_STATUS Status;
    EFI_SECURITY2_PROTOCOL *Sec2;
    Status = gBS->LocateProtocol(&gEfiSecurity2ProtocolGuid, NULL, (VOID**)&Sec2);
    if (EFI_ERROR(Status))
        return Status;

    EFI_HANDLE Dummy = NULL;
    Status = Sec2->FileAuthenticationState(Sec2, 0, (EFI_DEVICE_PATH_PROTOCOL*)File, FALSE);
    return Status;
}

static VOID Phase14_VerificationFailure(void)
{
    Print(L"Signature or hash verification failed\n");
    while (1);
}

// Phase 15: Initialize graphics output protocol
static EFI_STATUS Phase15_InitGop(EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop, LOADER_PARAMS *Params)
{
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)Gop);
    if (EFI_ERROR(Status))
        return Status;
    Params->GopModeInfo = (*Gop)->Mode->Info;
    Params->FrameBufferBase = (*Gop)->Mode->FrameBufferBase;
    Params->FrameBufferSize = (*Gop)->Mode->FrameBufferSize;
    Print(L"Resolution %ux%u\n", Params->GopModeInfo->HorizontalResolution, Params->GopModeInfo->VerticalResolution);
    return EFI_SUCCESS;
}

// Phase 16: Render simple rectangle
static VOID Phase16_RenderRectangle(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    UINT32 Width = 200;
    UINT32 Height = 100;
    UINT32 X = (Gop->Mode->Info->HorizontalResolution - Width) / 2;
    UINT32 Y = (Gop->Mode->Info->VerticalResolution - Height) / 2;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel = {0xFF, 0x00, 0x00, 0};
    Gop->Blt(Gop, &Pixel, EfiBltVideoFill, 0, 0, X, Y, Width, Height, 0);
}

// Phase 17: Text mode fallback
static VOID Phase17_FallbackText(void)
{
    Print(L"Graphics unavailable, using text mode\n");
}

// Phase 10: Jump to kernel entry point
typedef VOID (*KERNEL_ENTRY)(LOADER_PARAMS *);
static EFI_STATUS Phase10_JumpKernel(const ELF64_EHDR *Hdr, LOADER_PARAMS *Params)
{
    KERNEL_ENTRY Entry = (KERNEL_ENTRY)(UINTN)Hdr->e_entry;
    Print(L"Jumping to kernel...\n");
    gBS->ExitBootServices(mImageHandle, Params->MapKey);
    Entry(Params);
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    mImageHandle = ImageHandle;
    mSystemTable = SystemTable;
    InitializeLib(ImageHandle, SystemTable);

    Phase01_PrintBootMessage();

    EFI_FILE_PROTOCOL *KernelFile;
    if (EFI_ERROR(Phase03_OpenKernel(&KernelFile)))
        return EFI_NOT_FOUND;

    ELF64_EHDR Ehdr;
    if (EFI_ERROR(Phase04_ReadElfHeader(KernelFile, &Ehdr)))
        return EFI_LOAD_ERROR;

    ELF64_PHDR *Phdrs;
    if (EFI_ERROR(Phase05_ParseProgramHeaders(KernelFile, &Ehdr, &Phdrs)))
        return EFI_LOAD_ERROR;

    if (EFI_ERROR(Phase06_AllocateSegments(&Ehdr, Phdrs)))
        return EFI_OUT_OF_RESOURCES;

    if (EFI_ERROR(Phase07_LoadSegments(KernelFile, &Ehdr, Phdrs)))
        return EFI_LOAD_ERROR;

    LOADER_PARAMS Params;
    if (EFI_ERROR(Phase09_GetMemoryMap(&Params)))
        return EFI_BUFFER_TOO_SMALL;
    Params.KernelBase = Phdrs[0].p_paddr;
    Params.KernelEntry = Ehdr.e_entry;

    UINTN KernelSize = 0;
    KernelFile->SetPosition(KernelFile, 0);
    KernelFile->Read(KernelFile, &KernelSize, NULL); // get file size -> this will return 0

    if (EFI_ERROR(Phase11_HashAndExtend((VOID*)(UINTN)Phdrs[0].p_paddr, (UINTN)Phdrs[0].p_memsz)))
        Print(L"TPM hash failed\n");

    EFI_STATUS SigStatus = Phase13_VerifySignature(KernelFile);
    if (EFI_ERROR(SigStatus))
        Phase14_VerificationFailure();

    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    if (!EFI_ERROR(Phase15_InitGop(&Gop, &Params)))
        Phase16_RenderRectangle(Gop);
    else
        Phase17_FallbackText();

    Phase10_JumpKernel(&Ehdr, &Params);
    return EFI_SUCCESS;
}

// ======================================================
// Phases 21-40 kernel support functions

// Forward declaration of kernel_main implemented elsewhere
void kernel_main(void);

/* Phase21_KernelEntry
 * Entry point from bootloader. Setup stack, clear BSS and call kernel_main().
 */
__attribute__((naked)) void Phase21_KernelEntry(void)
{
    __asm__ __volatile__(
        "lea 0x70000(%%rip), %%rsp\n"
        "extern __bss_start\n"
        "extern __bss_end\n"
        "lea __bss_start(%%rip), %%rdi\n"
        "lea __bss_end(%%rip), %%rcx\n"
        "sub %%rdi, %%rcx\n"
        "xor %%rax, %%rax\n"
        "rep stosb\n"
        "call kernel_main\n"
        "cli\n"
        "hlt\n");
}

/* Serial I/O for early logging */
#define COM1_PORT 0x3F8
static inline void outb(uint16_t port, uint8_t val) { __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }

void Phase22_SerialInit(void)
{
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x03);
}

static void serial_putc(char c)
{
    while (!(inb(COM1_PORT + 5) & 0x20));
    outb(COM1_PORT, c);
}

/* GDT structures */
struct GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed));

struct GdtPtr { uint16_t limit; uint64_t base; } __attribute__((packed));

static struct GdtEntry gdt[3];
static struct GdtPtr   gdtp;

static void set_gdt_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].base_low  = base & 0xFFFF;
    gdt[idx].base_mid  = (base >> 16) & 0xFF;
    gdt[idx].access    = access;
    gdt[idx].gran      = (limit >> 16) & 0x0F;
    gdt[idx].gran     |= gran & 0xF0;
    gdt[idx].base_hi   = (base >> 24) & 0xFF;
}

void Phase23_GdtInit(void)
{
    set_gdt_entry(0, 0, 0, 0, 0);
    set_gdt_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    set_gdt_entry(2, 0, 0xFFFFF, 0x92, 0xA0);
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint64_t)gdt;
    __asm__ volatile("lgdt %0" :: "m"(gdtp));
    __asm__ volatile(
        "mov $0x10, %%ds\n"
        "mov $0x10, %%es\n"
        "mov $0x10, %%ss\n"
        "ljmp $0x08, $.1\n"
        ".1:"
        :::"memory");
}

/* IDT and ISR handling */
struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
} __attribute__((packed));

struct IdtPtr { uint16_t limit; uint64_t base; } __attribute__((packed));

static struct IdtEntry idt[256];
static struct IdtPtr   idtp;

typedef struct {
    uint64_t rip; uint64_t cs; uint64_t rflags; uint64_t rsp; uint64_t ss;
} __attribute__((packed)) InterruptFrame;

void Phase25_IsrHandler(uint64_t num, uint64_t err, InterruptFrame *frame)
{
    Print(L"Exception %lu err=%lx\n", num, err);
    (void)frame; while(1);
}

#define DECL_ISR(n) \
__attribute__((naked)) void isr##n(void) { \
    __asm__ __volatile__(\
        "pushq $0\n" \
        "pushq $"#n"\n" \
        "jmp common_isr" ); }

DECL_ISR(0); DECL_ISR(1); DECL_ISR(2); DECL_ISR(3); DECL_ISR(4); DECL_ISR(5); DECL_ISR(6); DECL_ISR(7); DECL_ISR(8); DECL_ISR(9); DECL_ISR(10); DECL_ISR(11); DECL_ISR(12); DECL_ISR(13); DECL_ISR(14); DECL_ISR(15); DECL_ISR(16); DECL_ISR(17); DECL_ISR(18); DECL_ISR(19); DECL_ISR(20); DECL_ISR(21); DECL_ISR(22); DECL_ISR(23); DECL_ISR(24); DECL_ISR(25); DECL_ISR(26); DECL_ISR(27); DECL_ISR(28); DECL_ISR(29); DECL_ISR(30); DECL_ISR(31);

__attribute__((naked)) void common_isr(void)
{
    __asm__ __volatile__(
        "pushaq\n"
        "mov %rsp, %rdi\n"
        "mov 8(%rsp), %rsi\n"
        "mov 16(%rsp), %rdx\n"
        "callq Phase25_IsrHandler\n"
        "popaq\n"
        "add $16, %rsp\n"
        "iretq\n");
}

static void set_idt_gate(int num, void (*handler)(void))
{
    uint64_t addr = (uint64_t)handler;
    idt[num].offset_low = addr & 0xFFFF;
    idt[num].selector = 0x08;
    idt[num].ist = 0;
    idt[num].type = 0x8E;
    idt[num].offset_mid = (addr >> 16) & 0xFFFF;
    idt[num].offset_hi = addr >> 32;
    idt[num].zero = 0;
}

void Phase24_IdtInit(void)
{
    for(int i=0;i<32;i++)
        set_idt_gate(i, ((void(**)())isr0)[i]);
    idtp.limit = sizeof(idt)-1;
    idtp.base = (uint64_t)idt;
    __asm__ volatile("lidt %0" :: "m"(idtp));
}

/* Paging structures */
static uint64_t *pml4;

void Phase26_EnablePaging(void)
{
    pml4 = AllocatePages(1);
    SetMem(pml4, EFI_PAGE_SIZE, 0);
    uint64_t *pdp = AllocatePages(1);
    SetMem(pdp, EFI_PAGE_SIZE, 0);
    uint64_t *pd  = AllocatePages(1);
    SetMem(pd, EFI_PAGE_SIZE, 0);
    uint64_t *pt  = AllocatePages(1);
    SetMem(pt, EFI_PAGE_SIZE, 0);
    pml4[0] = (uint64_t)pdp | 3;
    pdp[0]  = (uint64_t)pd | 3;
    for(int i=0;i<512;i++) pt[i] = (i*0x1000ULL) | 3;
    pd[0] = (uint64_t)pt | 3;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pml4));
}

void Phase27_EnableWriteProtect(void)
{
    uint64_t cr0; __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    cr0 |= (1<<16);
    __asm__ volatile("mov %0, %%cr0"::"r"(cr0));
}

/* Simple bitmap page allocator */
#define MAX_PAGES 1024
static uint8_t pmm_bitmap[MAX_PAGES/8];
static EFI_PHYSICAL_ADDRESS mem_base;

static int bitmap_find_free(void)
{
    for (int i=0;i<MAX_PAGES;i++)
        if (!(pmm_bitmap[i/8] & (1<<(i%8))))
            return i;
    return -1;
}

void Phase28_PmmInit(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size, UINTN desc_size)
{
    for (UINTN i=0; i<map_size/desc_size; ++i) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + i*desc_size);
        if (d->Type == EfiConventionalMemory) {
            mem_base = d->PhysicalStart;
            break;
        }
    }
    SetMem(pmm_bitmap, sizeof(pmm_bitmap), 0);
}

void *pmm_alloc(void)
{
    int idx = bitmap_find_free();
    if (idx<0) return NULL;
    pmm_bitmap[idx/8] |= (1<<(idx%8));
    return (void*)(mem_base + idx*4096ULL);
}

void pmm_free(void *p)
{
    UINTN idx = ((EFI_PHYSICAL_ADDRESS)p - mem_base)/4096ULL;
    pmm_bitmap[idx/8] &= ~(1<<(idx%8));
}

/* Simple heap using linked list */
typedef struct BlockHeader {
    struct BlockHeader *next;
    UINTN size;
} BlockHeader;

static BlockHeader *heap_head;

void Phase29_HeapInit(void *heap_start, UINTN size)
{
    heap_head = (BlockHeader*)heap_start;
    heap_head->next = NULL;
    heap_head->size = size - sizeof(BlockHeader);
}

void *kmalloc(UINTN size)
{
    BlockHeader *cur = heap_head, *prev = NULL;
    while (cur) {
        if (cur->size >= size) {
            if (cur->size <= size + sizeof(BlockHeader)) {
                if (prev) prev->next = cur->next; else heap_head = cur->next;
                return (cur+1);
            } else {
                BlockHeader *newb = (BlockHeader*)((UINT8*)(cur+1)+size);
                newb->next = cur->next;
                newb->size = cur->size - size - sizeof(BlockHeader);
                if (prev) prev->next = newb; else heap_head = newb;
                cur->size = size;
                return (cur+1);
            }
        }
        prev = cur; cur = cur->next;
    }
    return NULL;
}

void kfree(void *p)
{
    BlockHeader *b = (BlockHeader*)p - 1;
    b->next = heap_head;
    heap_head = b;
}

/* MADT parsing to count CPUs */
UINTN Phase30_CountCpus(EFI_ACPI_DESCRIPTION_HEADER *madt)
{
    UINT8 *ptr = (UINT8*)madt + sizeof(EFI_ACPI_DESCRIPTION_HEADER);
    UINT8 *end = (UINT8*)madt + madt->Length;
    UINTN count = 0;
    while (ptr < end) {
        UINT8 type = ptr[0];
        UINT8 len  = ptr[1];
        if (type == 0)
            count++;
        ptr += len;
    }
    Print(L"Detected %u CPUs\n", count);
    return count;
}

/* LAPIC setup */
volatile uint32_t *lapic = (uint32_t*)0xFEE00000;

void Phase31_InitLapic(void)
{
    lapic[0xF0/4] = 0x1FF;
}

/* ACPI table discovery */
EFI_ACPI_DESCRIPTION_HEADER* Phase32_FindTable(EFI_SYSTEM_TABLE* SystemTable, char *sig)
{
    EFI_CONFIGURATION_TABLE *ct = SystemTable->ConfigurationTable;
    for (UINTN i=0;i<SystemTable->NumberOfTableEntries;i++) {
        if (!CompareMem(&ct[i].VendorGuid, &gEfiAcpiTableGuid, sizeof(EFI_GUID))) {
            EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *rsdp = ct[i].VendorTable;
            EFI_ACPI_DESCRIPTION_HEADER *xsdt = (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)rsdp->XsdtAddress;
            UINT64 *entries = (UINT64*)((UINT8*)xsdt + sizeof(EFI_ACPI_DESCRIPTION_HEADER));
            UINTN count = (xsdt->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER))/8;
            for (UINTN j=0;j<count;j++) {
                EFI_ACPI_DESCRIPTION_HEADER *h = (EFI_ACPI_DESCRIPTION_HEADER*)(UINTN)entries[j];
                if (CompareMem(h->Signature, sig, 4)==0)
                    return h;
            }
        }
    }
    return NULL;
}

void Phase33_EnableApicTimer(uint32_t ticks)
{
    lapic[0x3E0/4] = 0x3;
    lapic[0x380/4] = ticks;
    lapic[0x320/4] = 0x20;
}

static volatile UINT64 timer_ticks = 0;
void Phase34_TimerHandler(uint64_t num, uint64_t err, InterruptFrame *f)
{
    timer_ticks++;
    if (!(timer_ticks % 100))
        Print(L"Ticks: %lu\n", timer_ticks);
    lapic[0xB0/4] = 0;
    (void)num; (void)err; (void)f;
}

/* SMP startup */
void Phase35_StartAPs(UINT8 *lapic_ids, UINTN count, void *trampoline)
{
    for (UINTN i=1;i<count;i++) {
        lapic[0x280/4] = lapic_ids[i]<<24;
        lapic[0x300/4] = 0x4500;
        for(volatile int d=0; d<100000; d++);
        lapic[0x280/4] = lapic_ids[i]<<24;
        lapic[0x300/4] = 0x4600 | ((UINT64)trampoline>>12);
        for(volatile int d=0; d<100000; d++);
    }
}

/* Trampoline setup */
void Phase36_SetupTrampoline(void *trampoline, void *entry)
{
    UINT8 *code = (UINT8*)trampoline;
    code[0] = 0xEA;
    *(uint32_t*)&code[1] = (uint32_t)(uint64_t)entry;
    *(uint16_t*)&code[5] = 0;
}

/* Spinlock */
typedef struct { volatile uint32_t v; } spinlock_t;
void Phase37_Lock(spinlock_t *l)
{
    while (__sync_lock_test_and_set(&l->v,1)) while(l->v);
}
void Phase37_Unlock(spinlock_t *l)
{
    __sync_lock_release(&l->v);
}

/* Logging */
typedef enum {LOG_INFO, LOG_WARN, LOG_ERROR} log_level_t;

void Phase38_Log(log_level_t lvl, const CHAR16 *msg)
{
    static const CHAR16 *prefix[] = {L"INFO: ", L"WARN: ", L"ERROR: "};
    for(const CHAR16 *p=prefix[lvl]; *p; ++p) serial_putc(*p);
    for(const CHAR16 *p=msg; *p; ++p) serial_putc(*p);
    serial_putc('\n');
}

/* Panic handling */
void Phase39_Panic(const CHAR16 *msg, uint64_t *rbp)
{
    Print(L"PANIC: %s\n", msg);
    while (rbp) {
        Print(L"%lx\n", rbp[1]);
        rbp = (uint64_t*)rbp[0];
    }
    while(1);
}

/* Virtual memory allocator using simple bitmap */
#define VMM_MAX_PAGES 512
static uint8_t vmm_bitmap[VMM_MAX_PAGES/8];
static uintptr_t vmm_base = 0xFFFF800000000000ULL;

void *Phase40_Valloc(UINTN pages)
{
    for(UINTN i=0;i<VMM_MAX_PAGES;i++) {
        bool free=true;
        for(UINTN j=0;j<pages;j++) {
            if (vmm_bitmap[(i+j)/8] & (1<<((i+j)%8))) { free=false; break; }
        }
        if (free) {
            for(UINTN j=0;j<pages;j++) vmm_bitmap[(i+j)/8] |= 1<<((i+j)%8);
            return (void*)(vmm_base + i*0x1000ULL);
        }
    }
    return NULL;
}

void Phase40_Vfree(void *ptr, UINTN pages)
{
    UINTN idx = ((uintptr_t)ptr - vmm_base)/0x1000ULL;
    for(UINTN j=0;j<pages;j++) vmm_bitmap[(idx+j)/8] &= ~(1<<((idx+j)%8));
}
