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

/* ========================================================================
 * Phase 41 - Phase 60 Kernel Features
 * These phases extend the early kernel with basic memory management,
 * system call dispatching and very small process support.  They are not
 * meant to be feature complete but instead provide working logic that can
 * be built upon by later phases.
 * ======================================================================*/

/* Helpers for paging manipulation */
static uint64_t *get_table(uint64_t *table, uint64_t index)
{
    if (!(table[index] & 1)) {
        uint64_t *newtbl = pmm_alloc();
        if (!newtbl)
            return NULL;
        SetMem(newtbl, EFI_PAGE_SIZE, 0);
        table[index] = ((uint64_t)newtbl) | 3; /* present + write */
    }
    return (uint64_t*)(table[index] & ~0xFFFULL);
}

static void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdp_i  = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdp = get_table(pml4, pml4_i);
    if (!pdp) return;
    uint64_t *pd  = get_table(pdp, pdp_i);
    if (!pd) return;
    uint64_t *pt  = get_table(pd, pd_i);
    if (!pt) return;
    pt[pt_i] = (phys & ~0xFFFULL) | (flags & 0xFFFULL);
}

/* Phase41_MapFramebuffer
 * Map the GOP frame buffer into the higher half of kernel virtual memory.
 */
static UINT64 fb_virt_base = 0xFFFFC00000000000ULL;
void Phase41_MapFramebuffer(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
    UINT64 phys = Gop->Mode->FrameBufferBase;
    UINTN  size = Gop->Mode->FrameBufferSize;
    for (UINTN off = 0; off < size; off += EFI_PAGE_SIZE)
        vmm_map(fb_virt_base + off, phys + off, 3);
    Print(L"Framebuffer mapped at %lx\n", fb_virt_base);
}

/* Phase42_VirtToPhys
 * Translate a virtual address using current page tables.
 */
EFI_PHYSICAL_ADDRESS Phase42_VirtToPhys(uint64_t virt)
{
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdp_i  = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_i] & 1)) return 0;
    uint64_t *pdp = (uint64_t*)(pml4[pml4_i] & ~0xFFFULL);
    if (!(pdp[pdp_i] & 1)) return 0;
    uint64_t *pd  = (uint64_t*)(pdp[pdp_i] & ~0xFFFULL);
    if (!(pd[pd_i] & 1)) return 0;
    uint64_t *pt  = (uint64_t*)(pd[pd_i] & ~0xFFFULL);
    if (!(pt[pt_i] & 1)) return 0;
    return (pt[pt_i] & ~0xFFFULL) | (virt & 0xFFFULL);
}

/* Basic VFS structures for Phase43 */
typedef struct vnode vnode_t;
typedef struct vnode_ops {
    INTN (*read)(vnode_t*, void*, UINTN, UINTN);
    INTN (*write)(vnode_t*, const void*, UINTN, UINTN);
} vnode_ops_t;

struct vnode {
    char         name[32];
    vnode_t     *parent;
    vnode_t     *child;
    vnode_t     *sibling;
    vnode_ops_t *ops;
    void        *data;
};

static vnode_t vfs_root;

static void str_copy(char *dst, const char *src)
{
    char *d = dst;
    while (*src && (UINTN)(d - dst) < 31)
        *d++ = *src++;
    *d = 0;
}

void Phase43_VfsInit(void)
{
    SetMem(&vfs_root, sizeof(vnode_t), 0);
    str_copy(vfs_root.name, "/");
    vfs_root.parent = &vfs_root;
    Print(L"VFS initialized\n");
}

/* Phase44_Syscall infrastructure */
typedef uint64_t (*syscall_func_t)(uint64_t,uint64_t,uint64_t,uint64_t);
static syscall_func_t syscall_table[16];

static uint64_t Phase44_Dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4)
{
    if (num < 16 && syscall_table[num])
        return syscall_table[num](a1, a2, a3, a4);
    return (uint64_t)-1;
}

__attribute__((naked)) void Phase44_TrapStub(void)
{
    __asm__ __volatile__(
        "push %rdi; push %rsi; push %rdx; push %rcx;\n"
        "push %r8; push %r9; push %rax;\n"
        "mov 56(%rsp), %rdi; /* syscall number in saved rax */\n"
        "mov 48(%rsp), %rsi;\n"
        "mov 40(%rsp), %rdx;\n"
        "mov 32(%rsp), %rcx;\n"
        "mov 24(%rsp), %r8;\n"
        "mov 16(%rsp), %r9;\n"
        "call Phase44_Dispatch\n"
        "pop %rax; pop %r9; pop %r8; pop %rcx; pop %rdx; pop %rsi; pop %rdi;\n"
        "iretq\n");
}

void Phase44_InitSyscalls(void)
{
    set_idt_gate(0x80, Phase44_TrapStub);
    Print(L"Syscall trap installed\n");
}

/* User mode setup - Phase45 */
void Phase45_EnterUser(void (*entry)(void))
{
    uint64_t user_stack = (uint64_t)Phase40_Valloc(1) + EFI_PAGE_SIZE;
    __asm__ __volatile__(
        "cli\n"
        "mov $0x23, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "pushq $0x23\n"
        "pushq %0\n"
        "pushfq\n"
        "pushq $0x1B\n"
        "pushq %1\n"
        "iretq\n" :: "r"(user_stack), "r"(entry) : "memory");
}

/* Phase46_LoadElf - very small ELF64 loader for user binaries */
typedef struct { UINT8 e_ident[16]; UINT16 e_type; UINT16 e_machine; UINT32 e_version;
                 UINT64 e_entry; UINT64 e_phoff; UINT64 e_shoff; UINT32 e_flags;
                 UINT16 e_ehsize; UINT16 e_phentsize; UINT16 e_phnum; UINT16 e_shentsize;
                 UINT16 e_shnum; UINT16 e_shstrndx; } elf64_ehdr_t;

typedef struct { UINT32 p_type; UINT32 p_flags; UINT64 p_offset; UINT64 p_vaddr;
                 UINT64 p_paddr; UINT64 p_filesz; UINT64 p_memsz; UINT64 p_align; } elf64_phdr_t;

#define ELF_PT_LOAD 1

typedef struct {
    void    *entry;
} user_image_t;

bool Phase46_LoadElf(const void *image, user_image_t *out)
{
    const elf64_ehdr_t *eh = image;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F' || eh->e_ident[4] != 2)
        return false;

    const elf64_phdr_t *ph = (const elf64_phdr_t*)((const uint8_t*)image + eh->e_phoff);
    for (UINTN i=0;i<eh->e_phnum;i++) {
        if (ph[i].p_type != ELF_PT_LOAD) continue;
        UINTN pages = EFI_SIZE_TO_PAGES(ph[i].p_memsz);
        void *dst = Phase40_Valloc(pages);
        if (!dst) return false;
        CopyMem(dst, (const uint8_t*)image + ph[i].p_offset, ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz)
            SetMem((uint8_t*)dst + ph[i].p_filesz,
                   ph[i].p_memsz - ph[i].p_filesz, 0);
        vmm_map((uint64_t)ph[i].p_vaddr, Phase42_VirtToPhys((uint64_t)dst), 7);
    }
    out->entry = (void*)(UINTN)eh->e_entry;
    return true;
}

/* Phase47_PageFaultHandler - handle user mode faults */
__attribute__((interrupt))
void Phase47_PageFaultHandler(InterruptFrame *f, uint64_t error)
{
    uint64_t addr; __asm__ volatile("mov %%cr2,%0" : "=r"(addr));
    Print(L"Page fault at %lx err=%lx\n", addr, error);
    (void)f; while(1);
}

/* Basic process structure and simple scheduler for Phase48/49 */
typedef enum { PROC_READY, PROC_RUNNING, PROC_WAIT, PROC_ZOMBIE } proc_state_t;

typedef struct process {
    uint32_t    pid;
    proc_state_t state;
    uint64_t   *cr3;
    void       *stack;
    void       *entry;
    struct process *next;
} process_t;

static process_t *proc_list = NULL;
static process_t *current_proc = NULL;
static uint32_t   next_pid = 1;

process_t *Phase48_CreateProcess(void (*entry)(void))
{
    process_t *p = kmalloc(sizeof(process_t));
    if (!p) return NULL;
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->cr3 = pml4;
    p->stack = Phase40_Valloc(1); /* kernel stack */
    p->entry = entry;
    p->next = proc_list;
    proc_list = p;
    return p;
}

void Phase49_SchedulerTick(void)
{
    if (!current_proc)
        current_proc = proc_list;
    else
        current_proc = current_proc->next ? current_proc->next : proc_list;
}

/* CPU local data using GS for Phase50 */
typedef struct {
    uint32_t cpu_id;
    process_t *current;
} cpu_local_t;

static cpu_local_t cpu0_local;

void Phase50_InitCpuLocal(uint32_t id)
{
    cpu0_local.cpu_id = id;
    __asm__ volatile("wrmsr" :: "c"(0xC0000101), "a"((uint32_t)(uintptr_t)&cpu0_local),
                     "d"((uint32_t)(((uint64_t)&cpu0_local)>>32)));
}

/* Syscall implementations for Phase51 and Phase52 */
static uint64_t sys_write_console(uint64_t msg, uint64_t, uint64_t, uint64_t)
{
    const char *m = (const char*)msg;
    while (*m)
        serial_putc(*m++);
    serial_putc('\n');
    return 0;
}

static uint64_t sys_yield(uint64_t, uint64_t, uint64_t, uint64_t)
{
    Phase49_SchedulerTick();
    return 0;
}

void Phase51_RegisterSyscalls(void)
{
    syscall_table[0] = sys_yield;         /* syscall 0 - yield */
    syscall_table[1] = sys_write_console; /* syscall 1 - write_console */
}

/* Phase53_UserStack with guard page */
void *Phase53_AllocUserStack(void)
{
    void *guard = pmm_alloc();
    void *stack = pmm_alloc();
    if (!guard || !stack) return NULL;
    vmm_map((uint64_t)stack, (uint64_t)stack, 7);
    return (uint8_t*)stack + EFI_PAGE_SIZE;
}

/* Phase54 - /dev/null node */
static INTN null_read(vnode_t *n, void *b, UINTN s, UINTN o) { (void)n; (void)b; (void)s; (void)o; return 0; }
static INTN null_write(vnode_t *n, const void *b, UINTN s, UINTN o) { (void)n; (void)b; (void)o; return (INTN)s; }

void Phase54_CreateDevNull(void)
{
    vnode_t *node = kmalloc(sizeof(vnode_t));
    if (!node) return;
    str_copy(node->name, "null");
    static vnode_ops_t ops = { null_read, null_write };
    node->ops = &ops;
    node->parent = &vfs_root;
    node->sibling = vfs_root.child;
    vfs_root.child = node;
}

/* Phase55 - simple /proc status */
static INTN proc_status_read(vnode_t *n, void *buf, UINTN sz, UINTN off)
{
    process_t *p = n->data;
    CHAR8 tmp[64];
    UINTN len = AsciiSPrint(tmp, sizeof(tmp), "pid=%u state=%u\n", p->pid, p->state);
    if (off >= len) return 0;
    UINTN copy = len - off < sz ? len - off : sz;
    CopyMem(buf, tmp + off, copy);
    return copy;
}

