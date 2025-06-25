// scheduler_mind.c - AiOS Scheduler Mind (Phases 451-460)

#include "kernel_shared.h"

// Forward declarations for external subsystems
EFI_STATUS Trust_Reset(void);
UINT64 Trust_GetCurrentScore(void);
void Trust_AdjustScore(UINTN id, INTN delta);
void Telemetry_LogEvent(const CHAR8 *name, UINTN a, UINTN b);
void AICore_RecordPhase(const CHAR8 *name, UINTN phase, UINTN value);
void AICore_ReportPhase(const CHAR8 *name, UINTN value);
UINTN* AICore_SelectTopTasks(UINTN count);

static EFI_STATUS Scheduler_InitPhase451_BootstrapContext(KERNEL_CONTEXT *ctx) {
    ZeroMem(ctx->scheduler_entropy_buffer, sizeof(ctx->scheduler_entropy_buffer));
    ctx->scheduler_entropy_index = 0;
    ZeroMem(ctx->cpu_load_map, sizeof(ctx->cpu_load_map));
    ZeroMem(ctx->quantum_table, sizeof(ctx->quantum_table));
    ZeroMem(ctx->entropy_slope_buffer, sizeof(ctx->entropy_slope_buffer));
    ctx->entropy_stalling = FALSE;
    ZeroMem(ctx->thread_numa_map, sizeof(ctx->thread_numa_map));
    ctx->scheduler_pressure_mode = FALSE;
    ctx->background_priority = 0;
    Trust_Reset();
    ctx->trust_score = Trust_GetCurrentScore();
    Telemetry_LogEvent("Scheduler_Bootstrap", 0, 0);
    AICore_RecordPhase("scheduler_mind", 451, ctx->trust_score);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase452_MapCpuLoad(KERNEL_CONTEXT *ctx) {
    UINTN max_load = 0;
    ctx->hotspot_cpu = 0;
    for (UINTN i = 0; i < 8; ++i) {
        ctx->cpu_load_map[i] = AsmReadTsc() % 100;
        if (ctx->cpu_load_map[i] > max_load) {
            max_load = ctx->cpu_load_map[i];
            ctx->hotspot_cpu = i;
        }
    }
    Telemetry_LogEvent("Scheduler_LoadMapBuilt", ctx->hotspot_cpu, ctx->trust_score);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase453_AdjustQuantum(KERNEL_CONTEXT *ctx) {
    UINT64 entropy = ctx->EntropyScore ^ AsmReadTsc();
    for (UINTN i = 0; i < 8; ++i) {
        UINTN base = 5;
        if ((entropy & 0xFF) > 128)
            ctx->quantum_table[i] = base - 1;
        else
            ctx->quantum_table[i] = base + 1;
        if (Trust_GetCurrentScore() > 80)
            ctx->quantum_table[i]--;
    }
    AICore_RecordPhase("scheduler_mind", 453, entropy);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase454_ScanStarvation(KERNEL_CONTEXT *ctx) {
    UINTN violations = 0;
    for (UINTN id = 0; id < 8; ++id) {
        UINTN wait = AsmReadTsc() % 200;
        if (wait > 100) {
            Trust_AdjustScore(id, -3);
            ctx->MissCount++;
            violations++;
        }
    }
    Telemetry_LogEvent("Scheduler_StarvationScan", violations, ctx->trust_score);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase455_LogEntropySlope(KERNEL_CONTEXT *ctx) {
    UINTN idx = ctx->scheduler_entropy_index % 10;
    UINT64 prev = ctx->entropy_slope_buffer[idx];
    UINT64 now = ctx->EntropyScore;
    ctx->entropy_slope_buffer[idx] = now;
    ctx->scheduler_entropy_index++;
    INT64 slope = (INT64)(now - prev);
    if (slope < -2)
        ctx->entropy_stalling = TRUE;
    AICore_ReportPhase("entropy_slope", (UINTN)slope);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase456_SelectAiTopTasks(KERNEL_CONTEXT *ctx) {
    UINTN *tasks = AICore_SelectTopTasks(4);
    if (!tasks) return EFI_SUCCESS;
    for (UINTN i = 0; i < 4; ++i) {
        UINTN least = 0;
        for (UINTN c = 1; c < 8; ++c)
            if (ctx->cpu_load_map[c] < ctx->cpu_load_map[least])
                least = c;
        ctx->thread_numa_map[tasks[i]] = least;
        if ((AsmReadTsc() & 0x3FF) > 512)
            Trust_AdjustScore(tasks[i], +2);
    }
    Telemetry_LogEvent("Scheduler_AiSelect", tasks[0], tasks[1]);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase457_AdjustTrustOnSwitch(KERNEL_CONTEXT *ctx) {
    for (UINTN id = 0; id < 4; ++id) {
        UINT64 start = AsmReadTsc();
        UINT64 end = start + (AsmReadTsc() % 1000);
        UINT64 latency = end - start;
        ctx->cpu_elapsed_tsc[id] = latency;
        if (latency > CPU_PHASE_THRESHOLD)
            Trust_AdjustScore(id, -1);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase458_CollectZombies(KERNEL_CONTEXT *ctx) {
    UINTN count = 0;
    for (UINTN id = 0; id < 8; ++id) {
        if ((AsmReadTsc() & 1) == 0) {
            count++;
        }
    }
    Telemetry_LogEvent("ZombiesCollected", count, (UINTN)ctx->EntropyScore);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase459_SyncMemoryPressure(KERNEL_CONTEXT *ctx) {
    UINTN pressure = (ctx->DescriptorCount > 0) ?
        (UINTN)((ctx->EntropyScore % ctx->DescriptorCount)) : 0;
    if (pressure > 80) {
        ctx->scheduler_pressure_mode = TRUE;
        if (ctx->background_priority > 0)
            ctx->background_priority--;
    }
    AICore_RecordPhase("scheduler_mind", 459, ctx->EntropyScore);
    return EFI_SUCCESS;
}

static EFI_STATUS Scheduler_InitPhase460_RebalanceNUMA(KERNEL_CONTEXT *ctx) {
    UINT64 before = Trust_GetCurrentScore();
    for (UINTN id = 0; id < 4; ++id) {
        if (ctx->cpu_load_map[id] > 90) {
            ctx->thread_numa_map[id] = (ctx->thread_numa_map[id] + 1) % 2;
        }
    }
    UINT64 after = Trust_GetCurrentScore();
    if (after > before)
        Telemetry_LogEvent("Scheduler_NUMA", after, id);
    return EFI_SUCCESS;
}

EFI_STATUS SchedulerMind_RunAllPhases(KERNEL_CONTEXT *ctx) {
    EFI_STATUS Status;
    for (UINTN phase = 451; phase <= 460; ++phase) {
        switch (phase) {
            case 451: Status = Scheduler_InitPhase451_BootstrapContext(ctx); break;
            case 452: Status = Scheduler_InitPhase452_MapCpuLoad(ctx); break;
            case 453: Status = Scheduler_InitPhase453_AdjustQuantum(ctx); break;
            case 454: Status = Scheduler_InitPhase454_ScanStarvation(ctx); break;
            case 455: Status = Scheduler_InitPhase455_LogEntropySlope(ctx); break;
            case 456: Status = Scheduler_InitPhase456_SelectAiTopTasks(ctx); break;
            case 457: Status = Scheduler_InitPhase457_AdjustTrustOnSwitch(ctx); break;
            case 458: Status = Scheduler_InitPhase458_CollectZombies(ctx); break;
            case 459: Status = Scheduler_InitPhase459_SyncMemoryPressure(ctx); break;
            case 460: Status = Scheduler_InitPhase460_RebalanceNUMA(ctx); break;
            default: Status = EFI_INVALID_PARAMETER; break;
        }
        if (EFI_ERROR(Status)) {
            Telemetry_LogEvent("SchedulerPhaseError", phase, Status);
            return Status;
        }
        ctx->total_phases++;
    }
    return EFI_SUCCESS;
}

