
#include <efi.h>
#include <efilib.h>
#include "loader_structs.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"🧠 AiOS Kernel Stub Loaded\n");

    LOADER_PARAMS *Params = (LOADER_PARAMS *)ImageHandle;
    Print(L"BootUID: %lx\n", Params->BootUID);
    Print(L"TrustScore: %u\n", Params->FinalBootScore);

    while (1); // Halt for visibility
    return EFI_SUCCESS;
}