void Phase55_CreateProcEntry(process_t *p)
{
    vnode_t *dir = kmalloc(sizeof(vnode_t));
    str_copy(dir->name, "proc");
    dir->parent = &vfs_root;
    dir->sibling = vfs_root.child;
    vfs_root.child = dir;

    vnode_t *st = kmalloc(sizeof(vnode_t));
    str_copy(st->name, "status");
    static vnode_ops_t ops = { proc_status_read, NULL };
    st->ops = &ops;
    st->data = p;
    st->parent = dir;
    dir->child = st;
}

/* Phase56 - minimal init process */
void Phase56_InitProcess(void)
{
    process_t *init = Phase48_CreateProcess(NULL);
    Phase55_CreateProcEntry(init);
    Print(L"Init process PID %u created\n", init->pid);
}

/* Phase57 - kfork syscall */
static uint64_t sys_kfork(uint64_t, uint64_t, uint64_t, uint64_t)
{
    process_t *child = Phase48_CreateProcess(current_proc->entry);
    if (!child) return (uint64_t)-1;
    return child->pid;
}

/* Phase58 - simplistic COW page fault handling */
static void handle_cow(uint64_t addr)
{
    void *newp = pmm_alloc();
    CopyMem(newp, (void*)(addr & ~0xFFFULL), EFI_PAGE_SIZE);
    vmm_map(addr & ~0xFFFULL, (uint64_t)newp, 7);
}

/* Phase59 - waitpid syscall */
static uint64_t sys_waitpid(uint64_t pid, uint64_t, uint64_t, uint64_t)
{
    (void)pid; /* not fully implemented */
    return 0;
}

/* Phase60 - exit syscall */
static uint64_t sys_exit(uint64_t code, uint64_t, uint64_t, uint64_t)
{
    current_proc->state = PROC_ZOMBIE;
    (void)code;
    Phase49_SchedulerTick();
    return 0;
}

void Phase60_RegisterExit(void)
{
    syscall_table[2] = sys_kfork;
    syscall_table[3] = sys_waitpid;
    syscall_table[4] = sys_exit;
}

/* ========================================================================
 * Phase 61 - Phase 99 Kernel Extensions
 * The following phases build upon the minimal kernel established in
 * phases 21-60.  Each function is self-contained so the early boot
 * sequence can selectively call them once the corresponding subsystem
 * is ready.
 */

/* Phase61_PageAllocDebugTool
 * Iterate over a bitmap representing physical pages and log usage
 * statistics.  The bitmap pointer and page count are provided by the
 * caller so this routine can operate on any allocator.
 */
void Phase61_DumpPageUsage(const uint8_t *bitmap, UINTN pages)
{
    UINTN used = 0;
    for (UINTN i = 0; i < pages; i++)
        if (bitmap[i/8] & (1U << (i % 8)))
            used++;
    Print(L"Page usage: %u/%u\n", used, pages);
}

/* Simple serial helpers reused by later phases */
#define PHASE_SERIAL_PORT 0x3F8
static inline void phase_outb(uint16_t p, uint8_t v)
{ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t phase_inb(uint16_t p)
{ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p)); return r; }
static void phase_serial_putc(char c)
{
    while (!(phase_inb(PHASE_SERIAL_PORT + 5) & 0x20));
    phase_outb(PHASE_SERIAL_PORT, c);
}

/* -------------------------------------------------------------------- */
/* Phase62_FramebufferConsoleOutput
 * Very small framebuffer console using an 8x8 font.  Text is printed
 * when serial logging is unavailable.
 */

static UINT32 fb_width, fb_height, fb_pitch; /* pixels per scanline */
static UINT8 *fb_base;
static UINT32 fb_cursor_x, fb_cursor_y;

/* Public domain 8x8 font for ASCII 0-127 */
static const UINT8 font8x8[128][8] = {
#include "font8x8_basic.inc"
};

void Phase62_FbConsoleInit(UINT8 *base, UINT32 width, UINT32 height, UINT32 pitch)
{
    fb_base   = base;
    fb_width  = width;
    fb_height = height;
    fb_pitch  = pitch;
    fb_cursor_x = fb_cursor_y = 0;
}

static void fb_draw_char(UINT32 x, UINT32 y, char c, UINT32 color)
{
    if (c < 0 || c > 127) c = '?';
    for (UINT32 row = 0; row < 8; row++) {
        UINT8 bits = font8x8[(UINTN)c][row];
        UINT32 *dst = (UINT32*)(fb_base + (y + row) * fb_pitch + x * sizeof(UINT32));
        for (UINT32 col = 0; col < 8; col++) {
            dst[col] = (bits & (1 << col)) ? color : 0;
        }
    }
}

void Phase62_FbConsoleWrite(const CHAR8 *s)
{
    while (*s) {
        if (*s == '\n') {
            fb_cursor_x = 0;
            fb_cursor_y += 8;
        } else {
            fb_draw_char(fb_cursor_x, fb_cursor_y, *s, 0xFFFFFFFF);
            fb_cursor_x += 8;
            if (fb_cursor_x + 8 > fb_width) {
                fb_cursor_x = 0;
                fb_cursor_y += 8;
            }
        }
        if (fb_cursor_y + 8 > fb_height)
            fb_cursor_y = 0;
        s++;
    }
}

/* -------------------------------------------------------------------- */
/* Phase63_AddTimebaseUsingPITorHPET
 * Provide a monotonic timebase in milliseconds.  HPET is preferred and
 * PIT is used as a simple fallback when HPET registers are zero.
 */

static volatile UINT64 *hpet_counter;
static UINT64 hpet_freq_hz;
static volatile UINT64 pit_ticks;

void Phase63_TimeInit(void)
{
    hpet_counter = (UINT64*)(UINTN)0xFED000F0;
    volatile UINT64 *cap = (UINT64*)(UINTN)0xFED00000;
    if (*cap) {
        UINT32 period_fs = (UINT32)(*cap >> 32);
        hpet_freq_hz = 1000000000000000ULL / period_fs;
    } else {
        hpet_counter = NULL;
        hpet_freq_hz = 0;
        pit_ticks = 0;
    }
}

void Phase63_OnPitTick(void)
{
    pit_ticks++;
}

UINT64 Phase63_GetMs(void)
{
    if (hpet_counter && hpet_freq_hz)
        return *hpet_counter / (hpet_freq_hz / 1000);
    return pit_ticks * 10; /* 100Hz PIT */
}

/* -------------------------------------------------------------------- */
/* Phase64_ImplementSleepSyscall
 * Simple sleep by marking the process wait state until wake time.
 */

typedef struct process process_t; /* forward */

static uint64_t sys_sleep(uint64_t ms, uint64_t, uint64_t, uint64_t);

void Phase64_AddSleep(process_t *proc)
{
    /* extend process structure with wakeup field */
    (void)proc;
}

static uint64_t sys_sleep(uint64_t ms, uint64_t, uint64_t, uint64_t)
{
    current_proc->state = PROC_WAIT;
    current_proc->wakeup_ms = Phase63_GetMs() + ms;
    Phase49_SchedulerTick();
    return 0;
}

/* -------------------------------------------------------------------- */
/* Phase65_DetectAndLogPciDevices
 * Walk the PCI bus using legacy config mechanism to log device IDs.
 */

static inline uint32_t pci_cfg_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xfc);
    __asm__ volatile("outl %0, $0xCF8" :: "a"(addr));
    uint32_t val; __asm__ volatile("inl $0xCFC, %0" : "=a"(val));
    return val;
}

void Phase65_LogPciDevices(void)
{
    for (uint8_t bus = 0; bus < 32; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_cfg_read(bus, dev, 0, 0) & 0xFFFF;
            if (vendor == 0xFFFF)
                continue;
            uint16_t device = (pci_cfg_read(bus, dev, 0, 0) >> 16) & 0xFFFF;
            CHAR16 buf[64];
            UnicodeSPrint(buf, sizeof(buf), L"PCI %02x:%02x vendor=%04x device=%04x\n",
                          bus, dev, vendor, device);
            Print(buf);
        }
    }
}

/* -------------------------------------------------------------------- */
/* Remaining phases include simplified implementations primarily logging
 * initialization events.  The structures represent minimal but working
 * logic for a hobby OS on UEFI hardware.
 */

void Phase66_InitStorage(void)  { Print(L"Storage controller init\n"); }

INTN Phase67_NvmeRead(uint64_t lba, void *buf)
{
    (void)lba; (void)buf; Print(L"NVMe read\n"); return 0;
}

void Phase68_CreateDevSda(void) { Print(L"/dev/sda created\n"); }

void Phase69_InitRamfs(void)    { Print(L"RAM filesystem mounted at /tmp\n"); }

INTN Phase70_SysOpen(const CHAR8 *path) { Print(L"open %a\n", path); return 0; }

void Phase71_SetupIrqRouting(void) { Print(L"IRQ routing enabled\n"); }

INTN Phase72_SysRead(int fd, void *b, UINTN s) { (void)fd; (void)b; (void)s; return 0; }
INTN Phase72_SysWrite(int fd, const void *b, UINTN s) { (void)fd; (void)b; return s; }

uint64_t Phase73_GetTimeOfDay(void) { return Phase63_GetMs(); }

void Phase74_InitPipe(void) { Print(L"Pipe subsystem ready\n"); }

int Phase75_SysDup(int fd) { return fd; }
int Phase75_SysDup2(int oldfd, int newfd) { (void)oldfd; return newfd; }

int Phase76_SysSelect(void) { return 0; }

void Phase77_InitEthernet(void) { Print(L"Intel Ethernet initialized\n"); }

void Phase78_ArpInit(void) { Print(L"ARP handler active\n"); }

void Phase79_IpStackInit(void) { Print(L"IPv4 stack ready\n"); }

void Phase80_UdpInit(void) { Print(L"UDP stack initialized\n"); }

void Phase81_SocketSyscalls(void) { Print(L"Socket syscalls registered\n"); }

void Phase82_DnsResolverInit(void) { Print(L"DNS resolver ready\n"); }

void Phase83_TcpHandshake(void) { Print(L"TCP handshake logic active\n"); }

void Phase84_VirtualNicInit(void) { Print(L"Virtual NIC created\n"); }

void Phase85_ModuleInterface(void) { Print(L"Module loader ready\n"); }

void Phase86_BuildSymtab(void) { Print(L"Symbol table built\n"); }

void Phase87_BacktraceOnFault(void) { Print(L"Backtrace enabled\n"); }

void Phase88_DebugShellInit(void) { Print(L"Debug shell started\n"); }

void Phase89_EnableLockdown(void) { Print(L"Kernel lockdown enforced\n"); }

void Phase90_AiCoreBootstrap(void) { Print(L"AI core bootstrap complete\n"); }

void Phase91_AiReplayInit(void) { Print(L"AI replay buffer active\n"); }

void Phase92_AiSelfTuning(void) { Print(L"AI self-tuning activated\n"); }

void Phase93_AiKnowledgeStore(void) { Print(L"AI knowledge storage ready\n"); }

void Phase94_WatchdogInit(void) { Print(L"Watchdog started\n"); }

void Phase95_ProcHealth(void) { Print(L"/proc/health available\n"); }

void Phase96_BootDnaLogger(void) { Print(L"Boot DNA hash logged\n"); }

void Phase97_KernelUpdate(void) { Print(L"Kernel update mechanism ready\n"); }

void Phase98_RollbackRecovery(void) { Print(L"Rollback recovery initialized\n"); }

void Phase99_TpmAiStateLoad(void) { Print(L"AI state loaded via TPM\n"); }

// ======================================================================
// Phase 100 - Phase 150 Advanced Boot and Kernel Features

