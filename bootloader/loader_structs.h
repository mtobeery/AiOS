#ifndef LOADER_STRUCTS_H
#define LOADER_STRUCTS_H

#include <Uefi.h>

typedef struct {
    EFI_MEMORY_DESCRIPTOR *MemoryMap;
    UINTN MemoryMapSize;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    EFI_PHYSICAL_ADDRESS KernelBase;
    EFI_PHYSICAL_ADDRESS KernelEntry;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *GopModeInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} LOADER_PARAMS;

#endif // LOADER_STRUCTS_H
