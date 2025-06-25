// kernel_main.c - AiOS Unified Kernel Execution Entry Point
// Coordinates all AI-native components (cpu_mind, memory_mind, gpu_mind, etc.)

#include "kernel_shared.h"      // Shared structs, macros, and constants
#include "telemetry_mind.h"     // Telemetry and monitoring
#include "trust_mind.h"         // System-wide trust score tracking
#include "ai_core.h"            // Central AI agent and context

// Forward declarations (modules must implement these)
EFI_STATUS CpuMind_RunAllPhases(KERNEL_CONTEXT *ctx);
EFI_STATUS MemoryMind_RunAllPhases(KERNEL_CONTEXT *ctx);
// Future: EFI_STATUS GpuMind_RunAllPhases(KERNEL_CONTEXT *ctx);

KERNEL_CONTEXT gKernelCtx;

// === ENTRY POINT ===
EFI_STATUS AiOS_KernelMain(VOID) {
    Telemetry_LogEvent("AiOS_Kernel_Begin", 0, 0);
    Trust_Reset();
    gKernelCtx.total_phases = 0;
    gKernelCtx.trust_score = 0;

    EFI_STATUS Status;

    // === PHASE 001–150: CPU MIND ===
    Status = CpuMind_RunAllPhases(&gKernelCtx);
    if (EFI_ERROR(Status)) {
        Telemetry_LogEvent("CpuMindFailure", 1, Status);
        return Status;
    }

    // === PHASE 151–300: MEMORY MIND ===
    Status = MemoryMind_RunAllPhases(&gKernelCtx);
    if (EFI_ERROR(Status)) {
        Telemetry_LogEvent("MemoryMindFailure", 2, Status);
        return Status;
    }

    // === Placeholder: GPU MIND, SCHEDULER MIND, IO MIND, etc. ===

    // Final AI wrap-up
    gKernelCtx.trust_score = Trust_GetCurrentScore();
    AICore_ReportPhase("kernel_mind_complete", gKernelCtx.trust_score);
    Telemetry_LogEvent("AiOS_Kernel_Ready", gKernelCtx.trust_score, gKernelCtx.total_phases);

    return EFI_SUCCESS;
}