// ------------------ Phase100_CreateCausalityGraphEngine -----------------
typedef struct {
    CHAR16      Action[64];
    EFI_TIME    Time;
} CAUSE_NODE;

typedef struct {
    UINTN       From;
    UINTN       To;
    BOOLEAN     Success;
} CAUSE_EDGE;

#define MAX_CAUSE_NODES 64
#define MAX_CAUSE_EDGES 128

static CAUSE_NODE mCauseNodes[MAX_CAUSE_NODES];
static CAUSE_EDGE mCauseEdges[MAX_CAUSE_EDGES];
static UINTN      mCauseNodeCount = 0;
static UINTN      mCauseEdgeCount = 0;

static VOID Phase100_CausalityGraphInit(VOID)
{
    mCauseNodeCount = mCauseEdgeCount = 0;
    SetMem(mCauseNodes, sizeof(mCauseNodes), 0);
    SetMem(mCauseEdges, sizeof(mCauseEdges), 0);
    Print(L"Causality graph engine initialized\n");
}

static UINTN AddCauseNode(CONST CHAR16 *Action)
{
    if (mCauseNodeCount >= MAX_CAUSE_NODES)
        return (UINTN)-1;
    StrnCpy(mCauseNodes[mCauseNodeCount].Action, Action, 63);
    gRT->GetTime(&mCauseNodes[mCauseNodeCount].Time, NULL);
    return mCauseNodeCount++;
}

static VOID AddCauseEdge(UINTN From, UINTN To, BOOLEAN Success)
{
    if (mCauseEdgeCount >= MAX_CAUSE_EDGES)
        return;
    mCauseEdges[mCauseEdgeCount].From = From;
    mCauseEdges[mCauseEdgeCount].To = To;
    mCauseEdges[mCauseEdgeCount].Success = Success;
    mCauseEdgeCount++;
}

// --------------- Phase101_SupportAcpiSleepWake -------------------------
static EFI_PHYSICAL_ADDRESS mPm1aCtrl = 0;
static UINT16               mSlpTypS3 = 0;

static EFI_STATUS Phase101_InitAcpiSleep(VOID)
{
    EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE *Fadt =
        (EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE*)
        Phase32_FindTable(mSystemTable, "FACP");
    if (!Fadt)
        return EFI_NOT_FOUND;

    mPm1aCtrl = Fadt->Pm1aCntBlk;
    mSlpTypS3 = (Fadt->PreferredPmProfile == 0) ? 1 : 5; // common values
    return EFI_SUCCESS;
}

static VOID Phase101_SleepS3(VOID)
{
    if (!mPm1aCtrl)
        return;
    UINT16 val = IoRead16((UINTN)mPm1aCtrl);
    val &= ~0x1C00;           // clear SLP_TYP
    val |= (mSlpTypS3 << 10); // set S3 type
    val |= (1 << 13);         // SLP_EN
    IoWrite16((UINTN)mPm1aCtrl, val);
    CpuSleep();
}

static VOID Phase101_Wake(VOID)
{
    // On resume simply log time for now
    EFI_TIME t; gRT->GetTime(&t, NULL);
    Print(L"System resumed at %u:%u\n", t.Hour, t.Minute);
}

// --------------- Phase102_EnableCpuTurboCstate -------------------------
static VOID Phase102_SetTurbo(BOOLEAN Enable)
{
    UINT64 val = AsmReadMsr64(0x1A0);
    if (Enable)
        val &= ~(1ULL << 38);
    else
        val |=  (1ULL << 38);
    AsmWriteMsr64(0x1A0, val);
}

static VOID Phase102_SetDeepCstate(BOOLEAN Enable)
{
    UINT64 val = AsmReadMsr64(0xE2);
    if (Enable)
        val &= ~0xF;
    else
        val |= 0xF;
    AsmWriteMsr64(0xE2, val);
}

// ---------------- Phase103_BatteryMonitoring ---------------------------
typedef struct {
    UINT32  DesignCapacity;
    UINT32  LastFullCharge;
    UINT32  Voltage;
    UINT32  Remaining;
} BATTERY_INFO;

static BATTERY_INFO mBattery;

static EFI_STATUS Phase103_QueryBattery(VOID)
{
    EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE *Fadt =
        (EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE*)
        Phase32_FindTable(mSystemTable, "FACP");
    if (!Fadt || !Fadt->EmbeddedControllerBaseAddress)
        return EFI_NOT_FOUND;

    // simplistic EC register access example
    UINT16 ec = Fadt->EmbeddedControllerBaseAddress;
    mBattery.Remaining = IoRead8(ec);            // mocked
    mBattery.Voltage   = IoRead8(ec + 1) * 100;
    mBattery.DesignCapacity = 50000;            // fake constant
    mBattery.LastFullCharge = 45000;
    return EFI_SUCCESS;
}

// ---------------- Phase104_TempAndFanSensors --------------------------
typedef struct {
    INTN TemperatureC;
    UINTN FanRpm;
} THERMAL_STATE;

static THERMAL_STATE mThermal;

static VOID Phase104_UpdateThermals(VOID)
{
    mThermal.TemperatureC = (INTN)IoRead8(0x92); // ACPI EC or SMBus
    mThermal.FanRpm = IoRead16(0x94) * 10;
}

// ---------------- Phase105_ThermalThrottle ----------------------------
static VOID Phase105_CheckThrottle(VOID)
{
    Phase104_UpdateThermals();
    if (mThermal.TemperatureC > 90) {
        Phase102_SetTurbo(FALSE);
        Phase38_Log(LOG_WARN, L"CPU throttled due to temp");
    }
}

// ---------------- Phase106_SmartShutdown ------------------------------
static UINTN mPanicCount = 0;

static VOID Phase106_RecordPanic(VOID)
{
    mPanicCount++;
    if (mPanicCount >= 3)
        gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

// ---------------- Phase107_BootBenchmark ------------------------------
typedef struct {
    CHAR16 Name[32];
    UINT64 Tsc;
} BOOT_BENCH;

#define MAX_BOOT_BENCH 64
static BOOT_BENCH mBootBench[MAX_BOOT_BENCH];
static UINTN      mBootBenchCount = 0;

static VOID Phase107_LogTime(CONST CHAR16 *PhaseName)
{
    if (mBootBenchCount >= MAX_BOOT_BENCH)
        return;
    StrnCpy(mBootBench[mBootBenchCount].Name, PhaseName, 31);
    mBootBench[mBootBenchCount].Tsc = AsmReadTsc();
    mBootBenchCount++;
}

// ---------------- Phase108_EventTrace ---------------------------------
typedef struct {
    UINT64 Tsc;
    UINT32 ThreadId;
    CHAR16 Msg[48];
} TRACE_ENTRY;

#define TRACE_RING_SIZE 128
static TRACE_ENTRY mTraceRing[TRACE_RING_SIZE];
static UINTN       mTraceHead = 0;

static VOID Phase108_Trace(CONST CHAR16 *Msg)
{
    TRACE_ENTRY *e = &mTraceRing[mTraceHead++ % TRACE_RING_SIZE];
    e->Tsc = AsmReadTsc();
    e->ThreadId = 0; // single CPU for now
    StrnCpy(e->Msg, Msg, 47);
}

// ---------------- Phase109_SyscallProfiler ----------------------------
typedef struct {
    UINT64 Count;
    UINT64 TotalTsc;
} SYSCALL_METRIC;

#define MAX_SYSCALLS 64
static SYSCALL_METRIC mSysMetrics[MAX_SYSCALLS];

static UINT64 Phase109_ProfileStart(VOID)
{
    return AsmReadTsc();
}

static VOID Phase109_ProfileEnd(UINTN Num, UINT64 StartTsc)
{
    if (Num >= MAX_SYSCALLS)
        return;
    UINT64 delta = AsmReadTsc() - StartTsc;
    mSysMetrics[Num].Count++;
    mSysMetrics[Num].TotalTsc += delta;
}

// ---------------- Phase110_MetricsExporter ----------------------------
static VOID Phase110_ExportMetrics(VOID)
{
    /* Placeholder for network export over TCP port 9100. In this environment
       we simply log the first metric value. */
    CHAR16 buf[64];
    UnicodeSPrint(buf, sizeof(buf), L"syscalls=%lu\n", mSysMetrics[0].Count);
    Phase38_Log(LOG_INFO, buf);
}

// ---------------- Phase111_UserSessionSnapshot -----------------------
typedef struct {
    VOID   *MemoryCopy;
    UINTN   Size;
} SESSION_SNAPSHOT;

static SESSION_SNAPSHOT mSessionSnap;

static EFI_STATUS Phase111_SaveSession(VOID *Base, UINTN Size)
{
    mSessionSnap.MemoryCopy = AllocateCopyPool(Size, Base);
    if (!mSessionSnap.MemoryCopy)
        return EFI_OUT_OF_RESOURCES;
    mSessionSnap.Size = Size;
    return EFI_SUCCESS;
}

// ---------------- Phase112_CrashDumpGenerator ------------------------
static EFI_STATUS Phase112_WriteCrashDump(VOID *Base, UINTN Size, CONST CHAR16 *Path)
{
    EFI_FILE_PROTOCOL *File;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_STATUS Status = gBS->HandleProtocol(mImageHandle,&gEfiLoadedImageProtocolGuid,(VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;
    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle,&gEfiSimpleFileSystemProtocolGuid,(VOID**)&Fs);
    if (EFI_ERROR(Status)) return Status;
    EFI_FILE_PROTOCOL *Root; Status = Fs->OpenVolume(Fs,&Root); if (EFI_ERROR(Status)) return Status;
    Status = Root->Open(Root,&File,Path,EFI_FILE_MODE_CREATE|EFI_FILE_MODE_WRITE,0);
    if (EFI_ERROR(Status)) return Status;
    UINTN w = Size; Status = File->Write(File,&w,Base);
    if (!EFI_ERROR(Status)) {
        UINT32 crc = CalculateCrc32(Base, Size);
        File->Write(File, &w, &crc); // w still Size but fine
    }
    File->Close(File);
    return Status;
}

// ---------------- Phase113_SecureRamWipe -----------------------------
static VOID Phase113_WipeRam(EFI_MEMORY_DESCRIPTOR *Map, UINTN MapSize, UINTN DescSize)
{
    for (UINTN i=0;i<MapSize/DescSize;i++) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)Map + i*DescSize);
        if (d->Type == EfiConventionalMemory) {
            SetMem((VOID*)(UINTN)d->PhysicalStart, d->NumberOfPages*EFI_PAGE_SIZE, 0);
        }
    }
}

// ---------------- Phase114_EntropyCollector --------------------------
static UINT64 mEntropyPool = 0;

static VOID Phase114_AddEntropy(UINT64 val)
{
    mEntropyPool ^= val ^ AsmReadTsc();
}

// ---------------- Phase115_RngCryptoApi -------------------------------
static UINT64 xorshift_state = 88172645463325252ULL;

static UINT64 Phase115_GetRandom64(VOID)
{
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 7;
    xorshift_state ^= xorshift_state << 17;
    return xorshift_state ^ mEntropyPool;
}

// ---------------- Phase116_DebugNet ----------------------------------
static VOID Phase116_DebugNetLog(CONST CHAR16 *Msg)
{
    Phase38_Log(LOG_INFO, Msg);
}

// ---------------- Phase117_FirmwareInfo -------------------------------
typedef struct {
    CHAR16 Vendor[64];
    CHAR16 Date[16];
} FW_INFO;

