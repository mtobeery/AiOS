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

typedef struct {
    EFI_HANDLE ImageHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootDir;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
    EFI_TCG2_PROTOCOL *Tcg2;
    EFI_FILE_HANDLE KernelFile;
    LOADER_PARAMS Params;
} BOOT_CONTEXT;


#endif // LOADER_STRUCTS_H
