#include <elf.h>
#include <string.h>
#include <efi.h>
#include <efilib.h>
#include "loader_structs.h"

EFI_STATUS
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"AiOS Bootloader Started!\n");

    EFI_STATUS status;
    EFI_FILE_IO_INTERFACE *fs;
    EFI_FILE_HANDLE root, kernel;

    status = uefi_call_wrapper(BS->LocateProtocol, 3, &gEfiSimpleFileSystemProtocolGuid, NULL, (void**)&fs);
    if (EFI_ERROR(status)) {
        Print(L"Failed to locate FS protocol: %r\n", status);
        return status;
    }

    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) {
        Print(L"Failed to open volume: %r\n", status);
        return status;
    }

    status = uefi_call_wrapper(root->Open, 5, root, &kernel, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"Could not open kernel.elf: %r\n", status);
        return status;
    }
    Print(L"kernel.elf opened successfully\n");

    // Read ELF header
    Elf64_Ehdr ehdr;
    UINTN size = sizeof(Elf64_Ehdr);
    status = uefi_call_wrapper(kernel->Read, 3, kernel, &size, &ehdr);
    if (EFI_ERROR(status) || size != sizeof(Elf64_Ehdr)) {
        Print(L"Failed to read ELF header: %r\n", status);
        return status;
    }

    if (CompareMem(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_machine != EM_X86_64) {
        Print(L"Invalid ELF64 file\n");
        return EFI_LOAD_ERROR;
    }
    Print(L"Valid ELF64\n");

    // Read program headers
    if (ehdr.e_phnum == 0) {
        Print(L"No program headers found\n");
        return EFI_LOAD_ERROR;
    }

    Elf64_Phdr *phdrs = AllocatePool(ehdr.e_phnum * sizeof(Elf64_Phdr));
    if (!phdrs) {
        Print(L"Failed to allocate phdrs\n");
        return EFI_OUT_OF_RESOURCES;
    }
    uefi_call_wrapper(kernel->SetPosition, 2, kernel, ehdr.e_phoff);
    size = ehdr.e_phnum * sizeof(Elf64_Phdr);
    status = uefi_call_wrapper(kernel->Read, 3, kernel, &size, phdrs);
    if (EFI_ERROR(status) || size != ehdr.e_phnum * sizeof(Elf64_Phdr)) {
        Print(L"Failed to read program headers: %r\n", status);
        return status;
    }

    UINTN load_segments = 0;
    for (UINTN i = 0; i < ehdr.e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD)
            load_segments++;
    }
    Print(L"%d loadable segments found\n", load_segments);

    // Allocate memory and load segments
    for (UINTN i = 0; i < ehdr.e_phnum; ++i) {
        if (phdrs[i].p_type != PT_LOAD)
            continue;
        EFI_PHYSICAL_ADDRESS addr = phdrs[i].p_paddr;
        UINTN pages = EFI_SIZE_TO_PAGES(phdrs[i].p_memsz);
        status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, pages, &addr);
        if (EFI_ERROR(status)) {
            Print(L"AllocatePages failed: %r\n", status);
            return status;
        }
        // Copy segment data
        uefi_call_wrapper(kernel->SetPosition, 2, kernel, phdrs[i].p_offset);
        UINTN file_sz = phdrs[i].p_filesz;
        status = uefi_call_wrapper(kernel->Read, 3, kernel, &file_sz, (void*)(UINTN)addr);
        if (EFI_ERROR(status) || file_sz != phdrs[i].p_filesz) {
            Print(L"Segment read failed: %r\n", status);
            return status;
        }
        UINTN bss_sz = phdrs[i].p_memsz - phdrs[i].p_filesz;
        if (bss_sz)
            SetMem((void*)(UINTN)(addr + phdrs[i].p_filesz), bss_sz, 0);
    }

    // Setup loader params
    LOADER_PARAMS params;
    UINTN map_size = 0, map_key, desc_size; UINT32 desc_ver;
    BS->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    map_size += desc_size * 8; // extra space
    params.mem_map = AllocatePool(map_size);
    if (!params.mem_map) {
        Print(L"Failed to allocate memory map\n");
        return EFI_OUT_OF_RESOURCES;
    }
    params.map_size = map_size;
    params.desc_size = desc_size;
    status = BS->GetMemoryMap(&map_size, params.mem_map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        Print(L"GetMemoryMap failed: %r\n", status);
        return status;
    }
    params.map_size = map_size;
    params.map_key = map_key;
    params.desc_version = desc_ver;
    params.kernel_base = phdrs[0].p_paddr;
    Print(L"Memory map entries collected: %d\n", map_size / desc_size);

    // Jump to kernel
    void (*kernel_entry)(LOADER_PARAMS*) = (void(*)(LOADER_PARAMS*))(UINTN)ehdr.e_entry;
    Print(L"Jumping to kernel...\n");
    kernel_entry(&params);

    return EFI_SUCCESS;
}