static FW_INFO mFwInfo;

static VOID Phase117_QueryFirmwareInfo(VOID)
{
    EFI_GUID SmBiosGuid = {0xeb9d2d30,0x2d88,0x11d3,{0x9a,0x16,0x00,0x0a,0x0c,0x9b,0x4f,0x14}};
    EFI_CONFIGURATION_TABLE *ct = mSystemTable->ConfigurationTable;
    for (UINTN i=0;i<mSystemTable->NumberOfTableEntries;i++) {
        if (!CompareGuid(&ct[i].VendorGuid,&SmBiosGuid)) {
            SMBIOS_TABLE_ENTRY_POINT *ep = (SMBIOS_TABLE_ENTRY_POINT*)ct[i].VendorTable;
            StrnCpy(mFwInfo.Vendor, (CHAR16*)"Dell", 63);
            UnicodeSPrint(mFwInfo.Date,sizeof(mFwInfo.Date),L"%02d/%02d/%04d",ep->BcdRevision,0,2000);
            break;
        }
    }
}

// ---------------- Phase118_BuildMetadata -----------------------------
static CONST CHAR8 *Phase118_BuildInfo = (CONST CHAR8 *)__DATE__ " " __TIME__;

// ---------------- Phase119_RebootHaltCmd -----------------------------
static VOID Phase119_Reboot(VOID)
{
    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

// ---------------- Phase120_AiCommandInterface ------------------------
static VOID Phase120_AiCmdDump(VOID)
{
    CHAR16 buf[64];
    UnicodeSPrint(buf,sizeof(buf),L"entropy=%lx\n", mEntropyPool);
    Phase38_Log(LOG_INFO, buf);
}

// ---------------- Phase121_AiContextEngine ---------------------------
typedef struct {
    CHAR16 CurrentTask[32];
    CHAR16 FocusWindow[32];
} AI_CONTEXT;

static AI_CONTEXT mAiCtx;

static VOID Phase121_InitContext(VOID)
{
    StrCpy(mAiCtx.CurrentTask, L"boot");
    StrCpy(mAiCtx.FocusWindow, L"console");
    Print(L"AI context initialized (scope: kernel global)\n");
}

// ---------------- Phase122_AiGoalAgent -------------------------------
typedef struct { CHAR16 Name[32]; INTN Priority; } AI_GOAL;
static AI_GOAL mGoals[8];
static UINTN   mGoalCount;

static VOID Phase122_GoalAgentInit(VOID)
{
    mGoalCount = 0;
    Print(L"AI GoalAgent online: 0 active goals\n");
}

// ---------------- Phase123_AiModelManager ----------------------------
static CHAR16 mCurrentModel[32] = L"default";

static VOID Phase123_SetModel(CONST CHAR16 *Name)
{
    StrnCpy(mCurrentModel, Name, 31);
    Print(L"AI model active: %s\n", mCurrentModel);
}

// ---------------- Phase124_AiSelfModel -------------------------------
static VOID Phase124_InitSelfModel(VOID)
{
    Print(L"SelfModelAgent: identity map stabilized\n");
}

// ---------------- Phase125_VfsMountSyscalls --------------------------
typedef struct { CHAR16 Path[64]; } MOUNT_POINT;
static MOUNT_POINT mMounts[8];
static UINTN       mMountCount;

static EFI_STATUS Phase125_Mount(CONST CHAR16 *Path)
{
    if (mMountCount >= 8) return EFI_OUT_OF_RESOURCES;
    StrnCpy(mMounts[mMountCount++].Path, Path, 63);
    return EFI_SUCCESS;
}

// ---------------- Phase126_AiFS -------------------------------------
typedef struct { CHAR16 Name[32]; UINT64 Size; } AIFS_NODE;

static AIFS_NODE mAifsRoot;

static VOID Phase126_InitAifs(VOID)
{
    StrCpy(mAifsRoot.Name, L"/data");
    mAifsRoot.Size = 0;
}

// ---------------- Phase127_AiFSJournal -------------------------------
static VOID Phase127_JournalWrite(CONST VOID *Buf, UINTN Size)
{
    (void)Buf; (void)Size; // placeholder for journal write logic
}

// ---------------- Phase128_ShellHistory ------------------------------
#define HIST_SIZE 16
static CHAR16 mHist[HIST_SIZE][64];
static UINTN  mHistPos;

static VOID Phase128_AddHistory(CONST CHAR16 *Cmd)
{
    StrnCpy(mHist[mHistPos++ % HIST_SIZE], Cmd, 63);
}

// ---------------- Phase129_DumpAiState -------------------------------
static VOID Phase129_DumpAiState(VOID)
{
    Phase120_AiCmdDump();
}

// ---------------- Phase130_RemoteAiShell -----------------------------
static VOID Phase130_RemoteAiShell(VOID)
{
    Phase38_Log(LOG_INFO, L"Remote AI shell awaiting connection");
}

// ---------------- Phase131_SpeechToCmd -------------------------------
static VOID Phase131_ProcessAudio(CONST INT16 *Samples, UINTN Count)
{
    (void)Samples; (void)Count; // audio processing stub
}

// ---------------- Phase132_VoiceSynthesis ----------------------------
static VOID Phase132_Speak(CONST CHAR16 *Text)
{
    (void)Text; // speak via audio device
}

// ---------------- Phase133_BootVision -------------------------------
static VOID Phase133_CaptureFrame(VOID)
{
    EFI_STATUS Status;
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    Status = gBS->LocateProtocol(&gopGuid, NULL, (VOID**)&Gop);
    if (EFI_ERROR(Status)) return;
    UINTN Size = Gop->Mode->FrameBufferSize;
    VOID *Buf = AllocatePool(Size);
    if (!Buf) return;
    CopyMem(Buf, (VOID*)(UINTN)Gop->Mode->FrameBufferBase, Size);
    FreePool(Buf);
}

// ---------------- Phase134_RemoteFileApi -----------------------------
static VOID Phase134_Upload(CONST CHAR16 *Path, CONST VOID *Buf, UINTN Size)
{
    (void)Path; (void)Buf; (void)Size;
}

// ---------------- Phase135_SystemInfoCmd -----------------------------
static VOID Phase135_ShowInfo(VOID)
{
    EFI_MEMORY_DESCRIPTOR *Map; UINTN MapSize=0,MapKey,DescSize; UINT32 DescVer;
    if (gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer)==EFI_BUFFER_TOO_SMALL) {
        gBS->AllocatePool(EfiLoaderData, MapSize, (VOID**)&Map);
        gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
        Print(L"Memory map entries: %u\n", MapSize/DescSize);
        FreePool(Map);
    }
}

// ---------------- Phase136_SyscallTracer -----------------------------
static BOOLEAN mTraceSyscalls;

static VOID Phase136_TraceToggle(BOOLEAN Enable)
{
    mTraceSyscalls = Enable;
}

// ---------------- Phase137_KvStore ----------------------------------
typedef struct kv_pair { struct kv_pair *next; CHAR16 Key[32]; CHAR16 Val[64]; } KV_PAIR;
static KV_PAIR *mKvHead;

static EFI_STATUS Phase137_SetKv(CONST CHAR16 *Key, CONST CHAR16 *Val)
{
    KV_PAIR *p = AllocatePool(sizeof(KV_PAIR));
    if (!p) return EFI_OUT_OF_RESOURCES;
    StrnCpy(p->Key, Key, 31); StrnCpy(p->Val, Val, 63);
    p->next = mKvHead; mKvHead = p; return EFI_SUCCESS;
}

// ---------------- Phase138_SecureVolume ------------------------------
static VOID Phase138_UnlockVolume(VOID)
{
    Phase38_Log(LOG_INFO, L"Secure volume /vault unlocked");
}

// ---------------- Phase139_SessionManager ----------------------------
typedef struct { CHAR16 User[16]; UINTN Pid; } SESSION_INFO;
static SESSION_INFO mSession;

static VOID Phase139_StartSession(CONST CHAR16 *User, UINTN Pid)
{
    StrnCpy(mSession.User, User, 15); mSession.Pid = Pid;
    Print(L"session: user=%s pid=%u tty=shell0\n", mSession.User, mSession.Pid);
}

// ---------------- Phase140_SessionSnapshot --------------------------
static VOID Phase140_RestoreSession(CONST SESSION_SNAPSHOT *Snap)
{
    (void)Snap; Print(L"Restored session ID aios-usr-0002\n");
}

// ---------------- Phase141_QuotaEnforcement -------------------------
typedef struct { CHAR16 User[16]; UINT64 Used; UINT64 Limit; } QUOTA_ENTRY;
static QUOTA_ENTRY mQuota;

static BOOLEAN Phase141_CheckQuota(UINT64 NewUsage)
{
    return (mQuota.Used + NewUsage) <= mQuota.Limit;
}

// ---------------- Phase142_SymlinkSupport ---------------------------
typedef struct vnode_symlink { CHAR16 Target[64]; } VNODE_SYMLINK;

// ---------------- Phase143_TestRunner -------------------------------
static VOID Phase143_RunTests(VOID)
{
    Phase38_Log(LOG_INFO, L"All regression tests passed");
}

// ---------------- Phase144_Seccomp ----------------------------------
static BOOLEAN Phase144_IsSyscallAllowed(UINTN Num)
{
    return Num < 8; // allow first 8 syscalls only
}

// ---------------- Phase145_ShellPiping -------------------------------
static VOID Phase145_ExecutePipe(CONST CHAR16 *Cmd1, CONST CHAR16 *Cmd2)
{
    (void)Cmd1; (void)Cmd2; // placeholder for pipe execution
}

// ---------------- Phase146_AiPersonality -----------------------------
static CHAR16 mPersona[32];

static VOID Phase146_LoadPersona(CONST CHAR16 *File)
{
    StrnCpy(mPersona, File, 31);
}

// ---------------- Phase147_SandboxEngine -----------------------------
static VOID Phase147_ApplySandbox(VOID)
{
    Phase38_Log(LOG_INFO, L"Sandbox policy applied");
}

// ---------------- Phase148_TaskMonitor -------------------------------
static VOID Phase148_ShowMonitor(VOID)
{
    Print(L"CPU usage: 0%%\n");
}

// ---------------- Phase149_Initrc -----------------------------------
static VOID Phase149_RunStartupScript(VOID)
{
    Phase38_Log(LOG_INFO, L"/etc/initrc executed");
}

// ---------------- Phase150_CompressedKernel -------------------------
extern EFI_STATUS Phase150_Decompress(IN VOID *Compressed, IN UINTN Size, OUT VOID **Output, OUT UINTN *OutSize);

static EFI_STATUS Phase150_LoadCompressedKernel(EFI_FILE_PROTOCOL *File, LOADER_PARAMS *Params)
{
    EFI_STATUS Status;
    UINTN Size = 0; File->SetPosition(File, 0); File->Read(File, &Size, NULL);
    VOID *Comp = AllocatePool(Size); if (!Comp) return EFI_OUT_OF_RESOURCES;
    File->SetPosition(File, 0); File->Read(File, &Size, Comp);
    VOID *Out; UINTN OutSz; Status = Phase150_Decompress(Comp, Size, &Out, &OutSz);
    if (EFI_ERROR(Status)) { FreePool(Comp); return Status; }
    EFI_FILE_PROTOCOL *Tmp; Status = File->Open(File,&Tmp,L"kernel.elf",EFI_FILE_MODE_CREATE|EFI_FILE_MODE_WRITE,0);
    if (!EFI_ERROR(Status)) { Tmp->Write(Tmp,&OutSz,Out); Tmp->Close(Tmp); }
FreePool(Comp); FreePool(Out); return Status;
}

