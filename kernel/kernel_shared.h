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
    // Phase 461+ enhancements
    UINT64 phase_entropy[20];
    UINT64 phase_trust[20];
    UINT64 phase_latency[20];
    UINTN phase_history_index;
    UINT64 avg_trust;
    UINT64 avg_latency;
    UINT64 contention_index;
    UINT64 entropy_gap;
    struct {
        UINTN core_id;
        UINTN task_id;
        UINT64 trust;
        UINT64 latency;
    } snapshot_buffer[64];
    UINTN snapshot_index;
    UINT64 latency_histogram[50];
    UINTN latency_hist_index;
    // Scheduler phase 511+ additions
    UINTN pulse_count;
    UINT8 trust_penalty_buffer[8];
    UINTN scheduler_load_prediction[4];
    INTN trust_entropy_curve;
    /* IO mind fields */
    UINT64 device_entropy_map[16];
    UINT64 io_trust_map[3];
    UINT64 io_entropy_buffer[16];
    UINT8 io_latency_flags[16];
    UINTN io_queue_stall[8];
    UINT64 io_trust_curves[3];
    UINTN io_miss_count;
    BOOLEAN io_sleep_state;
    BOOLEAN io_mirror_built;
    UINTN io_active_device;
    UINT64 device_recovery[16];
    UINT64 boot_dna_trust[16];
    UINT64 final_io_summary;
    BOOLEAN io_mind_complete;
    /* Trust mind fields */
    UINT64 kernel_trust_score;
    BOOLEAN trust_ready;
    BOOLEAN trust_finalized;
    UINT8  trust_anchor[32];
    /* AI core fields */
    UINT64 ai_global_trust_score;
    UINT8  ai_status;
    UINT64 ai_history[128];
    UINT64 intent_alignment_score;
    UINT64 ai_effectiveness;
    UINT64 ai_retrain_id;
    UINT64 ai_entropy_vector[16];
    UINT64 trust_recovery_map[16];
    BOOLEAN ai_core_block_b_ready;
    BOOLEAN ai_gpu_delegate_ready;
    UINT64  ai_scheduler_weight;
    UINT8   entropy_usage_percent;
    UINT64  ai_root_reasoning_tree_hash;
    INT16   ai_rule_weights[961];
    UINT64  ai_trust_matrix[10][10];
    UINT64  ai_entropy_input[16];
    UINT64  ai_prediction_cache[32];
    UINT8   ai_advisory_signature[32];
    UINT8   ai_state;
    BOOLEAN ai_finalized;
    /* Selfcoder fields */
    BOOLEAN selfcoder_active;
    UINT64  phase_queue[8];
    UINT64  selfcoder_trust_matrix[4][4];
    UINT64  selfcoder_plan[3];
    UINT64  generation_log[16];
    UINT8   sandbox_result;
    UINT8   sandbox_status;
    UINT64  boot_dna_history[16];
    UINT64  conflict_log[8];
    UINT64  rollback_slot[8];
    UINT64  phase_budget[16];
    UINT64  behavior_log[16];
    UINT64  dna_phase_fingerprint[16];
    UINT64  entropy_budget[16];
    UINT64  phase_opcode_map[16];
    UINT64  trust_slope[16];
} KERNEL_CONTEXT;

#endif // KERNEL_SHARED_H
