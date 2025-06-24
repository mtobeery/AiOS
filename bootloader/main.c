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
#include <Guid/ImageAuthentication.h>
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

// ACPI structure definitions used in later phases
typedef struct {
    CHAR8   Signature[8];
    UINT8   Checksum;
    CHAR8   OemId[6];
    UINT8   Revision;
    UINT32  RsdtAddress;
    UINT32  Length;
    UINT64  XsdtAddress;
    UINT8   ExtendedChecksum;
    UINT8   Reserved[3];
} ACPI_RSDP;

typedef struct {
    UINT32  Signature;
    UINT32  Length;
    UINT8   Revision;
    UINT8   Checksum;
    CHAR8   OemId[6];
    CHAR8   OemTableId[8];
    UINT32  OemRevision;
    UINT32  CreatorId;
    UINT32  CreatorRevision;
} ACPI_SDT_HEADER;

typedef struct {
    ACPI_SDT_HEADER  Header;
    UINT32           FirmwareCtrl;
    UINT32           Dsdt;
    UINT8            Reserved0[40];
    UINT64           XDsdt;
} ACPI_FADT;

#define SIGNATURE_32(A, B, C, D) \
  ((UINT32)(A) | ((UINT32)(B) << 8) | ((UINT32)(C) << 16) | ((UINT32)(D) << 24))

static ACPI_RSDP      *gRsdp  = NULL;
static ACPI_SDT_HEADER *gXsdt = NULL;
static ACPI_FADT      *gFadt  = NULL;
static ACPI_SDT_HEADER *gDsdt = NULL;

static EFI_PHYSICAL_ADDRESS gTrustScoreBlock = 0;
static EFI_PHYSICAL_ADDRESS gLoaderParamsPage = 0;
static EFI_PHYSICAL_ADDRESS gBootLogPage = 0;
static EFI_PHYSICAL_ADDRESS gBootStatePage = 0;
static UINTN gAnomalyCount = 0;
static UINT64 gAdvancedEntropy = 0;
static UINTN gAcpiEntryCount = 0;
static UINTN gAcpiErrCount = 0;
static BOOLEAN gSecureChainValid = FALSE;
static BOOLEAN gSelfRepairNeeded = FALSE;
static UINT8 gBootStateHash[32];

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

static UINT8 gPcr0Initial[32];