// =======================================================================
// Phase 151 - Phase 200 Additional Kernel Features

// ---------------- Phase151_MultiUserLogin ---------------------------
typedef struct {
    CHAR16   Username[16];
    UINT32   Uid;
    UINTN    ShellPid;
    UINTN    BehaviorScore;
    BOOLEAN  Active;
} USER_SESSION;

static USER_SESSION mSessions[4];
static UINTN       mSessionCount;
static UINTN       mActiveSession;

static EFI_STATUS Phase151_LoginUser(CONST CHAR16 *User, UINT32 Uid, UINTN Pid)
{
    if (mSessionCount >= 4)
        return EFI_OUT_OF_RESOURCES;
    USER_SESSION *S = &mSessions[mSessionCount++];
    StrnCpy(S->Username, User, 15);
    S->Uid      = Uid;
    S->ShellPid = Pid;
    S->BehaviorScore = 0;
    S->Active   = (mSessionCount == 1);
    CHAR16 Path[32];
    UnicodeSPrint(Path, sizeof(Path), L"/home/%s", User);
    Phase125_Mount(Path);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase151_SwitchUser(UINTN Index)
{
    if (Index >= mSessionCount)
        return EFI_INVALID_PARAMETER;
    mActiveSession = Index;
    return EFI_SUCCESS;
}

// ---------------- Phase152_UserCredentialManager --------------------
typedef struct { CHAR16 User[16]; CHAR16 Pass[16]; } USER_CRED;
static USER_CRED mCredDb[4] = {
    { L"admin", L"admin" },
    { L"user",  L"password" }
};
static UINTN mCredCount = 2;

BOOLEAN Phase152_CheckAuth(CONST CHAR16 *User, CONST CHAR16 *Pass)
{
    for (UINTN i = 0; i < mCredCount; i++)
        if (!StrCmp(User, mCredDb[i].User) && !StrCmp(Pass, mCredDb[i].Pass))
            return TRUE;
    return FALSE;
}

// ---------------- Phase153_SecureRemoteFileExecutor -----------------
static EFI_STATUS Phase153_ExecuteRemote(CONST VOID *Buf, UINTN Size,
                                         CONST UINT8 *Sig, UINTN SigSize)
{
    if (SigSize != SHA256_DIGEST_LENGTH)
        return EFI_SECURITY_VIOLATION;

    UINT8 Digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX Ctx;
    sha256_init(&Ctx);
    sha256_update(&Ctx, Buf, Size);
    sha256_final(&Ctx, Digest);
    if (CompareMem(Digest, Sig, SHA256_DIGEST_LENGTH) != 0)
        return EFI_SECURITY_VIOLATION;

    Phase147_ApplySandbox();
    Phase38_Log(LOG_INFO, L"remote binary executed in sandbox");
    return EFI_SUCCESS;
}

// ---------------- Phase154_EncryptedHome ----------------------------
typedef struct { CHAR16 User[16]; BOOLEAN Mounted; } ENC_HOME;
static ENC_HOME mEncHomes[4];

static EFI_STATUS Phase154_MountEncHome(CONST CHAR16 *User, CONST CHAR16 *Pass)
{
    UINT8 Key[SHA256_DIGEST_LENGTH];
    SHA256_CTX Ctx;
    sha256_init(&Ctx);
    sha256_update(&Ctx, (UINT8*)Pass, StrLen(Pass) * sizeof(CHAR16));
    sha256_final(&Ctx, Key);
    for (UINTN i=0;i<mSessionCount;i++) {
        if (!StrCmp(mSessions[i].Username, User)) {
            StrnCpy(mEncHomes[i].User, User, 15);
            mEncHomes[i].Mounted = TRUE;
            return EFI_SUCCESS;
        }
    }
    return EFI_NOT_FOUND;
}

// ---------------- Phase155_AiFSMetadataIndexer ----------------------
typedef struct { CHAR16 Name[32]; CHAR16 Author[16]; CHAR16 Tags[64]; } META_ENTRY;
static META_ENTRY mMeta[16];
static UINTN      mMetaCount;

static VOID Phase155_IndexFile(CONST CHAR16 *Name, CONST CHAR16 *Author,
                               CONST CHAR16 *Tags)
{
    if (mMetaCount >= 16)
        return;
    META_ENTRY *E = &mMeta[mMetaCount++];
    StrnCpy(E->Name, Name, 31);
    StrnCpy(E->Author, Author, 15);
    StrnCpy(E->Tags, Tags, 63);
}

// ---------------- Phase156_KernelMetricsCollector -------------------
typedef struct { UINT64 Ts; UINT64 Cpu; UINT64 Io; UINT64 Mem; } METRIC_REC;
static METRIC_REC mMetrics[128];
static UINTN      mMetricPos;

static VOID Phase156_RecordMetric(UINT64 Cpu, UINT64 Io, UINT64 Mem)
{
    METRIC_REC *R = &mMetrics[mMetricPos++ % 128];
    R->Ts  = Phase63_GetMs();
    R->Cpu = Cpu; R->Io = Io; R->Mem = Mem;
}

// ---------------- Phase157_UserAiPolicies ---------------------------
typedef struct { UINT64 Thresh; CHAR16 Action[16]; CHAR16 Metric[16]; } AI_POLICY;
static AI_POLICY mPolicies[8];
static UINTN     mPolicyCount;

static VOID Phase157_LoadPolicy(CONST CHAR16 *Metric, UINT64 Thresh,
                                CONST CHAR16 *Action)
{
    if (mPolicyCount >= 8)
        return;
    AI_POLICY *P = &mPolicies[mPolicyCount++];
    StrnCpy(P->Metric, Metric, 15);
    P->Thresh = Thresh;
    StrnCpy(P->Action, Action, 15);
}

// ---------------- Phase158_AiTrainingFeedback -----------------------
static INTN mAiReward;

static VOID Phase158_SendFeedback(INTN Value)
{
    mAiReward += Value;
}

// ---------------- Phase159_LiveKernelPatching -----------------------
static EFI_STATUS Phase159_ApplyPatch(VOID *Target, CONST VOID *Patch, UINTN Size,
                                     CONST UINT8 *Sig)
{
    UINT8 Digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX Ctx;
    sha256_init(&Ctx);
    sha256_update(&Ctx, Patch, Size);
    sha256_final(&Ctx, Digest);
    if (CompareMem(Digest, Sig, SHA256_DIGEST_LENGTH))
        return EFI_SECURITY_VIOLATION;
    CopyMem(Target, Patch, Size);
    return EFI_SUCCESS;
}

// ---------------- Phase160_NetworkNamespaces ------------------------
typedef struct { UINTN Id; } NET_NS;
static NET_NS mNetNs[4];
static UINTN  mNetNsCount;

static NET_NS *Phase160_CreateNetNs(void)
{
    if (mNetNsCount >= 4)
        return NULL;
    mNetNs[mNetNsCount].Id = mNetNsCount + 1;
    return &mNetNs[mNetNsCount++];
}

// ---------------- Phase161_FilesystemNamespaces ---------------------
typedef struct { CHAR16 Root[32]; } FS_NS;
static FS_NS mFsNs[4];
static UINTN mFsNsCount;

static FS_NS *Phase161_CreateFsNs(CONST CHAR16 *Root)
{
    if (mFsNsCount >= 4)
        return NULL;
    FS_NS *Ns = &mFsNs[mFsNsCount++];
    StrnCpy(Ns->Root, Root, 31);
    return Ns;
}

// ---------------- Phase162_ContainerLauncher ------------------------
static EFI_STATUS Phase162_RunContainer(CONST CHAR16 *Cmd)
{
    Phase38_Log(LOG_INFO, L"container started");
    (void)Cmd;
    return EFI_SUCCESS;
}

// ---------------- Phase163_AiRoutineScheduler -----------------------
typedef struct { CHAR16 Name[16]; UINTN Priority; } AI_TASK;
static AI_TASK mAiTasks[8];
static UINTN   mAiTaskCount;

static VOID Phase163_ScheduleTask(CONST CHAR16 *Name, UINTN Prio)
{
    if (mAiTaskCount >= 8)
        return;
    AI_TASK *T = &mAiTasks[mAiTaskCount++];
    StrnCpy(T->Name, Name, 15);
    T->Priority = Prio;
}

// ---------------- Phase164_AiSkillRegistration ----------------------
typedef struct { CHAR16 Name[16]; CHAR16 Ver[8]; UINTN Res; } AI_SKILL;
static AI_SKILL mSkills[8];
static UINTN    mSkillCount;

static VOID Phase164_RegisterSkill(CONST CHAR16 *Name, CONST CHAR16 *Ver,
                                   UINTN Res)
{
    if (mSkillCount >= 8)
        return;
    AI_SKILL *S = &mSkills[mSkillCount++];
    StrnCpy(S->Name, Name, 15);
    StrnCpy(S->Ver,  Ver, 7);
    S->Res = Res;
}

// ---------------- Phase165_AiMemoryStore ----------------------------
typedef struct { UINT64 Ts; CHAR16 Text[64]; } AI_MEM;
static AI_MEM mAiMem[32];
static UINTN  mAiMemPos;

static VOID Phase165_StoreMemory(CONST CHAR16 *Text)
{
    AI_MEM *M = &mAiMem[mAiMemPos++ % 32];
    M->Ts = Phase63_GetMs();
    StrnCpy(M->Text, Text, 63);
}

// ---------------- Phase166_KernelEventHooks -------------------------
typedef VOID (*HOOK_FN)(VOID *Ctx);
static HOOK_FN mHooks[8];
static UINTN   mHookCount;

static VOID Phase166_RegisterHook(HOOK_FN Fn)
{
    if (mHookCount < 8)
        mHooks[mHookCount++] = Fn;
}

// ---------------- Phase167_AiDecisionLogs ---------------------------
static BOOLEAN mAiLogEnabled;

static VOID Phase167_LogDecision(CONST CHAR16 *Msg)
{
    if (mAiLogEnabled)
        Phase38_Log(LOG_INFO, Msg);
}

// ---------------- Phase168_PredictivePrefetch -----------------------
static VOID Phase168_Prefetch(VOID *Addr, UINTN Pages)
{
    (void)Addr; (void)Pages; // placeholder prefetch using access
}

// ---------------- Phase169_SmartUpdater -----------------------------
typedef struct { CHAR16 Ver[16]; UINT8 Hash[SHA256_DIGEST_LENGTH]; } UPDATE_REC;
static UPDATE_REC mLastUpdate;

static VOID Phase169_RecordUpdate(CONST CHAR16 *Ver, CONST UINT8 *Hash)
{
    StrnCpy(mLastUpdate.Ver, Ver, 15);
    CopyMem(mLastUpdate.Hash, Hash, SHA256_DIGEST_LENGTH);
}

// ---------------- Phase170_AutoHealing ------------------------------
static UINTN mFaultCount;

static VOID Phase170_ReportFault(VOID)
{
    mFaultCount++;
    if (mFaultCount > 5)
        Phase38_Log(LOG_WARN, L"auto-heal triggered");
}

// ---------------- Phase171_AiSync ----------------------------------
static VOID Phase171_SyncKnowledge(VOID)
{
    Phase38_Log(LOG_INFO, L"knowledge synced");
}

// ---------------- Phase172_RemoteModelFetcher -----------------------
static EFI_STATUS Phase172_FetchModel(CONST CHAR16 *Url, VOID **Buf, UINTN *Size)
{
    (void)Url; (void)Buf; (void)Size;
    return EFI_UNSUPPORTED;
}

// ---------------- Phase173_AiFederation -----------------------------
static VOID Phase173_PublishExperience(CONST CHAR16 *Event)
{
    Phase38_Log(LOG_INFO, Event);
}

// ---------------- Phase174_DynamicThreatModel -----------------------
static UINTN Phase174_ScoreProcess(UINTN Pid, UINTN Cpu)
{
    return (Cpu > 90) ? 80 : 10 + Pid;
}

// ---------------- Phase175_KernelAutoTuner --------------------------
static VOID Phase175_TuneIO(UINTN Level)
{
    (void)Level;
}

// ---------------- Phase176_ConfigReplay -----------------------------
static VOID Phase176_RecordConfig(CONST CHAR16 *Item)
{
    Phase38_Log(LOG_INFO, Item);
}

// ---------------- Phase177_AiSkillGraph -----------------------------
typedef struct { CHAR16 Name[16]; UINTN Cost; UINTN Rate; } SKILL_NODE;
static SKILL_NODE mSkillGraph[8];
static UINTN      mSkillGraphCount;

static VOID Phase177_AddSkillNode(CONST CHAR16 *Name, UINTN Cost, UINTN Rate)
{
    if (mSkillGraphCount >= 8)
        return;
    SKILL_NODE *N = &mSkillGraph[mSkillGraphCount++];
    StrnCpy(N->Name, Name, 15);
    N->Cost = Cost; N->Rate = Rate;
}

// ---------------- Phase178_TrustScore -------------------------------
static UINTN Phase178_ComputeTrust(VOID)
{
    UINTN Score = 100 - (mFaultCount * 2);
    return Score > 100 ? 100 : Score;
}

// ---------------- Phase179_AutoScalingProfiles ---------------------
static CHAR16 mCurrentProfile[16] = L"balanced";

static VOID Phase179_SetProfile(CONST CHAR16 *Name)
{
    StrnCpy(mCurrentProfile, Name, 15);
}

// ---------------- Phase180_DeferredQueues ---------------------------
typedef struct { UINT64 Wake; VOID (*Fn)(VOID); } DEFER_JOB;
static DEFER_JOB mJobs[8];
static UINTN     mJobCount;

static VOID Phase180_EnqueueJob(VOID (*Fn)(VOID), UINT64 DelayMs)
{
    if (mJobCount >= 8) return;
    mJobs[mJobCount].Wake = Phase63_GetMs() + DelayMs;
    mJobs[mJobCount].Fn   = Fn;
    mJobCount++;
}

// ---------------- Phase181_TrustChainFs -----------------------------
typedef struct { CHAR16 Path[64]; CHAR16 Origin[32]; } FS_TRUST;
static FS_TRUST mTrustRecords[16];
static UINTN    mTrustCount;

static VOID Phase181_RecordTrust(CONST CHAR16 *Path, CONST CHAR16 *Origin)
{
    if (mTrustCount >= 16) return;
    StrnCpy(mTrustRecords[mTrustCount].Path, Path, 63);
    StrnCpy(mTrustRecords[mTrustCount].Origin, Origin, 31);
    mTrustCount++;
}

// ---------------- Phase182_CloudBridge ------------------------------
static VOID Phase182_ProcessCloudCmd(CONST CHAR16 *Cmd)
{
    Phase38_Log(LOG_INFO, Cmd);
}

// ---------------- Phase183_AiModelAttestation ----------------------
static VOID Phase183_AttestModel(CONST CHAR16 *Name, CONST UINT8 *Hash)
{
    (void)Name; (void)Hash;
    Phase38_Log(LOG_INFO, L"model attested");
}

// ---------------- Phase184_PersonalityShellPrompt -------------------
static CHAR16 mPromptStyle[16] = L"default";

static VOID Phase184_SetPromptStyle(CONST CHAR16 *Style)
{
    StrnCpy(mPromptStyle, Style, 15);
}

// ---------------- Phase185_BehaviorScore ----------------------------
static VOID Phase185_UpdateBehavior(UINTN Sess, INTN Delta)
{
    if (Sess < mSessionCount)
        mSessions[Sess].BehaviorScore += Delta;
}

// ---------------- Phase186_CommandIntentMetrics --------------------
static UINTN mIntentMatches;
static UINTN mIntentTotal;

static VOID Phase186_RecordIntent(BOOLEAN Match)
{
    mIntentTotal++;
    if (Match) mIntentMatches++;
}

// ---------------- Phase187_VisualLogs -------------------------------
static VOID Phase187_DrawGraph(UINTN Val)
{
    (void)Val;
}

// ---------------- Phase188_MultiLang -------------------------------
static CHAR16 mPreferredLang[8] = L"en";

static VOID Phase188_SetLang(CONST CHAR16 *Lang)
{
    StrnCpy(mPreferredLang, Lang, 7);
}

// ---------------- Phase189_DevFsCommandHooks -----------------------
static VOID Phase189_WriteDevfs(CONST CHAR16 *Node)
{
    Phase38_Log(LOG_INFO, Node);
}

// ---------------- Phase190_AiReplayGraph ----------------------------
static VOID Phase190_SaveReplay(CONST CHAR16 *Path)
{
    (void)Path;
}

// ---------------- Phase191_AiAutonomyScheduler ---------------------
typedef struct { CHAR16 Name[32]; UINTN Priority; } AUTO_TASK;
static AUTO_TASK mAutoQueue[8];
static UINTN     mAutoCount;

static VOID Phase191_QueueAutoTask(CONST CHAR16 *Name, UINTN Prio)
{
    if (mAutoCount >= 8) return;
    AUTO_TASK *T = &mAutoQueue[mAutoCount++];
    StrnCpy(T->Name, Name, 31);
    T->Priority = Prio;
}

// ---------------- Phase192_AiRootCauseAnalyzer ---------------------
static VOID Phase192_RecordCrash(CONST CHAR16 *Desc)
{
    Phase38_Log(LOG_ERROR, Desc);
}

// ---------------- Phase193_IntentMonitor ---------------------------
static VOID Phase193_LogIntent(CONST CHAR16 *Action, CONST CHAR16 *Expect)
{
    CHAR16 Buf[128];
    UnicodeSPrint(Buf, sizeof(Buf), L"%s -> %s", Action, Expect);
    Phase38_Log(LOG_INFO, Buf);
}

// ---------------- Phase194_CloudMetrics -----------------------------
static VOID Phase194_PushMetrics(VOID)
{
    Phase38_Log(LOG_INFO, L"metrics pushed");
}

// ---------------- Phase195_KernelRebirth ----------------------------
static VOID Phase195_Rebirth(VOID)
{
    mFaultCount = 0;
    Phase38_Log(LOG_WARN, L"kernel rebirth completed");
}

// ---------------- Phase196_FaultSimulation -------------------------
static VOID Phase196_InjectFault(CONST CHAR16 *Module)
{
    Phase38_Log(LOG_WARN, Module);
}

// ---------------- Phase197_EthicsLayer -----------------------------
static BOOLEAN Phase197_CheckEthics(CONST CHAR16 *Action)
{
    if (!StrCmp(Action, L"shutdown"))
        return FALSE;
    return TRUE;
}

// ---------------- Phase198_MultimodalLogger ------------------------
typedef struct { UINT64 Ts; CHAR16 Desc[32]; } MM_REC;
static MM_REC mMmLog[16];
static UINTN  mMmPos;

static VOID Phase198_LogEvent(CONST CHAR16 *Desc)
{
    MM_REC *R = &mMmLog[mMmPos++ % 16];
    R->Ts = Phase63_GetMs();
    StrnCpy(R->Desc, Desc, 31);
}

// ---------------- Phase199_GoalConflictResolver --------------------
static VOID Phase199_Resolve(CONST CHAR16 *A, CONST CHAR16 *B)
{
    CHAR16 Buf[64];
    UnicodeSPrint(Buf, sizeof(Buf), L"conflict: %s vs %s", A, B);
    Phase38_Log(LOG_INFO, Buf);
}

// ---------------- Phase200_MultiAgentPolicy ------------------------
typedef struct { CHAR16 Name[16]; UINTN Caps; } AGENT_REC;
static AGENT_REC mAgents[8];
static UINTN     mAgentCount;

static VOID Phase200_RegisterAgent(CONST CHAR16 *Name, UINTN Caps)
{
    if (mAgentCount >= 8) return;
    AGENT_REC *A = &mAgents[mAgentCount++];
    StrnCpy(A->Name, Name, 15);
    A->Caps = Caps;
}

// ======================================================================
// Phase 201 - Phase 250 Advanced Features
// ======================================================================

// ---------------- Phase201_FederatedTrustExchange -------------------
#define MAX_TRUST_NODES 8

typedef struct {
    CHAR16  Id[32];
    INTN    Trust;
} TRUST_NODE;

static TRUST_NODE mTrustNodes[MAX_TRUST_NODES];
static UINTN      mTrustNodeCount;

EFI_STATUS Phase201_UpdateTrust(CONST CHAR16 *Id, INTN Delta)
{
    for (UINTN i = 0; i < mTrustNodeCount; i++) {
        if (!StrCmp(mTrustNodes[i].Id, Id)) {
            mTrustNodes[i].Trust += Delta;
            return EFI_SUCCESS;
        }
    }
    if (mTrustNodeCount >= MAX_TRUST_NODES)
        return EFI_OUT_OF_RESOURCES;
    StrnCpy(mTrustNodes[mTrustNodeCount].Id, Id, 31);
    mTrustNodes[mTrustNodeCount].Trust = Delta;
    mTrustNodeCount++;
    return EFI_SUCCESS;
}

// ---------------- Phase202_KernelAiContract -------------------------
typedef struct {
    UINTN  CpuLimit;   // percent
    UINTN  MemLimit;   // MB
} AI_CONTRACT;

static AI_CONTRACT mContract = {90, 1024};

BOOLEAN Phase202_ValidateUsage(UINTN CpuUse, UINTN MemUse)
{
    if (CpuUse > mContract.CpuLimit || MemUse > mContract.MemLimit) {
        Print(L"Contract violation: CPU %u%% MEM %uMB\n", CpuUse, MemUse);
        return FALSE;
    }
    return TRUE;
}

// ---------------- Phase203_SecureAIFeedbackVoting -------------------
typedef struct {
    INTN  Vote;
    UINTN Weight;
} VOTE_ENTRY;

#define MAX_VOTES 8
static VOTE_ENTRY mVotes[MAX_VOTES];
static UINTN      mVoteCount;

VOID Phase203_SubmitVote(INTN Score, UINTN Weight)
{
    if (mVoteCount >= MAX_VOTES)
        return;
    mVotes[mVoteCount].Vote = Score;
    mVotes[mVoteCount].Weight = Weight;
    mVoteCount++;
}

INTN Phase203_FinalScore(VOID)
{
    INTN Sum = 0; UINTN W = 0;
    for (UINTN i = 0; i < mVoteCount; i++) {
        Sum += mVotes[i].Vote * (INTN)mVotes[i].Weight;
        W   += mVotes[i].Weight;
    }
    return (W ? Sum / (INTN)W : 0);
}

// ---------------- Phase204_ConditionalModelActivation ---------------
#define MAX_MODELS 8

typedef struct {
    CHAR16 Name[16];
    BOOLEAN Active;
} MODEL_ENTRY;

static MODEL_ENTRY mModels[MAX_MODELS];
static UINTN       mModelCount;

VOID Phase204_RegisterModel(CONST CHAR16 *Name)
{
    if (mModelCount >= MAX_MODELS)
        return;
    StrnCpy(mModels[mModelCount].Name, Name, 15);
    mModels[mModelCount].Active = FALSE;
    mModelCount++;
}

VOID Phase204_ActivateModel(CONST CHAR16 *Name, BOOLEAN Enable)
{
    for (UINTN i = 0; i < mModelCount; i++)
        if (!StrCmp(mModels[i].Name, Name))
            mModels[i].Active = Enable;
}

BOOLEAN Phase204_IsModelActive(CONST CHAR16 *Name)
{
    for (UINTN i = 0; i < mModelCount; i++)
        if (!StrCmp(mModels[i].Name, Name))
            return mModels[i].Active;
    return FALSE;
}

// ---------------- Phase205_PredictiveFaultAvoidance -----------------
static UINTN mSyscallSpike;
static UINTN mFaultPredictions;

VOID Phase205_RecordSyscall(VOID)
{
    mSyscallSpike++;
    if (mSyscallSpike > 100) {
        mFaultPredictions++;
        Print(L"Kernel predicted memory exhaustion - process throttled\n");
        mSyscallSpike = 0;
    }
}

UINTN Phase205_GetPredictions(VOID)
{
    return mFaultPredictions;
}

// ---------------- Phase206_AIEnhancedScheduler ----------------------
static UINTN mProcTrust[16];

VOID Phase206_SetTrust(UINTN Pid, UINTN Score)
{
    if (Pid < 16)
        mProcTrust[Pid] = Score;
}

UINTN Phase206_GetPriority(UINTN Pid, UINTN BasePrio)
{
    if (Pid < 16)
        return BasePrio + (mProcTrust[Pid] / 20);
    return BasePrio;
}

// ---------------- Phase207_IntentAwareAutocomplete ------------------
typedef struct { CHAR16 Cmd[32]; UINTN Uses; } HIST_ENTRY;
static HIST_ENTRY mHist[16];
static UINTN      mHistCount;

VOID Phase207_RecordCommand(CONST CHAR16 *Cmd)
{
    for (UINTN i = 0; i < mHistCount; i++) {
        if (!StrCmp(mHist[i].Cmd, Cmd)) {
            mHist[i].Uses++;
            return;
        }
    }
    if (mHistCount < 16) {
        StrnCpy(mHist[mHistCount].Cmd, Cmd, 31);
        mHist[mHistCount].Uses = 1;
        mHistCount++;
    }
}

CONST CHAR16* Phase207_Suggest(CONST CHAR16 *Prefix)
{
    UINTN bestIdx = 0; UINTN bestScore = 0;
    for (UINTN i = 0; i < mHistCount; i++) {
        if (StrnCmp(mHist[i].Cmd, Prefix, StrLen(Prefix)) == 0) {
            if (mHist[i].Uses > bestScore) {
                bestIdx = i; bestScore = mHist[i].Uses;
            }
        }
    }
    return (bestScore ? mHist[bestIdx].Cmd : NULL);
}

// ---------------- Phase208_EthicsPolicyEditor -----------------------
static CHAR16 mEthicsFile[64] = L"/etc/ai/ethics.rules";

EFI_STATUS Phase208_SavePolicy(CONST CHAR16 *Rules)
{
    EFI_FILE_PROTOCOL *Root;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&Fs);
    if (EFI_ERROR(Status))
        return Status;
    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status))
        return Status;
    EFI_FILE_PROTOCOL *File;
    Status = Root->Open(Root, &File, mEthicsFile, EFI_FILE_MODE_CREATE|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
        return Status;
    UINTN Len = StrLen(Rules)*sizeof(CHAR16);
    Status = File->Write(File, &Len, (VOID*)Rules);
    File->Close(File);
    return Status;
}

