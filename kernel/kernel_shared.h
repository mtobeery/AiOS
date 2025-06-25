// kernel_shared.h - AiOS Unified Kernel Shared Structures and Definitions

#ifndef KERNEL_SHARED_H
#define KERNEL_SHARED_H

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include "ai_core.h"
#include "telemetry_mind.h"
#include "trust_mind.h"
#include "entropy_mind.h"

// ==================== Constants ====================

#define CPU_PHASE_COUNT        150
#define MEMORY_PHASE_COUNT     150
#define TOTAL_PHASE_COUNT      (CPU_PHASE_COUNT + MEMORY_PHASE_COUNT)

#define CPU_PHASE_THRESHOLD    5000000
#define MEMORY_PHASE_THRESHOLD 4000000

#define CPU_PHASE_MAX_LOAD     10000000
#define MEMORY_ENTROPY_SALT    0x1A2B3C4D

// ==================== Shared State ====================

typedef struct {
    UINTN total_phases;
    UINT64 trust_score;
    UINT64 cpu_elapsed_tsc[CPU_PHASE_COUNT + 1];
    UINT8 cpu_missed[CPU_PHASE_COUNT + 1];
    UINT64 memory_elapsed_tsc[MEMORY_PHASE_COUNT + 1];
    UINT8 memory_missed[MEMORY_PHASE_COUNT + 1];

    // Memory-specific fields
    VOID *MemoryMap;
    UINTN MemoryMapSize;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    UINTN DescriptorCount;
    UINT64 EntropyScore;
    UINTN MissCount;

    /* Scheduler-specific fields */
    UINT64 scheduler_entropy_buffer[16];
    UINTN scheduler_entropy_index;
    UINTN cpu_load_map[8];
    UINTN hotspot_cpu;
    UINTN quantum_table[8];
    UINT64 entropy_slope_buffer[10];
    BOOLEAN entropy_stalling;
    UINT8 thread_numa_map[256];
    BOOLEAN scheduler_pressure_mode;
    UINT8 background_priority;
} KERNEL_CONTEXT;

#endif // KERNEL_SHARED_H