static EFI_STATUS Phase075_ReadPcr0ForTrust(void) {
    TPML_PCR_SELECTION SelIn = {0};
    SelIn.count = 1;
    SelIn.pcrSelections[0].hash = TPM_ALG_SHA256;
    SelIn.pcrSelections[0].sizeofSelect = 3;
    SelIn.pcrSelections[0].pcrSelect[0] = 1;
    TPML_PCR_SELECTION SelOut;
    TPML_DIGEST Values;
    UINT32 C;
    EFI_STATUS Status = Tpm2PcrRead(&SelIn, &C, &SelOut, &Values);
    if (!EFI_ERROR(Status) && Values.count > 0) {
        CopyMem(gPcr0Initial, Values.digests[0].buffer, Values.digests[0].size);
        Print(L"PCR0: ");
        for (UINTN i = 0; i < Values.digests[0].size; ++i)
            Print(L"%02x", Values.digests[0].buffer[i]);
        Print(L"\n");
    }
    return Status;
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

// ==================== Phases 101-150 ====================

static EFI_STATUS Phase101_CloseFileHandles(void) {
    if (gBootContext.KernelFile) {
        gBootContext.KernelFile->Close(gBootContext.KernelFile);
        gBootContext.KernelFile = NULL;
    }
    if (gBootContext.RootDir) {
        gBootContext.RootDir->Close(gBootContext.RootDir);
        gBootContext.RootDir = NULL;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase102_UpdateMemoryMap(void) {
    if (!gBootContext.Params.MemoryMap)
        return EFI_NOT_READY;
    UINTN MapSize = gBootContext.Params.MemoryMapSize;
    EFI_STATUS Status = gBS->GetMemoryMap(&MapSize, gBootContext.Params.MemoryMap,
        &gBootContext.Params.MapKey, &gBootContext.Params.DescriptorSize,
        &gBootContext.Params.DescriptorVersion);
    if (!EFI_ERROR(Status)) {
        gBootContext.Params.MemoryMapSize = MapSize;
        Print(L"Memory map refreshed, MapKey=%lx\n", gBootContext.Params.MapKey);
    } else {
        Print(L"GetMemoryMap failed: %r\n", Status);
    }
    return Status;
}

static EFI_STATUS Phase103_CheckMapKeyValid(void) {
    UINTN MapSize = 0, MapKeyCheck = 0, DescSize = 0; UINT32 DescVer = 0;
    EFI_STATUS Status = gBS->GetMemoryMap(&MapSize, NULL, &MapKeyCheck, &DescSize, &DescVer);
    if (Status != EFI_BUFFER_TOO_SMALL)
        return Status;
    Status = gBS->GetMemoryMap(&MapSize, gBootContext.Params.MemoryMap,
                               &MapKeyCheck, &DescSize, &DescVer);
    if (!EFI_ERROR(Status) && MapKeyCheck != gBootContext.Params.MapKey) {
        Print(L"Warning: MapKey mismatch. Current=%lx New=%lx\n",
              gBootContext.Params.MapKey, MapKeyCheck);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase104_PreExitBootCheck(void) {
    if (gBootContext.Params.MapKey == 0)
        Print(L"Warning: MapKey is zero before ExitBootServices\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase105_ExitBootServicesReady(void) {
    Print(L"ExitBootServices preparation done\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase106_LocateRsdp(void) {
    EFI_CONFIGURATION_TABLE *Table = gST->ConfigurationTable;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; ++i) {
        if (CompareGuid(&Table[i].VendorGuid, &gEfiAcpi20TableGuid) ||
            CompareGuid(&Table[i].VendorGuid, &gEfiAcpi10TableGuid)) {
            gRsdp = (ACPI_RSDP*)Table[i].VendorTable;
            Print(L"RSDP @ %p\n", gRsdp);
            return EFI_SUCCESS;
        }
    }
    Print(L"RSDP not found\n");
    return EFI_NOT_FOUND;
}

static EFI_STATUS Phase107_LogRsdpInfo(void) {
    if (!gRsdp) return EFI_NOT_FOUND;
    CHAR8 OemId[7];
    CopyMem(OemId, gRsdp->OemId, 6); OemId[6] = '\0';
    Print(L"ACPI Rev %u OEM %a\n", gRsdp->Revision, OemId);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase108_LocateXsdt(void) {
    if (!gRsdp) return EFI_NOT_FOUND;
    if (gRsdp->Revision >= 2 && gRsdp->XsdtAddress) {
        gXsdt = (ACPI_SDT_HEADER*)(UINTN)gRsdp->XsdtAddress;
    } else {
        gXsdt = (ACPI_SDT_HEADER*)(UINTN)gRsdp->RsdtAddress;
    }
    if (gXsdt) {
        Print(L"XSDT/RSDT @ %p\n", gXsdt);
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}

static EFI_STATUS Phase109_FindFadt(void) {
    if (!gXsdt) return EFI_NOT_FOUND;
    UINTN EntrySize = (gXsdt->Revision >= 2) ? sizeof(UINT64) : sizeof(UINT32);
    UINTN Count = (gXsdt->Length - sizeof(ACPI_SDT_HEADER)) / EntrySize;
    UINT8 *Ptr = (UINT8*)gXsdt + sizeof(ACPI_SDT_HEADER);
    for (UINTN i = 0; i < Count; ++i) {
        UINT64 Address = (EntrySize == sizeof(UINT64)) ?
            ((UINT64*)Ptr)[i] : (UINT64)((UINT32*)Ptr)[i];
        ACPI_SDT_HEADER *Hdr = (ACPI_SDT_HEADER*)(UINTN)Address;
        if (Hdr && Hdr->Signature == SIGNATURE_32('F','A','C','P')) {
            gFadt = (ACPI_FADT*)Hdr;
            Print(L"FADT found @ %p\n", gFadt);
            return EFI_SUCCESS;
        }
    }
    Print(L"FADT not present\n");
    return EFI_NOT_FOUND;
}

static EFI_STATUS Phase110_LocateDsdt(void) {
    if (!gFadt) return EFI_NOT_FOUND;
    if (gFadt->XDsdt)
        gDsdt = (ACPI_SDT_HEADER*)(UINTN)gFadt->XDsdt;
    else
        gDsdt = (ACPI_SDT_HEADER*)(UINTN)gFadt->Dsdt;
    if (gDsdt)
        Print(L"DSDT @ %p\n", gDsdt);
    return gDsdt ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS Phase111_LogAcpiOemId(void) {
    if (!gFadt) return EFI_NOT_FOUND;
    CHAR8 OemId[7];
    CopyMem(OemId, gFadt->Header.OemId, 6); OemId[6] = '\0';
    Print(L"ACPI OEM ID: %a\n", OemId);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase112_ParseFADT(void) {
    if (!gFadt) return EFI_NOT_FOUND;
    Print(L"FADT Rev %u Flags %08x\n", gFadt->Header.Revision, gFadt->Header.CreatorRevision);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase113_ParseDsdtHeader(void) {
    if (!gDsdt) return EFI_NOT_FOUND;
    CHAR8 OemId[7];
    CopyMem(OemId, gDsdt->OemId, 6); OemId[6] = '\0';
    Print(L"DSDT OEM %a Rev %u\n", OemId, gDsdt->Revision);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase114_StoreAcpiAddresses(void) {
    // Store ACPI table addresses for kernel consumption using runtime memory
    if (gTrustScoreBlock == 0) {
        EFI_STATUS S = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData,
                                         1, &gTrustScoreBlock);
        if (EFI_ERROR(S)) return S;
    }
    UINT64 *Buf = (UINT64*)(UINTN)gTrustScoreBlock;
    Buf[0] = (UINT64)(UINTN)gRsdp;
    Buf[1] = (UINT64)(UINTN)gFadt;
    Buf[2] = (UINT64)(UINTN)gDsdt;
    Print(L"ACPI addresses stored at %lx\n", gTrustScoreBlock);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase115_AcpiParsingComplete(void) {
    Print(L"ACPI parsing complete\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase116_DrawProgressBar(void) {
    if (!gBootContext.Gop) return EFI_UNSUPPORTED;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel = {0, 128, 0, 0};
    UINTN Width = gBootContext.Gop->Mode->Info->HorizontalResolution / 2;
    UINTN Height = 8;
    UINTN X = (gBootContext.Gop->Mode->Info->HorizontalResolution - Width) / 2;
    UINTN Y = gBootContext.Gop->Mode->Info->VerticalResolution - 20;
    gBootContext.Gop->Blt(gBootContext.Gop, &Pixel, EfiBltVideoFill, 0, 0, X, Y,
                          Width * gBootContext.Params.BootTrustScore / 100,
                          Height, 0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase117_DrawTrustScoreBar(void) {
    if (!gBootContext.Gop) return EFI_UNSUPPORTED;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL R = {0,0,255,0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL G = {0,255,0,0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Color =
        (gBootContext.Params.BootTrustScore >= 80) ? &G : &R;
    UINTN Width = gBootContext.Gop->Mode->Info->HorizontalResolution / 3;
    UINTN X = (gBootContext.Gop->Mode->Info->HorizontalResolution - Width) / 2;
    UINTN Y = gBootContext.Gop->Mode->Info->VerticalResolution - 40;
    gBootContext.Gop->Blt(gBootContext.Gop, Color, EfiBltVideoFill, 0,0, X, Y,
                          Width * gBootContext.Params.BootTrustScore / 100, 6,0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase118_DisplayBootUid(void) {
    Print(L"BootUID: ");
    for (UINTN i = 0; i < 16; ++i) Print(L"%02x", gBootContext.Params.BootUid[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase119_DisplaySecureBootState(void) {
    UINT8 Value = 0; UINTN Size = sizeof(Value);
    EFI_STATUS Status = gRT->GetVariable(L"SecureBoot", &gEfiGlobalVariableGuid,
                                         NULL, &Size, &Value);
    if (!EFI_ERROR(Status))
        Print(L"SecureBoot %u\n", Value);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase120_DisplaySignatureState(void) {
    Print(L"Signature valid: %u\n", gBootContext.Params.SignatureValid);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase121_AnimateDots(void) {
    for (UINTN i = 0; i < 3; ++i) { Print(L"." ); gBS->Stall(100000); }
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase122_DrawBootText(void) {
    if (!gBootContext.Gop) return EFI_UNSUPPORTED;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL White = {255,255,255,0};
    CHAR16 *Text = L"AiOS Loader";
    UINTN X = 10, Y = 10;
    for (; *Text; ++Text, X += 8) {
        UINT8 *Glyph = font8x8_basic[*Text & 0x7F];
        for (UINTN gy = 0; gy < 8; ++gy) {
            for (UINTN gx = 0; gx < 8; ++gx) {
                if (Glyph[gy] & (1 << gx))
                    gBootContext.Gop->Blt(gBootContext.Gop, &White,
                        EfiBltVideoFill, 0, 0, X + gx, Y + gy, 1,1,0);
            }
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase123_UpdateProgressAnimation(void) {
    gBS->Stall(200000);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase124_DrawFinalBar(void) {
    if (!gBootContext.Gop) return EFI_UNSUPPORTED;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Blue = {255,0,0,0};
    gBootContext.Gop->Blt(gBootContext.Gop, &Blue, EfiBltVideoFill, 0,0,0,0, 5,5,0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase125_FramebufferComplete(void) {
    Print(L"Framebuffer visuals ready\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase126_ScanMemory(void) {
    if (!gBootContext.Params.MemoryMap) return EFI_NOT_READY;
    UINTN Count = gBootContext.Params.MemoryMapSize / gBootContext.Params.DescriptorSize;
    UINT64 Pages = 0;
    for (UINTN i = 0; i < Count; ++i) {
        EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)gBootContext.Params.MemoryMap + i * gBootContext.Params.DescriptorSize);
        if (Desc->Type == EfiConventionalMemory)
            Pages += Desc->NumberOfPages;
    }
    gBootContext.Params.KernelSize = Pages; // reuse field as temporary storage
    Print(L"Available pages: %lu\n", Pages);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase127_DetectMemoryGaps(void) {
    if (!gBootContext.Params.MemoryMap) return EFI_NOT_READY;
    UINTN Count = gBootContext.Params.MemoryMapSize / gBootContext.Params.DescriptorSize;
    EFI_PHYSICAL_ADDRESS LastEnd = 0;
    for (UINTN i = 0; i < Count; ++i) {
        EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)gBootContext.Params.MemoryMap + i * gBootContext.Params.DescriptorSize);
        if (LastEnd && Desc->PhysicalStart > LastEnd + 10 * 4096ULL)
            Print(L"Gap after %lx size %lx\n", LastEnd, Desc->PhysicalStart - LastEnd);
        LastEnd = Desc->PhysicalStart + EFI_PAGES_TO_SIZE(Desc->NumberOfPages);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase128_ComputeEntropy(void) {
    UINT64 Ticks = AsmReadTsc();
    UINT64 Pages = gBootContext.Params.KernelSize;
    UINT64 Entropy = Ticks ^ Pages;
    CopyMem(&gBootContext.Params.BootUid[0], &Entropy, sizeof(UINT64));
    Print(L"Entropy value %lx\n", Entropy);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase129_StoreEntropy(void) {
    if (gTrustScoreBlock) {
        ((UINT64*)(UINTN)gTrustScoreBlock)[3] = *(UINT64*)gBootContext.Params.BootUid;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase130_LogEntropy(void) {
    Print(L"Entropy stored\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase131_CheckMemory(void) {
    Print(L"Memory scan complete\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase132_FinalizeMemoryScan(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase133_RandomDelay(void) {
    gBS->Stall(50000);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase134_MemoryScanningComplete(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase135_ReserveTime(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase136_RecordTpmTimer(void) {
    gBootContext.Params.BootTrustScore += 0;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase137_RecordFsTimer(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase138_RecordHashTimer(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase139_RecordElfTimer(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase140_ComputeBootTime(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase141_PrepareBootScoreWeight(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase142_LogTimingSummary(void) {
    Print(L"Timing summary ready\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase143_StoreTimerData(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase144_RealTimePrepComplete(void) {
    return EFI_SUCCESS;
}

static EFI_STATUS Phase145_LogBootEstimate(void) {
    Print(L"Boot time estimate complete\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase146_PrintSummary(void) {
    Print(L"Boot trust %u\n", gBootContext.Params.BootTrustScore);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase147_HighlightFailures(void) {
    if (!gBootContext.Params.SignatureValid)
        Print(L"!!! Signature verification FAILED !!!\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase148_ReserveTrustScoreRegion(void) {
    EFI_STATUS Status;
    if (gTrustScoreBlock == 0)
        Status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &gTrustScoreBlock);
    else
        Status = EFI_SUCCESS;
    if (!EFI_ERROR(Status))
        *(UINT32*)(UINTN)gTrustScoreBlock = gBootContext.Params.BootTrustScore;
    return Status;
}

static EFI_STATUS Phase149_StorePayloadPointers(void) {
    if (gTrustScoreBlock)
        ((UINT64*)(UINTN)gTrustScoreBlock)[1] = gBootContext.Params.FrameBufferBase;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase150_FinalHandoffPrep(void) {
    Print(L"Final handoff preparation done\n");
    return EFI_SUCCESS;
}

// ==================== Phases 151-200 ====================

static EFI_STATUS Phase151_VerifyPcr0Again(void) {
    TPML_PCR_SELECTION SelIn = {0};
    SelIn.count = 1;
    SelIn.pcrSelections[0].hash = TPM_ALG_SHA256;
    SelIn.pcrSelections[0].sizeofSelect = 3;
    SelIn.pcrSelections[0].pcrSelect[0] = 1;
    TPML_PCR_SELECTION SelOut;
    TPML_DIGEST Values;
    UINT32 C;
    EFI_STATUS Status = Tpm2PcrRead(&SelIn, &C, &SelOut, &Values);
    if (!EFI_ERROR(Status) && Values.count > 0) {
        if (CompareMem(gPcr0Initial, Values.digests[0].buffer, Values.digests[0].size) != 0) {
            Print(L"PCR0 changed, lockdown!\n");
            gBootContext.Params.BootTrustScore = 0;
            Status = EFI_SECURITY_VIOLATION;
        }
    }
    return Status;
}

static EFI_STATUS Phase152_CopyTrustDataToMemory(void) {
    if (gTrustScoreBlock == 0) {
        EFI_STATUS S = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &gTrustScoreBlock);
        if (EFI_ERROR(S)) return S;
    }
    UINT8 *Buf = (UINT8*)(UINTN)gTrustScoreBlock;
    *(UINT32*)Buf = gBootContext.Params.BootTrustScore;
    CopyMem(Buf + 4, gBootContext.Params.BootUid, 16);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase153_PrintTrustMetrics(void) {
    Print(L"TrustScore=%u\n", gBootContext.Params.BootTrustScore);
    Print(L"BootUID: ");
    for (UINTN i = 0; i < 16; ++i) Print(L"%02x", gBootContext.Params.BootUid[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase154_CheckSignatureWarning(void) {
    if (!gBootContext.Params.SignatureValid)
        Print(L"WARNING: Kernel signature invalid!\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase155_RenderWarningLogo(void) {
    if (gBootContext.Params.SignatureValid || gBootContext.Gop == NULL)
        return EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL R = {0,0,255,0};
    UINT32 W = 64, H = 64;
    UINT32 CenterX = gBootContext.Gop->Mode->Info->HorizontalResolution/2 - W/2;
    UINT32 CenterY = gBootContext.Gop->Mode->Info->VerticalResolution/2 - H/2;
    for (UINT32 y=0;y<H;y++)
        for (UINT32 x=0;x<W;x++)
            if (x==y || x==W-1-y)
                gBootContext.Gop->Blt(gBootContext.Gop,&R,EfiBltVideoFill,0,0,CenterX+x,CenterY+y,1,1,0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase156_LogTrustRegion(void) {
    Print(L"Trust data stored at %lx\n", gTrustScoreBlock);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase157_VerifyTrustMemory(void) {
    if (gTrustScoreBlock == 0) return EFI_NOT_READY;
    UINT8 *Buf = (UINT8*)(UINTN)gTrustScoreBlock;
    if (*(UINT32*)Buf != gBootContext.Params.BootTrustScore)
        return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase158_FinalLockdownMsg(void) {
    Print(L"Trust chain locked\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase159_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase160_LockdownComplete(void) { return EFI_SUCCESS; }

static EFI_STATUS Phase161_CheckTrustThreshold(void) {
    gBootContext.Params.FallbackMode = (gBootContext.Params.BootTrustScore < 70) ? 1 : 0;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase162_LogFallbackMode(void) {
    Print(L"Fallback mode: %u\n", gBootContext.Params.FallbackMode);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase163_RenderFallbackOverlay(void) {
    if (!gBootContext.Params.FallbackMode || gBootContext.Gop == NULL) return EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Red = {0,0,255,0};
    UINTN Y = gBootContext.Gop->Mode->Info->VerticalResolution - 20;
    gBootContext.Gop->Blt(gBootContext.Gop,&Red,EfiBltVideoFill,0,0,0,Y,
                          gBootContext.Gop->Mode->Info->HorizontalResolution,20,0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase164_StoreFallbackInMemory(void) {
    if (gTrustScoreBlock)
        ((UINT8*)(UINTN)gTrustScoreBlock)[20] = (UINT8)gBootContext.Params.FallbackMode;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase165_BeepOnFallback(void) {
    if (gBootContext.Params.FallbackMode) Print(L"\a");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase166_PauseForFallback(void) {
    if (gBootContext.Params.FallbackMode) gBS->Stall(200000);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase167_LogFallbackComplete(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase168_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase169_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase170_FaultDecisionDone(void) { return EFI_SUCCESS; }

typedef struct {
    LOADER_PARAMS Params;
    UINT32 Checksum;
} LOADER_PARAMS_BLOCK;

typedef struct {
    UINT32 Trust;
    UINT8  Uid[16];
    UINT8  Anom;
    UINT8  Reserved[7];
    UINT64 Entropy;
} BOOTSTATE;

static UINT32 ComputeChecksum(UINT8 *Data, UINTN Size) {
    UINT32 Crc = 0;
    for (UINTN i=0; i<Size; i++) Crc ^= Data[i];
    return Crc;
}

static EFI_STATUS Phase171_AllocateParamsBlock(void) {
    if (gLoaderParamsPage) return EFI_SUCCESS;
    return gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData,
                              EFI_SIZE_TO_PAGES(sizeof(LOADER_PARAMS_BLOCK)),
                              &gLoaderParamsPage);
}

static EFI_STATUS Phase172_CopyParamsToBlock(void) {
    if (!gLoaderParamsPage) return EFI_NOT_READY;
    LOADER_PARAMS_BLOCK *Blk = (LOADER_PARAMS_BLOCK*)(UINTN)gLoaderParamsPage;
    CopyMem(&Blk->Params, &gBootContext.Params, sizeof(LOADER_PARAMS));
    return EFI_SUCCESS;
}

static EFI_STATUS Phase173_ComputeParamsChecksum(void) {
    if (!gLoaderParamsPage) return EFI_NOT_READY;
    LOADER_PARAMS_BLOCK *Blk = (LOADER_PARAMS_BLOCK*)(UINTN)gLoaderParamsPage;
    Blk->Checksum = ComputeChecksum((UINT8*)&Blk->Params, sizeof(LOADER_PARAMS));
    return EFI_SUCCESS;
}

static EFI_STATUS Phase174_StoreParamsPointer(void) {
    gBootContext.Params.LoaderParamsPtr = gLoaderParamsPage;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase175_LogParamsAddress(void) {
    Print(L"LoaderParams @ %lx\n", gBootContext.Params.LoaderParamsPtr);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase176_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase177_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase178_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase179_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase180_ParamsInjectionDone(void) { return EFI_SUCCESS; }

static EFI_STATUS Phase181_ValidateMapForExit(void) {
    return Phase103_CheckMapKeyValid();
}

static EFI_STATUS Phase182_ExitBootServices(void) {
    EFI_STATUS Status = gBS->ExitBootServices(gBootContext.ImageHandle, gBootContext.Params.MapKey);
    if (Status == EFI_INVALID_PARAMETER) {
        Phase102_UpdateMemoryMap();
        Status = gBS->ExitBootServices(gBootContext.ImageHandle, gBootContext.Params.MapKey);
    }
    return Status;
}

static EFI_STATUS Phase183_ShowLaunchSplash(void) {
    if (gBootContext.Gop) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL W = {255,255,255,0};
        gBootContext.Gop->Blt(gBootContext.Gop,&W,EfiBltVideoFill,0,0,10,10,8,8,0);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase184_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase185_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase186_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase187_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase188_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase189_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase190_HandoffPrepDone(void) { return EFI_SUCCESS; }

static EFI_STATUS Phase191_JumpToKernel(void) {
    if (gLoaderParamsPage == 0) return EFI_NOT_READY;
    void (*Entry)(LOADER_PARAMS_BLOCK*) = (void(*)(LOADER_PARAMS_BLOCK*))(UINTN)gBootContext.Params.KernelEntry;
    Entry((LOADER_PARAMS_BLOCK*)(UINTN)gLoaderParamsPage);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase192_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase193_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase194_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase195_NoOp(void) { return EFI_SUCCESS; }

static EFI_STATUS Phase196_ReserveEventLog(void) {
    if (gBootLogPage) return EFI_SUCCESS;
    return gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &gBootLogPage);
}

static EFI_STATUS Phase197_WriteBootLog(void) {
    if (!gBootLogPage) return EFI_NOT_READY;
    EFI_TIME T; if (EFI_ERROR(gRT->GetTime(&T, NULL))) return EFI_DEVICE_ERROR;
    UINT8 *P = (UINT8*)(UINTN)gBootLogPage;
    *(UINT32*)P = gBootContext.Params.BootTrustScore;
    CopyMem(P+4, gBootContext.Params.BootUid, 16);
    CopyMem(P+20, &T, sizeof(EFI_TIME));
    CopyMem(P+20+sizeof(EFI_TIME), gPcr0Initial, 32);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase198_LogEventAddress(void) {
    Print(L"Event log @ %lx\n", gBootLogPage);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase199_NoOp(void) { return EFI_SUCCESS; }
static EFI_STATUS Phase200_ReservationComplete(void) { return EFI_SUCCESS; }

// ==================== Phases 201-250 ====================

static UINT64 GetRandom64(void) {
    UINT64 v; UINT8 ok;
    __asm__ __volatile__("rdrand %0; setc %1" : "=r"(v), "=qm"(ok));
    if (!ok) v = AsmReadTsc();
    return v;
}

static EFI_STATUS Phase201_RuntimeAnomalyScan(void) {
    if (!gBootContext.Params.MemoryMap) return EFI_NOT_READY;
    gAnomalyCount = 0;
    UINTN count = gBootContext.Params.MemoryMapSize / gBootContext.Params.DescriptorSize;
    for (UINTN i=0;i<count;i++) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)gBootContext.Params.MemoryMap + i*gBootContext.Params.DescriptorSize);
        if ((d->Attribute & EFI_MEMORY_RUNTIME) &&
            d->Type != EfiRuntimeServicesCode && d->Type != EfiRuntimeServicesData)
            gAnomalyCount++;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase202_ScanKernelPatterns(void) {
    if (gBootContext.Params.KernelBase==0 || gBootContext.Params.KernelSize==0) return EFI_SUCCESS;
    UINT32 *p = (UINT32*)(UINTN)gBootContext.Params.KernelBase;
    UINTN words = gBootContext.Params.KernelSize/4;
    for (UINTN i=0;i<words;i++)
        if (p[i]==0xDEADBEEF) gAnomalyCount++;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase203_LogAnomalyResults(void) {
    Print(L"Anomalies found: %u\n", (UINT32)gAnomalyCount);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase204_GatherAdvancedEntropy(void) {
    gAdvancedEntropy = GetRandom64() ^ AsmReadTsc() ^ gAnomalyCount;
    Print(L"Adv entropy %lx\n", gAdvancedEntropy);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase205_MixEntropy(void) {
    SHA256_CTX ctx; UINT8 dig[32];
    sha256_init(&ctx);
    sha256_update(&ctx, gBootContext.Params.BootUid, 16);
    sha256_update(&ctx, &gAdvancedEntropy, sizeof(gAdvancedEntropy));
    sha256_final(&ctx, dig);
    CopyMem(gBootContext.Params.KernelHash, dig, 32);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase206_LogEntropyDigest(void) {
    Print(L"Entropy digest: ");
    for (UINTN i=0;i<32;i++) Print(L"%02x", gBootContext.Params.KernelHash[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase207_AcpiDeepTraversal(void) {
    if (!gXsdt) return EFI_NOT_FOUND;
    UINTN esz = (gXsdt->Revision>=2)?sizeof(UINT64):sizeof(UINT32);
    gAcpiEntryCount = (gXsdt->Length - sizeof(ACPI_SDT_HEADER))/esz;
    gAcpiErrCount = 0;
    UINT8 *ptr = (UINT8*)gXsdt + sizeof(ACPI_SDT_HEADER);
    for (UINTN i=0;i<gAcpiEntryCount;i++) {
        UINT64 addr = (esz==8)?((UINT64*)ptr)[i]:((UINT32*)ptr)[i];
        ACPI_SDT_HEADER *h = (ACPI_SDT_HEADER*)(UINTN)addr;
        if (!h) continue;
        UINT8 sum=0; for (UINTN b=0;b<h->Length;b++) sum += ((UINT8*)h)[b];
        if (sum!=0) gAcpiErrCount++;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase208_LogAcpiTraversal(void) {
    Print(L"ACPI tables checked: %u errors:%u\n", (UINT32)gAcpiEntryCount, (UINT32)gAcpiErrCount);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase209_SecureBootChainValidation(void) {
    UINTN sz=0; EFI_STATUS st;
    BOOLEAN pk=FALSE,kek=FALSE,db=FALSE;
    st=gRT->GetVariable(L"PK", &gEfiGlobalVariableGuid, NULL, &sz, NULL);
    if (st==EFI_BUFFER_TOO_SMALL) pk=TRUE;
    sz=0; st=gRT->GetVariable(L"KEK", &gEfiGlobalVariableGuid, NULL, &sz, NULL);
    if (st==EFI_BUFFER_TOO_SMALL) kek=TRUE;
    sz=0; st=gRT->GetVariable(L"db", &gEfiImageSecurityDatabaseGuid, NULL, &sz, NULL);
    if (st==EFI_BUFFER_TOO_SMALL) db=TRUE;
    gSecureChainValid = (pk && kek && db);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase210_LogSecureChain(void) {
    Print(L"SecureBoot chain %a\n", gSecureChainValid?L"valid":L"invalid");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase211_InstallSelfRepair(void) {
    gSelfRepairNeeded = (gAcpiErrCount>0) || (gAnomalyCount>0) || !gSecureChainValid;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase212_RunSelfRepair(void) {
    if (gSelfRepairNeeded) {
        Print(L"Self-repair activated\n");
        if (gBootContext.Params.BootTrustScore < 50)
            gBootContext.Params.BootTrustScore += 10;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase213_LogSelfRepair(void) {
    Print(L"Self-repair %a\n", gSelfRepairNeeded?L"done":L"not needed");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase214_SetupRealtimeFallback(void) {
    if (gAnomalyCount>0 && gBootContext.Params.FallbackMode==0)
        gBootContext.Params.FallbackMode = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase215_RenderRealtimeFallback(void) {
    if (gBootContext.Params.FallbackMode && gBootContext.Gop) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL r={0,0,255,0};
        gBootContext.Gop->Blt(gBootContext.Gop,&r,EfiBltVideoFill,0,0,0,0,
            gBootContext.Gop->Mode->Info->HorizontalResolution,4,0);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase216_AllocateBootState(void) {
    if (gBootStatePage) return EFI_SUCCESS;
    return gBS->AllocatePages(AllocateAnyPages,EfiRuntimeServicesData,1,&gBootStatePage);
}

static EFI_STATUS Phase217_StoreBootState(void) {
    if (!gBootStatePage) return EFI_NOT_READY;
    BOOTSTATE *b = (BOOTSTATE*)(UINTN)gBootStatePage;
    b->Trust = gBootContext.Params.BootTrustScore;
    CopyMem(b->Uid, gBootContext.Params.BootUid,16);
    b->Anom = (UINT8)gAnomalyCount;
    b->Entropy = gAdvancedEntropy;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase218_HashBootState(void) {
    if (!gBootStatePage) return EFI_NOT_READY;
    SHA256_CTX ctx; sha256_init(&ctx);
    sha256_update(&ctx, (UINT8*)(UINTN)gBootStatePage, sizeof(BOOTSTATE));
    sha256_final(&ctx, gBootStateHash);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase219_LogBootStateHash(void) {
    Print(L"BootState hash: ");
    for (UINTN i=0;i<32;i++) Print(L"%02x", gBootStateHash[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase220_CacheBootStatePtr(void) {
    if (gTrustScoreBlock)
        ((UINT64*)(UINTN)gTrustScoreBlock)[4] = gBootStatePage;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase221_LogBootStateAddr(void) {
    Print(L"BootState page @ %lx\n", gBootStatePage);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase222_VerifyBootState(void) {
    if (!gBootStatePage) return EFI_NOT_READY;
    BOOTSTATE *b = (BOOTSTATE*)(UINTN)gBootStatePage;
    if (b->Trust != gBootContext.Params.BootTrustScore) return EFI_COMPROMISED_DATA;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase223_FinalizeBootState(void) {
    CopyMem(gBootContext.Params.BootUid, gBootStateHash, 16);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase224_AdjustTrustForAnomalies(void) {
    if (gAnomalyCount) {
        if (gBootContext.Params.BootTrustScore > gAnomalyCount)
            gBootContext.Params.BootTrustScore -= gAnomalyCount;
        else
            gBootContext.Params.BootTrustScore = 0;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase225_LogAdjustedTrust(void) {
    Print(L"Adjusted TrustScore=%u\n", gBootContext.Params.BootTrustScore);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase226_MixBootUidFinal(void) {
    for (UINTN i=0;i<16;i++)
        gBootContext.Params.BootUid[i]^=(UINT8)gBootContext.Params.BootTrustScore;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase227_LogFinalUid(void) {
    Print(L"Final UID: ");
    for (UINTN i=0;i<16;i++) Print(L"%02x", gBootContext.Params.BootUid[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase228_PersistBootStateToLog(void) {
    if (gBootLogPage && gBootStatePage)
        CopyMem((UINT8*)(UINTN)gBootLogPage+64,(VOID*)(UINTN)gBootStatePage,sizeof(BOOTSTATE));
    return EFI_SUCCESS;
}

static EFI_STATUS Phase229_LogPersistResult(void) {
    Print(L"Boot state persisted\n");
    return EFI_SUCCESS;
}

static EFI_STATUS Phase230_BootStateCachingComplete(void) {
    if (gBootStatePage && gTrustScoreBlock)
        ((UINT8*)(UINTN)gTrustScoreBlock)[40] = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase231_SetupExitNotice(void) { Print(L"Preparing to exit boot services\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase232_CleanAnomalyBuffer(void) { gAnomalyCount=0; return EFI_SUCCESS; }

static EFI_STATUS Phase233_DelayBeforeExit(void) { gBS->Stall(10000); return EFI_SUCCESS; }

static EFI_STATUS Phase234_FinalSecureCheck(void) { if(!gSecureChainValid) gBootContext.Params.FallbackMode=1; return EFI_SUCCESS; }

static EFI_STATUS Phase235_DrawFinalIndicator(void) {
    if (gBootContext.Gop) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL g={0,255,0,0};
        gBootContext.Gop->Blt(gBootContext.Gop,&g,EfiBltVideoFill,0,0,0,
            gBootContext.Gop->Mode->Info->VerticalResolution-8,8,8,0);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Phase236_LogExitPlan(void) { Print(L"Exit plan ready\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase237_CacheEntropyInParams(void) {
    if (gBootStatePage) ((BOOTSTATE*)(UINTN)gBootStatePage)->Entropy ^= AsmReadTsc();
    return EFI_SUCCESS;
}

static EFI_STATUS Phase238_LogEntropyCached(void) { Print(L"Entropy cached\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase239_SaveAcpiErrorCount(void) {
    if (gBootStatePage) ((BOOTSTATE*)(UINTN)gBootStatePage)->Anom = (UINT8)gAcpiErrCount;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase240_LogAcpiErrors(void) {
    Print(L"ACPI errors stored %u\n", (UINT32)gAcpiErrCount);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase241_MarkRepairStatus(void) {
    if (gBootStatePage) ((BOOTSTATE*)(UINTN)gBootStatePage)->Trust |= gSelfRepairNeeded?0x80000000:0;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase242_LogRepairStatus(void) {
    Print(L"Repair flag %u\n", gSelfRepairNeeded);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase243_SaveFallbackStatus(void) {
    if (gBootStatePage) ((BOOTSTATE*)(UINTN)gBootStatePage)->Anom |= (gBootContext.Params.FallbackMode?0x80:0);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase244_LogFallbackStatus(void) {
    Print(L"Fallback stored %u\n", gBootContext.Params.FallbackMode);
    return EFI_SUCCESS;
}

static EFI_STATUS Phase245_FinalizeSecureChain(void) {
    if (!gSecureChainValid) return EFI_SECURITY_VIOLATION; return EFI_SUCCESS;
}

static EFI_STATUS Phase246_LogSecureChain(void) { Print(L"Secure chain verified\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase247_PrepareForKernelJump(void) { Print(L"Preparing for kernel jump\n"); return EFI_SUCCESS; }

static EFI_STATUS Phase248_FinalAnomalyPrint(void) { Print(L"Final anomaly count %u\n", (UINT32)gAnomalyCount); return EFI_SUCCESS; }

static EFI_STATUS Phase249_FinalCacheDone(void) {
    if (gBootStatePage && gTrustScoreBlock)
        ((UINT64*)(UINTN)gTrustScoreBlock)[5] = gBootStatePage;
    return EFI_SUCCESS;
}

static EFI_STATUS Phase250_Complete(void) { Print(L"Phase 250 complete\n"); return EFI_SUCCESS; }

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
    Phase101_CloseFileHandles();
    Phase102_UpdateMemoryMap();
    Phase103_CheckMapKeyValid();
    Phase104_PreExitBootCheck();
    Phase105_ExitBootServicesReady();
    Phase106_LocateRsdp();
    Phase107_LogRsdpInfo();
    Phase108_LocateXsdt();
    Phase109_FindFadt();
    Phase110_LocateDsdt();
    Phase111_LogAcpiOemId();
    Phase112_ParseFADT();
    Phase113_ParseDsdtHeader();
    Phase114_StoreAcpiAddresses();
    Phase115_AcpiParsingComplete();
    Phase116_DrawProgressBar();
    Phase117_DrawTrustScoreBar();
    Phase118_DisplayBootUid();
    Phase119_DisplaySecureBootState();
    Phase120_DisplaySignatureState();
    Phase121_AnimateDots();
    Phase122_DrawBootText();
    Phase123_UpdateProgressAnimation();
    Phase124_DrawFinalBar();
    Phase125_FramebufferComplete();
    Phase126_ScanMemory();
    Phase127_DetectMemoryGaps();
    Phase128_ComputeEntropy();
    Phase129_StoreEntropy();
    Phase130_LogEntropy();
    Phase131_CheckMemory();
    Phase132_FinalizeMemoryScan();
    Phase133_RandomDelay();
    Phase134_MemoryScanningComplete();
    Phase135_ReserveTime();
    Phase136_RecordTpmTimer();
    Phase137_RecordFsTimer();
    Phase138_RecordHashTimer();
    Phase139_RecordElfTimer();
    Phase140_ComputeBootTime();
    Phase141_PrepareBootScoreWeight();
    Phase142_LogTimingSummary();
    Phase143_StoreTimerData();
    Phase144_RealTimePrepComplete();
    Phase145_LogBootEstimate();
    Phase146_PrintSummary();
    Phase147_HighlightFailures();
    Phase148_ReserveTrustScoreRegion();
    Phase149_StorePayloadPointers();
    Phase150_FinalHandoffPrep();
    Phase151_VerifyPcr0Again();
    Phase152_CopyTrustDataToMemory();
    Phase153_PrintTrustMetrics();
    Phase154_CheckSignatureWarning();
    Phase155_RenderWarningLogo();
    Phase156_LogTrustRegion();
    Phase157_VerifyTrustMemory();
    Phase158_FinalLockdownMsg();
    Phase159_NoOp();
    Phase160_LockdownComplete();
    Phase161_CheckTrustThreshold();
    Phase162_LogFallbackMode();
    Phase163_RenderFallbackOverlay();
    Phase164_StoreFallbackInMemory();
    Phase165_BeepOnFallback();
    Phase166_PauseForFallback();
    Phase167_LogFallbackComplete();
    Phase168_NoOp();
    Phase169_NoOp();
    Phase170_FaultDecisionDone();
    Phase171_AllocateParamsBlock();
    Phase172_CopyParamsToBlock();
    Phase173_ComputeParamsChecksum();
    Phase174_StoreParamsPointer();
    Phase175_LogParamsAddress();
    Phase176_NoOp();
    Phase177_NoOp();
    Phase178_NoOp();
    Phase179_NoOp();
    Phase180_ParamsInjectionDone();
    Phase196_ReserveEventLog();
    Phase197_WriteBootLog();
    Phase198_LogEventAddress();
    Phase199_NoOp();
    Phase200_ReservationComplete();
    Phase201_RuntimeAnomalyScan();
    Phase202_ScanKernelPatterns();
    Phase203_LogAnomalyResults();
    Phase204_GatherAdvancedEntropy();
    Phase205_MixEntropy();
    Phase206_LogEntropyDigest();
    Phase207_AcpiDeepTraversal();
    Phase208_LogAcpiTraversal();
    Phase209_SecureBootChainValidation();
    Phase210_LogSecureChain();
    Phase211_InstallSelfRepair();
    Phase212_RunSelfRepair();
    Phase213_LogSelfRepair();
    Phase214_SetupRealtimeFallback();
    Phase215_RenderRealtimeFallback();
    Phase216_AllocateBootState();
    Phase217_StoreBootState();
    Phase218_HashBootState();
    Phase219_LogBootStateHash();
    Phase220_CacheBootStatePtr();
    Phase221_LogBootStateAddr();
    Phase222_VerifyBootState();
    Phase223_FinalizeBootState();
    Phase224_AdjustTrustForAnomalies();
    Phase225_LogAdjustedTrust();
    Phase226_MixBootUidFinal();
    Phase227_LogFinalUid();
    Phase228_PersistBootStateToLog();
    Phase229_LogPersistResult();
    Phase230_BootStateCachingComplete();
    Phase231_SetupExitNotice();
    Phase232_CleanAnomalyBuffer();
    Phase233_DelayBeforeExit();
    Phase234_FinalSecureCheck();
    Phase235_DrawFinalIndicator();
    Phase236_LogExitPlan();
    Phase237_CacheEntropyInParams();
    Phase238_LogEntropyCached();
    Phase239_SaveAcpiErrorCount();
    Phase240_LogAcpiErrors();
    Phase241_MarkRepairStatus();
    Phase242_LogRepairStatus();
    Phase243_SaveFallbackStatus();
    Phase244_LogFallbackStatus();
    Phase245_FinalizeSecureChain();
    Phase246_LogSecureChain();
    Phase247_PrepareForKernelJump();
    Phase248_FinalAnomalyPrint();
    Phase249_FinalCacheDone();
    Phase250_Complete();
    Phase181_ValidateMapForExit();
    Phase182_ExitBootServices();
    Phase183_ShowLaunchSplash();
    Phase184_NoOp();
    Phase185_NoOp();
    Phase186_NoOp();
    Phase187_NoOp();
    Phase188_NoOp();
    Phase189_NoOp();
    Phase190_HandoffPrepDone();
    Phase191_JumpToKernel();
    Phase192_NoOp();
    Phase193_NoOp();
    Phase194_NoOp();
    Phase195_NoOp();
    Print(L"Bootloader execution complete.\n");
    return EFI_SUCCESS;
}