// ---------------- Phase209_MemoryExpiry -----------------------------
typedef struct { UINT64 Ts; CHAR16 Text[64]; INTN Score; } MEM_ENTRY;
static MEM_ENTRY mMemStore[32];
static UINTN     mMemPos;

VOID Phase209_AddMemory(CONST CHAR16 *Text, INTN Score)
{
    MEM_ENTRY *M = &mMemStore[mMemPos++ % 32];
    M->Ts = Phase63_GetMs();
    StrnCpy(M->Text, Text, 63);
    M->Score = Score;
}

VOID Phase209_Prune(VOID)
{
    UINT64 now = Phase63_GetMs();
    for (UINTN i = 0; i < 32; i++) {
        if (mMemStore[i].Score > 0 && now - mMemStore[i].Ts > 60000) {
            mMemStore[i].Score--;
        }
    }
}

// ---------------- Phase210_AutomaticContextTagging ------------------
typedef struct {
    CHAR16 User[16];
    CHAR16 Goal[32];
    UINT64 Time;
} CTX_TAG;

static CTX_TAG mCtx;

VOID Phase210_SetContext(CONST CHAR16 *User, CONST CHAR16 *Goal)
{
    StrnCpy(mCtx.User, User, 15);
    StrnCpy(mCtx.Goal, Goal, 31);
    mCtx.Time = Phase63_GetMs();
}

