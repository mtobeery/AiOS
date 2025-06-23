#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
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
