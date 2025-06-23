#ifndef LOADER_STRUCTS_H
#define LOADER_STRUCTS_H

#include <efi.h>

typedef struct {
    void   *mem_map;
    UINTN   map_size;
    UINTN   map_key;
    UINTN   desc_size;
    UINT32  desc_version;
    UINTN   kernel_base;
} LOADER_PARAMS;

#endif // LOADER_STRUCTS_H