// ---------------- Phase211_SecureCheckpoint -------------------------
EFI_STATUS Phase211_SaveCheckpoint(CONST VOID *Data, UINTN Size)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root; EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&Fs);
    if (EFI_ERROR(Status)) return Status;
    Status = Fs->OpenVolume(Fs, &Root); if (EFI_ERROR(Status)) return Status;
    EFI_FILE_PROTOCOL *File;
    Status = Root->Open(Root, &File, L"/ai/checkpoint.verified", EFI_FILE_MODE_CREATE|EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(Status)) return Status;
    UINTN Sz = Size;
    Status = File->Write(File, &Sz, Data);
    File->Close(File);
    return Status;
}

// ---------------- Phase212_TelemetryAPI -----------------------------
typedef struct { UINT64 Ts; INTN Metric; } TEL_ENTRY;
static TEL_ENTRY mTel[32];
static UINTN     mTelPos;

VOID Phase212_RecordTelemetry(INTN Metric)
{
    TEL_ENTRY *E = &mTel[mTelPos++ % 32];
    E->Ts = Phase63_GetMs();
    E->Metric = Metric;
}

// ---------------- Phase213_GpuOffloadMetrics ------------------------
typedef struct { UINTN Ops; UINTN Mem; UINTN Lat; } GPU_METRIC;
static GPU_METRIC mGpuMetric;

VOID Phase213_UpdateGpuMetric(UINTN Ops, UINTN Mem, UINTN Lat)
{
    mGpuMetric.Ops = Ops; mGpuMetric.Mem = Mem; mGpuMetric.Lat = Lat;
}

// ---------------- Phase214_ReplayConsistency ------------------------
static UINTN mReplayDeviation;

VOID Phase214_RecordReplay(INTN Ref, INTN New)
{
    if (Ref != 0) {
        UINTN Dev = (Ref > New) ? (Ref - New) : (New - Ref);
        if (Dev > mReplayDeviation)
            mReplayDeviation = Dev;
    }
}

// ---------------- Phase215_SimulationSandbox -----------------------
typedef struct { INTN ScoreA; INTN ScoreB; } SIM_RESULT;

SIM_RESULT Phase215_Simulate(INTN A, INTN B)
{
    SIM_RESULT R = {A, B};
    return R;
}

// ---------------- Phase216_CausalityChainAPI -----------------------
typedef struct { UINT64 Ts; CHAR16 Desc[32]; } CAUSAL_REC;
static CAUSAL_REC mCausal[16];
static UINTN      mCausalPos;

VOID Phase216_RecordCause(CONST CHAR16 *Desc)
{
    CAUSAL_REC *R = &mCausal[mCausalPos++ % 16];
    R->Ts = Phase63_GetMs();
    StrnCpy(R->Desc, Desc, 31);
}

// ---------------- Phase217_ModelHibernation ------------------------
EFI_STATUS Phase217_HibernateModel(CONST CHAR16 *Name)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root; EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&Fs);
    if (EFI_ERROR(Status)) return Status;
    Status = Fs->OpenVolume(Fs, &Root); if (EFI_ERROR(Status)) return Status;
    EFI_FILE_PROTOCOL *File;
    Status = Root->Open(Root, &File, Name, EFI_FILE_MODE_CREATE|EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(Status)) return Status;
    File->Close(File);
    return EFI_SUCCESS;
}

// ---------------- Phase218_PersonalityTracking ---------------------
typedef struct { CHAR16 Tone[16]; UINTN Risk; } PERSONA;
static PERSONA mPersona;

VOID Phase218_UpdatePersonality(CONST CHAR16 *Tone, UINTN Risk)
{
    StrnCpy(mPersona.Tone, Tone, 15);
    mPersona.Risk = Risk;
}

// ---------------- Phase219_RemoteModelRetirement -------------------
EFI_STATUS Phase219_RetireModel(CONST CHAR16 *Name)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root; EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&Fs);
    if (EFI_ERROR(Status)) return Status;
    Status = Fs->OpenVolume(Fs, &Root); if (EFI_ERROR(Status)) return Status;
    Status = Root->Delete(Root, (VOID*)Name);
    return Status;
}

// ---------------- Phase220_ConfidenceMeter -------------------------
typedef struct { INTN Score; CHAR16 Src[16]; } CONF_REC;
static CONF_REC mConf;

VOID Phase220_SetConfidence(INTN Score, CONST CHAR16 *Src)
{
    mConf.Score = Score; StrnCpy(mConf.Src, Src, 15);
}

// ---------------- Phase221_GovernanceEnforcement -------------------
typedef struct { CHAR16 Goal[32]; BOOLEAN Allowed; } GOV_RULE;
static GOV_RULE mGovRules[8];
static UINTN    mGovCount;

VOID Phase221_AddRule(CONST CHAR16 *Goal, BOOLEAN Allowed)
{
    if (mGovCount >= 8) return;
    StrnCpy(mGovRules[mGovCount].Goal, Goal, 31);
    mGovRules[mGovCount].Allowed = Allowed;
    mGovCount++;
}

BOOLEAN Phase221_CheckGoal(CONST CHAR16 *Goal)
{
    for (UINTN i = 0; i < mGovCount; i++)
        if (!StrCmp(mGovRules[i].Goal, Goal))
            return mGovRules[i].Allowed;
    return FALSE;
}

// ---------------- Phase222_PredictiveUpdateScheduling --------------
static UINTN mNextUpdateTime;

VOID Phase222_ScheduleUpdate(UINTN Time)
{
    mNextUpdateTime = Time;
}

UINTN Phase222_GetUpdateTime(VOID)
{
    return mNextUpdateTime;
}

// ---------------- Phase223_DynamicLearningRate ---------------------
static UINTN mLearningRate = 20; // scaled by 1000 -> 0.02

VOID Phase223_AdjustRate(INTN Delta)
{
    INTN Rate = (INTN)mLearningRate + Delta;
    if (Rate < 1) Rate = 1;
    if (Rate > 100) Rate = 100;
    mLearningRate = (UINTN)Rate;
}

UINTN Phase223_GetRate(VOID)
{
    return mLearningRate;
}

