#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/PciIo.h>
#include <Protocol/Tcg2Protocol.h>
#include <Protocol/Hash2.h>

#define SHA256_DIGEST_LENGTH 32

// Phase 18: Read CPUID and total RAM from memory map
EFI_STATUS Phase18_ReadCpuRam(CHAR8 Vendor[13], UINT64 *Ram)
{
    UINT32 RegEax, RegEbx, RegEcx, RegEdx;
    RegEax = 0;
    __asm__ __volatile__("cpuid" : "=a"(RegEax), "=b"(RegEbx), "=c"(RegEcx), "=d"(RegEdx) : "0"(RegEax));
    CopyMem(Vendor, &RegEbx, 4);
    CopyMem(Vendor + 4, &RegEdx, 4);
    CopyMem(Vendor + 8, &RegEcx, 4);
    Vendor[12] = '\0';

    UINTN MapSize = 0, MapKey, DescSize;
    UINT32 DescVer;
    EFI_MEMORY_DESCRIPTOR *Map = NULL;
    EFI_STATUS Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (Status != EFI_BUFFER_TOO_SMALL)
        return Status;
    Status = gBS->AllocatePool(EfiBootServicesData, MapSize, (VOID**)&Map);
    if (EFI_ERROR(Status))
        return Status;
    Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR(Status))
        return Status;

    UINT64 Total = 0;
    for (UINTN i = 0; i < MapSize / DescSize; ++i) {
        EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)Map + i * DescSize);
        if (Desc->Type == EfiConventionalMemory || Desc->Type == EfiBootServicesData || Desc->Type == EfiLoaderData)
            Total += Desc->NumberOfPages * 4096ULL;
    }
    *Ram = Total;
    gBS->FreePool(Map);
    return EFI_SUCCESS;
}

// Phase 19: Scan PCI bus for GPU vendor/device ID
EFI_STATUS Phase19_ScanPciGpu(CHAR16 *OutBuf, UINTN OutLen)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles;
    UINTN HandleCount;
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status))
        return Status;

    for (UINTN i = 0; i < HandleCount; ++i) {
        EFI_PCI_IO_PROTOCOL *Pci;
        Status = gBS->HandleProtocol(Handles[i], &gEfiPciIoProtocolGuid, (VOID**)&Pci);
        if (EFI_ERROR(Status))
            continue;
        PCI_TYPE00 Config;
        Status = Pci->Pci.Read(Pci, EfiPciIoWidthUint32, 0, sizeof(Config)/sizeof(UINT32), &Config);
        if (EFI_ERROR(Status))
            continue;
        if (Config.Hdr.ClassCode[2] == 0x03) { // Display controller
            UnicodeSPrint(OutBuf, OutLen, L"%04x:%04x", Config.Hdr.VendorId, Config.Hdr.DeviceId);
            return EFI_SUCCESS;
        }
    }
    UnicodeSPrint(OutBuf, OutLen, L"UnknownGPU");
    return EFI_NOT_FOUND;
}

// Phase 20: Generate AI_BOOT_DNA hash
EFI_STATUS Phase20_GenerateBootDNA(CHAR8 *CpuVendor, UINT64 Ram, CHAR16 *GpuId)
{
    EFI_HASH2_PROTOCOL *Hash2;
    EFI_STATUS Status = gBS->LocateProtocol(&gEfiHash2ProtocolGuid, NULL, (VOID**)&Hash2);
    if (EFI_ERROR(Status))
        return Status;

    CHAR8 Buffer[128];
    AsciiSPrint(Buffer, sizeof(Buffer), "%a-%lx-%s", CpuVendor, Ram, GpuId);
    EFI_HASH_OUTPUT Hash;
    Status = Hash2->Hash(Hash2, &gEfiHashAlgorithmSha256Guid, (UINTN)AsciiStrLen(Buffer), (UINT8*)Buffer, &Hash);
    if (EFI_ERROR(Status))
        return Status;

    Print(L"AI_BOOT_DNA: ");
    for (UINTN i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        Print(L"%02x", Hash.HashBuf[i]);
    Print(L"\n");
    return EFI_SUCCESS;
}
