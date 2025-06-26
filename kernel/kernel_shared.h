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
    UINT64 latency_prediction[20];
    BOOLEAN priority_inversion_flag;
    BOOLEAN latency_spike_alert;
    UINT8 latency_confidence;
    UINT64 sched_health;
    BOOLEAN sched_cycle_complete;
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
    /* Storage mind fields */
    UINT64 nvme_bar[4];
    UINT8  nvme_smart_log[512];
    UINTN  nvme_temperature;
    UINTN  nvme_error_count;
    UINTN  nvme_unsafe_shutdowns;
    UINT8  storage_phase_class;
    /* Trust mind fields */
    UINT64 kernel_trust_score;
    BOOLEAN trust_ready;
    BOOLEAN trust_finalized;
    UINT8  trust_anchor[32];
    BOOLEAN trust_oscillating;
    BOOLEAN trust_drift_alert;
    UINT8   trust_freeze_count;
    UINT64  trust_entropy_volatility;
    UINT8   boot_dna_trust_snapshot[64];
    BOOLEAN memory_trust_sync;
    BOOLEAN trust_alarm_active;
    BOOLEAN trust_mind_sealed;
    UINT64  trust_floor;
    UINT64  trust_damage_index;
    INT64   trust_slope_buffer[20];
    UINT8   trust_heatmap[100];
    UINT64  trust_smoothed[20];
    UINT64  trust_normalized[20];
    UINT64  meta_trust_score;
    UINT64  shared_trust_cache[8];
    struct {
        UINT64 trust;
        UINT64 entropy;
    } entropy_snapshot_buffer[32];
    UINTN entropy_snapshot_index;
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
    UINT8   thermal_advisory;
    UINT64  ai_root_reasoning_tree_hash;
    INT16   ai_rule_weights[961];
    UINT64  ai_trust_matrix[10][10];
    UINT64  ai_entropy_input[16];
    UINT64  ai_prediction_cache[32];
    UINT8   ai_advisory_signature[32];
    UINT8   ai_state;
    BOOLEAN ai_alignment_confirmed;
    BOOLEAN ai_finalized;

    /* Thermal mind fields */
    struct { UINT64 cpu_mean; UINT64 gpu_mean; UINT64 cpu_stddev; UINT64 gpu_stddev; } thermal_baseline;
    UINT64  thermal_rise_rate;
    UINT64  thermal_phase_impact_map[100];
    UINT64  thermal_forecast[20];
    UINT8   thermal_hot_zone_id;
    BOOLEAN thermal_safe_mode;
    BOOLEAN thermal_mind_ready;
    BOOLEAN thermal_mind_finalized;

    /* Entropy mind fields */
    UINT8   entropy_source_score[3];
    struct { UINT64 mean; UINT64 stddev; } entropy_baseline;
    UINT64  entropy_heatmap[10][10];
    INT64   entropy_prediction_delta[5];
    INT64   entropy_prediction_weights[5];
    UINT8   entropy_density_score[3];
    UINT64  entropy_thermal_correlation;
    UINT64  entropy_recovery_time;
    UINT64  entropy_phase_map[100];
    UINT8   entropy_resilience_factor;
    UINT8   entropy_stability_window;
    INT64   entropy_micro_drift;
    UINT64  entropy_shock_buffer[16];
    UINT8   entropy_blindspot_flags[10];
    UINT64  entropy_load_forecast;
    UINT64  entropy_stability_pulse[16];
    UINT64  entropy_ai_reflection[10];
    UINT64  entropy_cluster_count;
    INT64   entropy_weights[5];
    UINT64  entropy_burst_prediction;
    UINT64  entropy_context_noise[3];
    UINT8   entropy_control_mode;
    UINT8   meta_confidence;
    UINT64  ai_uncertainty_input;
    BOOLEAN entropy_mind_ready;
    BOOLEAN entropy_mind_locked;

    /* Network mind fields */
    UINT8   mac_trust_profile[16];
    UINT16  vlan_context[16];
    UINT64  packet_entropy_score[32];
    UINT64  anomaly_fingerprint[8][2];
    UINT64  route_trust_score;
    UINT8   anomaly_trend[32];
    UINT8   quarantine_list[16];
    BOOLEAN vlan_alert;
    BOOLEAN network_mind_ready;
    /* Power mind fields */
    UINT8   battery_percent;
    INT8    battery_curve[64];
    INT8    discharge_slope;
    UINT64  power_anomaly_log[16];
    BOOLEAN power_safe_mode;
    UINT8   battery_trust_index;
    UINT64  power_confidence_score;
    UINT8   cpu_tdp_percent;
    BOOLEAN power_mind_ready;
} KERNEL_CONTEXT;

#endif // KERNEL_SHARED_H