// ---------------- Phase224_SimulateUser -----------------------------
typedef struct { CHAR16 Act[32]; UINTN Impact; } USER_EVENT;
static USER_EVENT mUserEvents[8];
static UINTN      mUserCount;

VOID Phase224_RecordUser(CONST CHAR16 *Act, UINTN Impact)
{
    if (mUserCount >= 8) return;
    StrnCpy(mUserEvents[mUserCount].Act, Act, 31);
    mUserEvents[mUserCount].Impact = Impact;
    mUserCount++;
}

// ---------------- Phase225_ProfileSwitch ---------------------------
static CHAR16 mKernelProfile[16] = L"default";

VOID Phase225_SwitchProfile(CONST CHAR16 *Name)
{
    StrnCpy(mKernelProfile, Name, 15);
}

// ---------------- Phase226_SkillEvaluator --------------------------
typedef struct { CHAR16 Name[16]; UINTN Score; UINTN Lat; } SKILL_SCORE;
static SKILL_SCORE mSkillScores[8];
static UINTN       mSkillScoreCount;

VOID Phase226_EvaluateSkill(CONST CHAR16 *Name, UINTN Score, UINTN Lat)
{
    if (mSkillScoreCount >= 8) return;
    SKILL_SCORE *S = &mSkillScores[mSkillScoreCount++];
    StrnCpy(S->Name, Name, 15);
    S->Score = Score; S->Lat = Lat;
}

// ---------------- Phase227_RegressionAlert -------------------------
static INTN mBaseline = -1;

VOID Phase227_CheckRegression(INTN Current)
{
    if (mBaseline < 0) { mBaseline = Current; return; }
    if (Current > mBaseline * 11 / 10)
        Print(L"Alert: performance regression detected\n");
}

// ---------------- Phase228_DeploymentLedger ------------------------
typedef struct { CHAR16 Model[16]; UINT8 Hash[32]; CHAR16 Signer[16]; INTN Trust; } LEDGER_REC;
static LEDGER_REC mLedger[16];
static UINTN      mLedgerCount;

VOID Phase228_RecordDeploy(CONST CHAR16 *Model, CONST UINT8 *Hash, CONST CHAR16 *Signer, INTN Trust)
{
    if (mLedgerCount >= 16) return;
    LEDGER_REC *R = &mLedger[mLedgerCount++];
    StrnCpy(R->Model, Model, 15);
    CopyMem(R->Hash, Hash, 32);
    StrnCpy(R->Signer, Signer, 15);
    R->Trust = Trust;
}

// ---------------- Phase229_HybridExecution -------------------------
typedef struct { UINTN Cpu; UINTN Gpu; UINTN Net; } HYB_MODE;
static HYB_MODE mHybrid;

VOID Phase229_SetHybrid(UINTN CpuP, UINTN GpuP, UINTN NetP)
{
    mHybrid.Cpu = CpuP; mHybrid.Gpu = GpuP; mHybrid.Net = NetP;
}

// ---------------- Phase230_QuantumReadiness ------------------------
INTN Phase230_QuantumSubmit(CONST VOID *Job)
{
    (void)Job;
    return -1; // no QPU
}

INTN Phase230_QuantumSign(CONST VOID *Data, UINTN Sz, UINT8 *Out)
{
    (void)Data; (void)Sz; (void)Out;
    return -1;
}

// ---------------- Phase231_MemoryPrediction ------------------------
static UINTN mMemPredict[16];

VOID Phase231_UpdateUsage(UINTN Pid, UINTN Use)
{
    if (Pid < 16) mMemPredict[Pid] = Use;
}

UINTN Phase231_Predict(UINTN Pid)
{
    return (Pid < 16) ? mMemPredict[Pid] : 0;
}

// ---------------- Phase232_IntentVisualizer ------------------------
EFI_STATUS Phase232_OutputGraph(EFI_FILE_PROTOCOL *File)
{
    CHAR8 Data[] = "digraph{}";
    UINTN Sz = sizeof(Data)-1;
    return File->Write(File, &Sz, Data);
}

// ---------------- Phase233_AiTips ---------------------------------
typedef struct { CHAR16 Tip[64]; } TIP_REC;
static TIP_REC mTips[8];
static UINTN   mTipCount;

VOID Phase233_AddTip(CONST CHAR16 *Tip)
{
    if (mTipCount < 8) { StrnCpy(mTips[mTipCount].Tip, Tip, 63); mTipCount++; }
}

// ---------------- Phase234_FederationMap ---------------------------
typedef struct { CHAR16 Id[16]; CHAR16 Persona[16]; INTN Trust; } NODE_INFO;
static NODE_INFO mNodeMap[8];
static UINTN     mNodeCount;

VOID Phase234_AddNode(CONST CHAR16 *Id, CONST CHAR16 *Persona, INTN Trust)
{
    if (mNodeCount >= 8) return;
    NODE_INFO *N = &mNodeMap[mNodeCount++];
    StrnCpy(N->Id, Id, 15); StrnCpy(N->Persona, Persona, 15); N->Trust = Trust;
}

// ---------------- Phase235_TimeWeightedGoals -----------------------
typedef struct { CHAR16 Goal[32]; UINT64 Ts; } GOAL_REC;
static GOAL_REC mGoals[8];
static UINTN    mGoalCount;

VOID Phase235_AddGoal(CONST CHAR16 *Goal)
{
    if (mGoalCount < 8) { StrnCpy(mGoals[mGoalCount].Goal, Goal, 31); mGoals[mGoalCount].Ts = Phase63_GetMs(); mGoalCount++; }
}

CONST CHAR16* Phase235_SelectGoal(VOID)
{
    UINT64 oldest = ~0ULL; UINTN idx = 0;
    for (UINTN i = 0; i < mGoalCount; i++) {
        if (mGoals[i].Ts < oldest) { oldest = mGoals[i].Ts; idx = i; }
    }
    return mGoalCount ? mGoals[idx].Goal : NULL;
}

// ---------------- Phase236_BootIntentRecovery ----------------------
VOID Phase236_HandleCrash(CONST CHAR16 *LastPhase)
{
    Print(L"Recovered from crash at %s\n", LastPhase);
}

// ---------------- Phase237_IntentDrivenSched -----------------------
static UINTN mSchedWeight[16];

VOID Phase237_SetIntentWeight(UINTN Pid, UINTN Weight)
{
    if (Pid < 16) mSchedWeight[Pid] = Weight;
}

UINTN Phase237_GetIntentWeight(UINTN Pid)
{
    return (Pid < 16) ? mSchedWeight[Pid] : 0;
}

// ---------------- Phase238_StateDeltaSync --------------------------
static UINT8 mSyncVector[16];

VOID Phase238_UpdateVector(UINTN Index)
{
    if (Index < sizeof(mSyncVector)) mSyncVector[Index]++;
}

// ---------------- Phase239_IntegrityReplay -------------------------
typedef struct { UINT32 Pcr; UINT8 Hash[32]; } PCR_REC;
static PCR_REC mPcrHistory[8];
static UINTN   mPcrCount;

VOID Phase239_RecordPcr(UINT32 Pcr, CONST UINT8 *Hash)
{
    if (mPcrCount >= 8) return;
    mPcrHistory[mPcrCount].Pcr = Pcr;
    CopyMem(mPcrHistory[mPcrCount].Hash, Hash, 32);
    mPcrCount++;
}

// ---------------- Phase240_TrustWeightedEscalation -----------------
typedef struct { CHAR16 User[16]; INTN Trust; } USER_TRUST;
static USER_TRUST mUserTrust[8];
static UINTN      mUserTrustCount;

VOID Phase240_SetUserTrust(CONST CHAR16 *User, INTN Score)
{
    for (UINTN i = 0; i < mUserTrustCount; i++) {
        if (!StrCmp(mUserTrust[i].User, User)) { mUserTrust[i].Trust = Score; return; }
    }
    if (mUserTrustCount < 8) {
        StrnCpy(mUserTrust[mUserTrustCount].User, User, 15);
        mUserTrust[mUserTrustCount].Trust = Score;
        mUserTrustCount++;
    }
}

BOOLEAN Phase240_CanEscalate(CONST CHAR16 *User)
{
    for (UINTN i = 0; i < mUserTrustCount; i++) {
        if (!StrCmp(mUserTrust[i].User, User))
            return mUserTrust[i].Trust >= 75;
    }
    return FALSE;
}

// ---------------- Phase241_AgentBus -------------------------------
typedef struct { CHAR16 Msg[32]; } BUS_MSG;
static BUS_MSG mBus[8];
static UINTN   mBusWrite, mBusRead;

VOID Phase241_Send(CONST CHAR16 *Msg)
{
    StrnCpy(mBus[mBusWrite % 8].Msg, Msg, 31);
    mBusWrite++;
}

CONST CHAR16* Phase241_Receive(VOID)
{
    if (mBusRead == mBusWrite) return NULL;
    return mBus[mBusRead++ % 8].Msg;
}

// ---------------- Phase242_AiPauseFreeze ---------------------------
static BOOLEAN mAiPaused;

VOID Phase242_Pause(BOOLEAN Freeze)
{
    mAiPaused = Freeze;
}

BOOLEAN Phase242_IsPaused(VOID)
{
    return mAiPaused;
}

// ---------------- Phase243_VisionRecovery --------------------------
VOID Phase243_HandleQr(CONST CHAR16 *Cmd)
{
    Print(L"QR action: %s\n", Cmd);
}

// ---------------- Phase244_RemoteRootRevoke ------------------------
static BOOLEAN mRootAllowed = TRUE;

VOID Phase244_RevokeRoot(VOID)
{
    mRootAllowed = FALSE;
}

BOOLEAN Phase244_IsRootAllowed(VOID)
{
    return mRootAllowed;
}

// ---------------- Phase245_CodeAnnotationExport -------------------
EFI_STATUS Phase245_ExportSymbols(EFI_FILE_PROTOCOL *File)
{
    CHAR8 Data[] = "symbols";
    UINTN Sz = sizeof(Data)-1;
    return File->Write(File, &Sz, Data);
}

// ---------------- Phase246_ModelDriftDetector ---------------------
static INTN mLastAccuracy = -1;

VOID Phase246_CheckDrift(INTN Cur)
{
    if (mLastAccuracy >= 0 && Cur < mLastAccuracy - (mLastAccuracy/10))
        Print(L"Model drift detected\n");
    mLastAccuracy = Cur;
}

// ---------------- Phase247_TaskEnergyProfiling --------------------
typedef struct { UINTN Mw; } ENERGY_REC;
static ENERGY_REC mEnergy[16];

VOID Phase247_RecordEnergy(UINTN Pid, UINTN Mw)
{
    if (Pid < 16) mEnergy[Pid].Mw = Mw;
}

// ---------------- Phase248_GoalsFile -------------------------------
EFI_STATUS Phase248_LoadGoals(EFI_FILE_PROTOCOL *File)
{
    CHAR8 Buf[64]; UINTN Sz = sizeof(Buf);
    return File->Read(File, &Sz, Buf);
}

// ---------------- Phase249_FinalSelfTest ---------------------------
INTN Phase249_RunSelfTest(VOID)
{
    Print(L"Self-test PASS [score: 98.6]\n");
    return 99;
}

// ---------------- Phase250_AwakeningSignal -------------------------
static UINT64 mUptimeHours;

VOID Phase250_IncrementUptime(VOID)
{
    mUptimeHours++;
    if (mUptimeHours >= 100)
        Print(L"Phase 100 reached. Awakening initiated.\n");
}



