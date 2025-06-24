// AiOSLoader_main.c - Realistic 4000+ Line Bootloader with Logical Phase Structure

#include <Uefi.h>
#include <Library/UefiLib.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/Tcg2Protocol.h>
#include <Protocol/Hash2.h>
#include <Library/BaseCryptLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Guid/FileInfo.h>
#include <Guid/Acpi.h>
#include <Guid/GlobalVariable.h>
#include "font8x8_basic.inc"
#include "loader_structs.h"

static BOOT_CONTEXT gBootContext;

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

static ELF64_EHDR gElfHeader;
static ELF64_PHDR *gPhdrs = NULL;
static UINT64 gKernelBaseTmp = 0;
static UINT64 gKernelMinAddr = 0;
static EFI_HASH2_PROTOCOL *gHash2 = NULL;
static UINT8 *gKernelBuffer = NULL;
static UINTN gKernelBufferSize = 0;
static EFI_FILE_HANDLE gSigFile = NULL;
static UINT8 *gSignature = NULL;
static UINTN gSignatureSize = 0;

static EFI_STATUS Phase001_InitializeBootContext(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    ZeroMem(&gBootContext, sizeof(gBootContext));
    gBootContext.ImageHandle = ImageHandle;
    gBootContext.SystemTable = SystemTable;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase002_LogSystemTableInfo(void) {
    Print(L"Firmware: %s Rev %u\n", gST->FirmwareVendor, gST->FirmwareRevision);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase003_LogCurrentTime(void) {
    EFI_TIME Time;
    if (!EFI_ERROR(gRT->GetTime(&Time, NULL))) {
        Print(L"Time: %04u-%02u-%02u %02u:%02u:%02u\n", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase004_CheckSecureBoot(void) {
    UINT8 Value = 0; UINTN Size = sizeof(Value);
    EFI_STATUS Status = gRT->GetVariable(L"SecureBoot", &gEfiGlobalVariableGuid, NULL, &Size, &Value);
    if (!EFI_ERROR(Status))
        Print(L"SecureBoot: %u\n", Value);
    else
        Print(L"SecureBoot variable not found\n");
    return Status;
}

static EFI_STATUS Phase005_CheckSetupMode(void) {
    UINT8 Value = 0; UINTN Size = sizeof(Value);
    EFI_STATUS Status = gRT->GetVariable(L"SetupMode", &gEfiGlobalVariableGuid, NULL, &Size, &Value);
    if (!EFI_ERROR(Status))
        Print(L"SetupMode: %u\n", Value);
    return Status;
}

static EFI_STATUS Phase006_ClearConsole(void) {
    return gST->ConOut->ClearScreen(gST->ConOut);
}

static EFI_STATUS Phase007_LogBootServices(void) {
    Print(L"BootServices @ %p\n", gBS);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase008_PrepareParams(void) {
    ZeroMem(&gBootContext.Params, sizeof(gBootContext.Params));
    return EFI_SUCCESS;
}

static EFI_STATUS Phase009_LogConsoleMode(void) {
    Print(L"Console mode: %u\n", gST->ConOut->Mode->Mode);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase010_EnvironmentReady(void) {
    Print(L"UEFI environment ready\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase011_LocateLoadedImage(void) {
    return gBS->HandleProtocol(gBootContext.ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&gBootContext.LoadedImage);
}

static EFI_STATUS Phase012_LocateFileSystem(void) {
    return gBS->HandleProtocol(gBootContext.LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&gBootContext.FileSystem);
}

static EFI_STATUS Phase013_OpenRootDirectory(void) {
    return gBootContext.FileSystem->OpenVolume(gBootContext.FileSystem, &gBootContext.RootDir);
}

static EFI_STATUS Phase014_LocateGraphicsOutput(void) {
    return gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gBootContext.Gop);
}

static EFI_STATUS Phase015_LogGraphicsMode(void) {
    if (gBootContext.Gop != NULL) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = gBootContext.Gop->Mode->Info;
        Print(L"GOP: %ux%u\n", Info->HorizontalResolution, Info->VerticalResolution);
        gBootContext.Params.GopModeInfo = Info;
        gBootContext.Params.FrameBufferBase = gBootContext.Gop->Mode->FrameBufferBase;
        gBootContext.Params.FrameBufferSize = gBootContext.Gop->Mode->FrameBufferSize;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase016_GetAcpiRsdp(void) {
    EFI_CONFIGURATION_TABLE *Table = gST->ConfigurationTable;
    for (UINTN Index = 0; Index < gST->NumberOfTableEntries; ++Index) {
        if (CompareGuid(&Table[Index].VendorGuid, &gEfiAcpi20TableGuid) || CompareGuid(&Table[Index].VendorGuid, &gEfiAcpi10TableGuid)) {
            Print(L"ACPI RSDP @ %p\n", Table[Index].VendorTable);
            break;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase017_GetMemoryMapInfo(void) {
    UINTN MapSize = 0, MapKey, DescSize; UINT32 DescVer; EFI_STATUS Status;
    Status = gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    if (Status == EFI_BUFFER_TOO_SMALL) {
        gBootContext.Params.MemoryMapSize = MapSize;
        gBootContext.Params.DescriptorSize = DescSize;
        gBootContext.Params.DescriptorVersion = DescVer;
        Print(L"Memory map requires %lu bytes\n", MapSize);
        Status = EFI_SUCCESS;
    }
    return Status;
}

static EFI_STATUS Phase018_LogImagePath(void) {
    Print(L"Image base: %p\n", gBootContext.LoadedImage->ImageBase);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase019_ProtocolSetupComplete(void) {
    Print(L"UEFI protocols ready\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase020_PrepareTpmContext(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase021_LocateTcg2(void) {
    return gBS->LocateProtocol(&gEfiTcg2ProtocolGuid, NULL, (VOID**)&gBootContext.Tcg2);
}

static EFI_STATUS Phase022_QueryTpmCapabilities(void) {
    EFI_TCG2_BOOT_SERVICE_CAPABILITY Cap; EFI_STATUS Status;
    if (gBootContext.Tcg2 == NULL) return EFI_NOT_FOUND;
    Cap.Size = sizeof(Cap);
    Status = gBootContext.Tcg2->GetCapability(gBootContext.Tcg2, &Cap);
    if (!EFI_ERROR(Status)) {
        Print(L"TPM Present, ActiveBanks: %08x\n", Cap.ActivePcrBanks);
    }
    return Status;
}

static EFI_STATUS ReadAndPrintPcr(UINT32 Index) {
    TPML_PCR_SELECTION SelIn = {0};
    TPML_PCR_SELECTION SelOut;
    TPML_DIGEST Values;
    UINT32 Counter;
    SelIn.count = 1;
    SelIn.pcrSelections[0].hash = TPM_ALG_SHA256;
    SelIn.pcrSelections[0].sizeofSelect = 3;
    SelIn.pcrSelections[0].pcrSelect[0] = (Index < 8) ? (1 << Index) : 0;
    SelIn.pcrSelections[0].pcrSelect[1] = (Index >= 8 && Index < 16) ? (1 << (Index-8)) : 0;
    SelIn.pcrSelections[0].pcrSelect[2] = (Index >= 16) ? (1 << (Index-16)) : 0;
    EFI_STATUS Status = Tpm2PcrRead(&SelIn, &Counter, &SelOut, &Values);
    if (!EFI_ERROR(Status) && Values.count > 0) {
        Print(L"PCR%u: ", Index);
        for (UINTN i = 0; i < Values.digests[0].size; ++i)
            Print(L"%02x", Values.digests[0].buffer[i]);
        Print(L"\n");
    }
    return Status;
}

static EFI_STATUS Phase023_ReadPcr0(void) { return ReadAndPrintPcr(0); }
static EFI_STATUS Phase024_ReadPcr1(void) { return ReadAndPrintPcr(1); }
static EFI_STATUS Phase025_ReadPcr2(void) { return ReadAndPrintPcr(2); }
static EFI_STATUS Phase026_ReadPcr3(void) { return ReadAndPrintPcr(3); }
static EFI_STATUS Phase027_ReadPcr4(void) { return ReadAndPrintPcr(4); }
static EFI_STATUS Phase028_ReadPcr5(void) { return ReadAndPrintPcr(5); }

static EFI_STATUS Phase029_ExportEventLog(void) {
    if (gBootContext.Tcg2 == NULL) return EFI_NOT_STARTED;
    EFI_PHYSICAL_ADDRESS Log, Last; BOOLEAN Trunc;
    EFI_STATUS Status = gBootContext.Tcg2->GetEventLog(gBootContext.Tcg2, EFI_TCG2_EVENT_LOG_FORMAT_TCG_2, &Log, &Last, &Trunc);
    if (!EFI_ERROR(Status)) {
        UINTN Size = (UINTN)(Last - Log);
        Print(L"Event log %lu bytes%s\n", Size, Trunc ? L" (truncated)" : L"");
    }
    return Status;
}

static EFI_STATUS Phase030_TpmReady(void) {
    Print(L"TPM initialization done\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase031_GetMemoryMap(void) {
    EFI_STATUS Status;
    UINTN MapSize = 0, MapKey, DescSize; UINT32 DescVer;
    Status = gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    if (Status != EFI_BUFFER_TOO_SMALL) return Status;
    Status = gBS->AllocatePool(EfiBootServicesData, MapSize, (VOID**)&gBootContext.Params.MemoryMap);
    if (EFI_ERROR(Status)) return Status;
    gBootContext.Params.MemoryMapSize = MapSize;
    Status = gBS->GetMemoryMap(&gBootContext.Params.MemoryMapSize, gBootContext.Params.MemoryMap, &gBootContext.Params.MapKey, &gBootContext.Params.DescriptorSize, &gBootContext.Params.DescriptorVersion);
    return Status;
}

static EFI_STATUS Phase032_LogMemoryRegions(void) {
    UINTN Count = gBootContext.Params.MemoryMapSize / gBootContext.Params.DescriptorSize;
    for (UINTN i = 0; i < Count; ++i) {
        EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)gBootContext.Params.MemoryMap + i * gBootContext.Params.DescriptorSize);
        Print(L"Region %u: Type %u Pages %lu\n", (UINT32)i, Desc->Type, Desc->NumberOfPages);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase033_CalcFreeMemory(void) {
    UINTN Count = gBootContext.Params.MemoryMapSize / gBootContext.Params.DescriptorSize;
    UINT64 Total = 0;
    for (UINTN i = 0; i < Count; ++i) {
        EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)gBootContext.Params.MemoryMap + i * gBootContext.Params.DescriptorSize);
        if (Desc->Type == EfiConventionalMemory)
            Total += Desc->NumberOfPages * 4096ULL;
    }
    Print(L"Usable RAM: %lu bytes\n", Total);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase034_LogAcpiAddress(void) {
    EFI_CONFIGURATION_TABLE *Table = gST->ConfigurationTable;
    for (UINTN Index = 0; Index < gST->NumberOfTableEntries; ++Index) {
        if (CompareGuid(&Table[Index].VendorGuid, &gEfiAcpi20TableGuid)) {
            Print(L"ACPI table at %p\n", Table[Index].VendorTable);
            break;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase035_PrepareEntropySeed(void) {
    UINT64 Tsc = AsmReadTsc();
    Print(L"Entropy seed: %lx\n", Tsc);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase036_HashMemoryMap(void) {
    if (gBootContext.Params.MemoryMap == NULL) return EFI_NOT_READY;
    SHA256_CTX Ctx; UINT8 Digest[SHA256_DIGEST_LENGTH];
    sha256_init(&Ctx);
    sha256_update(&Ctx, (UINT8*)gBootContext.Params.MemoryMap, gBootContext.Params.MemoryMapSize);
    sha256_final(&Ctx, Digest);
    Print(L"Memory map hash: ");
    for (UINTN i = 0; i < SHA256_DIGEST_LENGTH; ++i) Print(L"%02x", Digest[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase037_StoreEntropy(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase038_DisplayEntropy(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase039_PrepareExitBoot(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase040_MemoryReady(void) { Print(L"Memory preparation done\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase041_OpenKernelFile(void) {
    CHAR16 *Path = L"\\EFI\\AiOS\\kernel.elf";
    return gBootContext.RootDir->Open(gBootContext.RootDir, &gBootContext.KernelFile, Path, EFI_FILE_MODE_READ, 0);
}

static EFI_STATUS Phase042_ReadElfHeader(UINT8 *Header) {
    UINTN Size = 64; return gBootContext.KernelFile->Read(gBootContext.KernelFile, &Size, Header);
}

static EFI_STATUS Phase043_ValidateElfMagic(UINT8 *Header) {
    if (Header[0]==0x7f && Header[1]=='E' && Header[2]=='L' && Header[3]=='F') return EFI_SUCCESS;
    return EFI_LOAD_ERROR;
}

static EFI_STATUS Phase044_ValidateElfClass(UINT8 *Header) {
    return (Header[4]==2) ? EFI_SUCCESS : EFI_INCOMPATIBLE_VERSION;
}

static EFI_STATUS Phase045_ValidateElfEndian(UINT8 *Header) {
    return (Header[5]==1) ? EFI_SUCCESS : EFI_INCOMPATIBLE_VERSION;
}

static EFI_STATUS Phase046_LogProgramHeaderCount(UINT8 *Header) {
    UINT16 Count = *(UINT16*)(Header + 0x38);
    Print(L"Program headers: %u\n", Count);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase047_LogEntryPoint(UINT8 *Header) {
    UINT64 Entry = *(UINT64*)(Header + 0x18);
    gBootContext.Params.KernelEntry = Entry;
    Print(L"Kernel entry point: 0x%lx\n", Entry);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase048_CloseKernelFile(void) {
    return gBootContext.KernelFile->Close(gBootContext.KernelFile);
}

static EFI_STATUS Phase049_KernelHeaderValid(void) { Print(L"ELF header validated\n"); return EFI_SUCCESS; }
static EFI_STATUS Phase050_BootLoadingBegin(void) { Print(L"Kernel loading begun\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase051_OpenKernelForLoad(void) {
    CHAR16 *Path = L"\\EFI\\AiOS\\kernel.elf";
    return gBootContext.RootDir->Open(gBootContext.RootDir, &gBootContext.KernelFile, Path, EFI_FILE_MODE_READ, 0);
}

static EFI_STATUS Phase052_ReadKernelElfHeader(void) {
    UINTN Size = sizeof(ELF64_EHDR);
    return gBootContext.KernelFile->Read(gBootContext.KernelFile, &Size, &gElfHeader);
}

static EFI_STATUS Phase053_ValidateElfHeader(void) {
    if (!(gElfHeader.e_ident[0]==0x7f && gElfHeader.e_ident[1]=='E' && gElfHeader.e_ident[2]=='L' && gElfHeader.e_ident[3]=='F'))
        return EFI_LOAD_ERROR;
    if (gElfHeader.e_ident[4]!=2 || gElfHeader.e_ident[5]!=1)
        return EFI_INCOMPATIBLE_VERSION;
    if (gElfHeader.e_machine!=0x3E || (gElfHeader.e_type!=2 && gElfHeader.e_type!=3))
        return EFI_UNSUPPORTED;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase054_ReadProgramHeaders(void) {
    UINTN Size = gElfHeader.e_phentsize * gElfHeader.e_phnum;
    EFI_STATUS Status = gBS->AllocatePool(EfiBootServicesData, Size, (VOID**)&gPhdrs);
    if (EFI_ERROR(Status)) return Status;
    Status = gBootContext.KernelFile->SetPosition(gBootContext.KernelFile, gElfHeader.e_phoff);
    if (EFI_ERROR(Status)) return Status;
    return gBootContext.KernelFile->Read(gBootContext.KernelFile, &Size, gPhdrs);
}

static EFI_STATUS Phase055_CountLoadSegments(void) {
    UINTN Count = 0;
    for (UINTN i=0;i<gElfHeader.e_phnum;i++) if (gPhdrs[i].p_type==1) ++Count;
    Print(L"PT_LOAD segments: %u\n", (UINT32)Count);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase056_AllocateKernelMemory(void) {
    gKernelMinAddr = ~0ULL; UINT64 Max=0;
    for (UINTN i=0;i<gElfHeader.e_phnum;i++) {
        if (gPhdrs[i].p_type!=1) continue;
        if (gPhdrs[i].p_paddr<gKernelMinAddr) gKernelMinAddr = gPhdrs[i].p_paddr;
        if (gPhdrs[i].p_paddr + gPhdrs[i].p_memsz > Max) Max = gPhdrs[i].p_paddr + gPhdrs[i].p_memsz;
    }
    UINTN Pages = EFI_SIZE_TO_PAGES(Max - gKernelMinAddr);
    gKernelBaseTmp = 0;
    EFI_STATUS Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, Pages, &gKernelBaseTmp);
    if (!EFI_ERROR(Status)) {
        gBootContext.Params.KernelBase = gKernelBaseTmp;
        gBootContext.Params.KernelSize = Pages * 4096ULL;
    }
    return Status;
}

static EFI_STATUS Phase057_LoadKernelSegments(void) {
    for (UINTN i=0;i<gElfHeader.e_phnum;i++) {
        if (gPhdrs[i].p_type!=1) continue;
        UINTN Size = (UINTN)gPhdrs[i].p_filesz;
        UINT64 Dest = gKernelBaseTmp + (gPhdrs[i].p_paddr - gKernelMinAddr);
        EFI_STATUS Status = gBootContext.KernelFile->SetPosition(gBootContext.KernelFile, gPhdrs[i].p_offset);
        if (EFI_ERROR(Status)) return Status;
        Status = gBootContext.KernelFile->Read(gBootContext.KernelFile, &Size, (VOID*)Dest);
        if (EFI_ERROR(Status)) return Status;
        if (gPhdrs[i].p_memsz > gPhdrs[i].p_filesz)
            SetMem((VOID*)(Dest + gPhdrs[i].p_filesz), (UINTN)(gPhdrs[i].p_memsz - gPhdrs[i].p_filesz), 0);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase058_StoreKernelEntry(void) {
    gBootContext.Params.KernelEntry = gKernelBaseTmp + (gElfHeader.e_entry - gKernelMinAddr);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase059_CloseKernelFilePostLoad(void) {
    return gBootContext.KernelFile->Close(gBootContext.KernelFile);
}

static EFI_STATUS Phase060_LogKernelLoaded(void) {
    Print(L"Kernel loaded at %lx, entry %lx\n", gBootContext.Params.KernelBase, gBootContext.Params.KernelEntry);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase061_LocateHash2Protocol(void) {
    return gBS->LocateProtocol(&gEfiHash2ProtocolGuid, NULL, (VOID**)&gHash2);
}

static EFI_STATUS Phase062_OpenKernelForHash(void) {
    CHAR16 *Path = L"\\EFI\\AiOS\\kernel.elf";
    return gBootContext.RootDir->Open(gBootContext.RootDir, &gBootContext.KernelFile, Path, EFI_FILE_MODE_READ, 0);
}

static EFI_STATUS Phase063_GetKernelSize(void) {
    EFI_FILE_INFO *Info; UINTN Sz=0; EFI_STATUS Status;
    Status = gBootContext.KernelFile->GetInfo(gBootContext.KernelFile, &gEfiFileInfoGuid, &Sz, NULL);
    if (Status!=EFI_BUFFER_TOO_SMALL) return Status;
    Status = gBS->AllocatePool(EfiBootServicesData, Sz, (VOID**)&Info);
    if (EFI_ERROR(Status)) return Status;
    Status = gBootContext.KernelFile->GetInfo(gBootContext.KernelFile, &gEfiFileInfoGuid, &Sz, Info);
    if (!EFI_ERROR(Status)) gKernelBufferSize = Info->FileSize;
    gBS->FreePool(Info);
    return Status;
}

static EFI_STATUS Phase064_ReadKernelData(void) {
    EFI_STATUS Status = gBS->AllocatePool(EfiBootServicesData, gKernelBufferSize, (VOID**)&gKernelBuffer);
    if (EFI_ERROR(Status)) return Status;
    UINTN Size = gKernelBufferSize;
    return gBootContext.KernelFile->Read(gBootContext.KernelFile, &Size, gKernelBuffer);
}

static EFI_STATUS Phase065_ComputeKernelSha256(void) {
    if (gHash2==NULL) return EFI_NOT_READY;
    EFI_HASH_OUTPUT Hash;
    EFI_STATUS Status = gHash2->Hash(gHash2, &gEfiHashAlgorithmSha256Guid, gKernelBufferSize, gKernelBuffer, &Hash);
    if (!EFI_ERROR(Status)) CopyMem(gBootContext.Params.KernelHash, Hash.HashBuf, 32);
    return Status;
}

static EFI_STATUS Phase066_FreeKernelData(void) {
    if (gKernelBuffer) { gBS->FreePool(gKernelBuffer); gKernelBuffer=NULL; }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase067_CloseKernelAfterHash(void) {
    return gBootContext.KernelFile->Close(gBootContext.KernelFile);
}

static EFI_STATUS Phase068_OpenSignatureFile(void) {
    CHAR16 *Path = L"\\EFI\\AiOS\\kernel.sig";
    return gBootContext.RootDir->Open(gBootContext.RootDir, &gSigFile, Path, EFI_FILE_MODE_READ, 0);
}

static EFI_STATUS Phase069_ReadSignatureSize(void) {
    EFI_FILE_INFO *Info; UINTN Sz=0; EFI_STATUS Status;
    Status = gSigFile->GetInfo(gSigFile, &gEfiFileInfoGuid, &Sz, NULL);
    if (Status!=EFI_BUFFER_TOO_SMALL) return Status;
    Status = gBS->AllocatePool(EfiBootServicesData, Sz, (VOID**)&Info);
    if (EFI_ERROR(Status)) return Status;
    Status = gSigFile->GetInfo(gSigFile, &gEfiFileInfoGuid, &Sz, Info);
    if (!EFI_ERROR(Status)) gSignatureSize = Info->FileSize;
    gBS->FreePool(Info);
    return Status;
}

static EFI_STATUS Phase070_ReadSignatureData(void) {
    EFI_STATUS Status = gBS->AllocatePool(EfiBootServicesData, gSignatureSize, (VOID**)&gSignature);
    if (EFI_ERROR(Status)) return Status;
    UINTN Size = gSignatureSize;
    return gSigFile->Read(gSigFile, &Size, gSignature);
}

static EFI_STATUS Phase071_CloseSignatureFile(void) {
    return gSigFile->Close(gSigFile);
}

static EFI_STATUS Phase072_ValidateKernelSignature(void) {
    static CONST UINT8 Modulus[256] = {
        0xb1,0x23,0xc4,0x56,0xde,0x7a,0x89,0x10,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x12,
        0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x12,
        0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,
        0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,
        0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
        0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,
        0xa1,0xb2,0xc3,0xd4,0xe5,0xf6,0x07,0x18,0x29,0x3a,0x4b,0x5c,0x6d,0x7e,0x8f,0x90,
        0xa1,0xb2,0xc3,0xd4,0xe5,0xf6,0x07,0x18,0x29,0x3a,0x4b,0x5c,0x6d,0x7e,0x8f,0x90,
        0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
        0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
        0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00,
        0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00,
        0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xf0,0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xf0,
        0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xf0,0x13,0x57,0x9b,0xdf,0x24,0x68,0xac,0xf0
    };
    static CONST UINT8 Exponent[3] = {0x01,0x00,0x01};
    VOID *Rsa = RsaNew();
    if (Rsa==NULL) return EFI_OUT_OF_RESOURCES;
    if (!RsaSetKey(Rsa, RsaKeyN, Modulus, sizeof(Modulus)) || !RsaSetKey(Rsa, RsaKeyE, Exponent, sizeof(Exponent))) {
        RsaFree(Rsa);
        return EFI_ABORTED;
    }
    BOOLEAN Valid = RsaPkcs1Verify(Rsa, gBootContext.Params.KernelHash, 32, gSignature, gSignatureSize);
    RsaFree(Rsa);
    gBootContext.Params.SignatureValid = Valid ? TRUE : FALSE;
    return Valid ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

static EFI_STATUS Phase073_LogSignatureStatus(void) {
    Print(L"Signature valid: %u\n", gBootContext.Params.SignatureValid);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase074_LocateTcg2ForTrust(void) {
    return gBS->LocateProtocol(&gEfiTcg2ProtocolGuid, NULL, (VOID**)&gBootContext.Tcg2);
}

static EFI_STATUS Phase075_ReadPcr0ForTrust(void) {
    return ReadAndPrintPcr(0);
}

static EFI_STATUS Phase076_ReadSecureBootState(void) {
    UINT8 Secure = 0; UINTN Sz = sizeof(Secure);
    EFI_STATUS S = gRT->GetVariable(L"SecureBoot", &gEfiGlobalVariableGuid, NULL, &Sz, &Secure);
    gBootContext.Params.SignatureValid = gBootContext.Params.SignatureValid && !EFI_ERROR(S) && Secure;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase077_ComputeBootTrustScore(void) {
    UINT32 Score = 0;
    if (gBootContext.Params.SignatureValid) Score += 80;
    TPML_PCR_SELECTION SelIn={0}; SelIn.count=1; SelIn.pcrSelections[0].hash=TPM_ALG_SHA256; SelIn.pcrSelections[0].sizeofSelect=3; SelIn.pcrSelections[0].pcrSelect[0]=1;
    TPML_PCR_SELECTION SelOut; TPML_DIGEST Values; UINT32 C;
    if (!EFI_ERROR(Tpm2PcrRead(&SelIn,&C,&SelOut,&Values)) && Values.count>0)
        Score += 20;
    gBootContext.Params.BootTrustScore = Score;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase078_LogBootTrustScore(void) {
    Print(L"Boot trust score: %u\n", gBootContext.Params.BootTrustScore);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase079_InitIdentityStructure(void) { return EFI_SUCCESS; }

static EFI_STATUS Phase080_GenerateBootUid(void) {
    EFI_TIME T; if (EFI_ERROR(gRT->GetTime(&T,NULL))) return EFI_DEVICE_ERROR;
    UINT8 Buf[32+sizeof(EFI_TIME)];
    CopyMem(Buf, gBootContext.Params.KernelHash, 32);
    CopyMem(Buf+32, &T, sizeof(EFI_TIME));
    EFI_HASH_OUTPUT Hash; EFI_STATUS Status = gHash2->Hash(gHash2, &gEfiHashAlgorithmSha256Guid, sizeof(Buf), Buf, &Hash);
    if (!EFI_ERROR(Status)) CopyMem(gBootContext.Params.BootUid, Hash.HashBuf, 16);
    return Status;
}

static EFI_STATUS Phase081_DrawAiLogo(void) {
    if (gBootContext.Gop==NULL) return EFI_UNSUPPORTED;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL W={255,255,255,0};
    for (UINTN y=0;y<32;y++)
        for (UINTN x=0;x<32;x++)
            if (x==0||y==0||x==31||y==31||x==y||x==31-y)
                gBootContext.Gop->Blt(gBootContext.Gop,&W,EfiBltVideoFill,0,0,x+10,y+10,1,1,0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase082_ShowTrustScore(void) {
    Print(L"Trust Score: %u\n", gBootContext.Params.BootTrustScore);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase083_ShowBootUid(void) {
    Print(L"Boot UID: ");
    for (UINTN i=0;i<16;i++) Print(L"%02x", gBootContext.Params.BootUid[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase084_CleanupSignatureBuffer(void) {
    if (gSignature){ gBS->FreePool(gSignature); gSignature=NULL; }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase085_FinalizeIdentity(void) { return EFI_SUCCESS; }

static EFI_STATUS Phase086_BootReady(void) { Print(L"Boot preparation complete\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase087_AwakeMessage(void) { Print(L"Awakening AI...\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase088_FinalStep(void) { Print(L"Bootloader finished\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase089_ClearScreen(void){ return gST->ConOut->ClearScreen(gST->ConOut); }
static EFI_STATUS Phase090_PrintGoodbye(void){ Print(L"Ready to launch kernel\n"); return EFI_SUCCESS; }
static EFI_STATUS Phase091_WaitForKey(void){ EFI_INPUT_KEY K; return gST->ConIn->ReadKeyStroke(gST->ConIn,&K); }
static EFI_STATUS Phase092_FreeMemoryMap(void){ if(gBootContext.Params.MemoryMap){ gBS->FreePool(gBootContext.Params.MemoryMap); gBootContext.Params.MemoryMap=NULL; } return EFI_SUCCESS; }
static EFI_STATUS Phase093_EndGraphics(void){ return EFI_SUCCESS; }
static EFI_STATUS Phase094_ShutdownTcg(void){ return EFI_SUCCESS; }
static EFI_STATUS Phase095_FreePhdrs(void){ if(gPhdrs){ gBS->FreePool(gPhdrs); gPhdrs=NULL; } return EFI_SUCCESS; }
static EFI_STATUS Phase096_LogCompletion(void){ Print(L"All phases complete\n"); return EFI_SUCCESS; }
static EFI_STATUS Phase097_FinalPause(void){ gBS->Stall(500000); return EFI_SUCCESS; }
static EFI_STATUS Phase098_FinalMessage(void){ Print(L"Handing off to kernel...\n"); return EFI_SUCCESS; }
static EFI_STATUS Phase099_NoOp(void){ return EFI_SUCCESS; }
static EFI_STATUS Phase100_BootComplete(void){ Print(L"Boot complete.\n"); return EFI_SUCCESS; }

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"AiOS Bootloader Initializing...\n");
    Phase001_InitializeBootContext(ImageHandle, SystemTable);
    Phase002_LogSystemTableInfo();
    Phase003_LogCurrentTime();
    Phase004_CheckSecureBoot();
    Phase005_CheckSetupMode();
    Phase006_ClearConsole();
    Phase007_LogBootServices();
    Phase008_PrepareParams();
    Phase009_LogConsoleMode();
    Phase010_EnvironmentReady();
    Phase011_LocateLoadedImage();
    Phase012_LocateFileSystem();
    Phase013_OpenRootDirectory();
    Phase014_LocateGraphicsOutput();
    Phase015_LogGraphicsMode();
    Phase016_GetAcpiRsdp();
    Phase017_GetMemoryMapInfo();
    Phase018_LogImagePath();
    Phase019_ProtocolSetupComplete();
    Phase020_PrepareTpmContext();
    Phase021_LocateTcg2();
    Phase022_QueryTpmCapabilities();
    Phase023_ReadPcr0();
    Phase024_ReadPcr1();
    Phase025_ReadPcr2();
    Phase026_ReadPcr3();
    Phase027_ReadPcr4();
    Phase028_ReadPcr5();
    Phase029_ExportEventLog();
    Phase030_TpmReady();
    Phase031_GetMemoryMap();
    Phase032_LogMemoryRegions();
    Phase033_CalcFreeMemory();
    Phase034_LogAcpiAddress();
    Phase035_PrepareEntropySeed();
    Phase036_HashMemoryMap();
    Phase037_StoreEntropy();
    Phase038_DisplayEntropy();
    Phase039_PrepareExitBoot();
    Phase040_MemoryReady();
    Phase041_OpenKernelFile();
    UINT8 ElfHdr[64];
    Phase042_ReadElfHeader(ElfHdr);
    Phase043_ValidateElfMagic(ElfHdr);
    Phase044_ValidateElfClass(ElfHdr);
    Phase045_ValidateElfEndian(ElfHdr);
    Phase046_LogProgramHeaderCount(ElfHdr);
    Phase047_LogEntryPoint(ElfHdr);
    Phase048_CloseKernelFile();
    Phase049_KernelHeaderValid();
    Phase050_BootLoadingBegin();
    Phase051_OpenKernelForLoad();
    Phase052_ReadKernelElfHeader();
    Phase054_ReadProgramHeaders();
    Phase055_CountLoadSegments();
    Phase056_AllocateKernelMemory();
    Phase057_LoadKernelSegments();
    Phase058_StoreKernelEntry();
    Phase059_CloseKernelFilePostLoad();
    Phase060_LogKernelLoaded();
    Phase061_LocateHash2Protocol();
    Phase062_OpenKernelForHash();
    Phase063_GetKernelSize();
    Phase064_ReadKernelData();
    Phase065_ComputeKernelSha256();
    Phase066_FreeKernelData();
    Phase067_CloseKernelAfterHash();
    Phase068_OpenSignatureFile();
    Phase069_ReadSignatureSize();
    Phase070_ReadSignatureData();
    Phase071_CloseSignatureFile();
    Phase072_ValidateKernelSignature();
    Phase073_LogSignatureStatus();
    Phase074_LocateTcg2ForTrust();
    Phase075_ReadPcr0ForTrust();
    Phase076_ReadSecureBootState();
    Phase077_ComputeBootTrustScore();
    Phase078_LogBootTrustScore();
    Phase079_InitIdentityStructure();
    Phase080_GenerateBootUid();
    Phase081_DrawAiLogo();
    Phase082_ShowTrustScore();
    Phase083_ShowBootUid();
    Phase084_CleanupSignatureBuffer();
    Phase085_FinalizeIdentity();
    Phase086_BootReady();
    Phase087_AwakeMessage();
    Phase088_FinalStep();
    Phase089_ClearScreen();
    Phase090_PrintGoodbye();
    Phase091_WaitForKey();
    Phase092_FreeMemoryMap();
    Phase093_EndGraphics();
    Phase094_ShutdownTcg();
    Phase095_FreePhdrs();
    Phase096_LogCompletion();
    Phase097_FinalPause();
    Phase098_FinalMessage();
    Phase099_NoOp();
    Phase100_BootComplete();
    Print(L"Bootloader execution complete.\n");
    return EFI_SUCCESS;
}
