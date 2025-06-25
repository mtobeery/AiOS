#ifndef TELEMETRY_MIND_H
#define TELEMETRY_MIND_H

#include <Uefi.h>
#include "kernel_shared.h"

void Telemetry_LogEvent(const CHAR8 *name, UINTN a, UINTN b);

EFI_STATUS Telemetry_InitPhase711_BootstrapTelemetryMind(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase712_EntropyDeltaRecorder(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase713_AIAnomalyMapper(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase714_TrustDropTriggerTracker(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase715_PerPhaseLogCompressor(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase716_TelemetryThrottler(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase717_LogEntropyBudgetManager(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase718_ModuleEventWindowTracker(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase719_TrustCurveSnapshotLogger(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase720_TelemetryFrameAssembler(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase721_AnomalyRebroadcastAgent(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase722_TrustEntropyHeatmapEmitter(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase723_AITrainingDataExtractor(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase724_EntropyCollapseChronicle(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase725_ModuleSilenceDetector(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase726_PeripheralTrustEchoLogger(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase727_PrecisionLogRouter(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase728_SilentPhaseDeltaLogger(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase729_MemoryLeakTraceTracker(KERNEL_CONTEXT *ctx);
EFI_STATUS Telemetry_InitPhase730_BootEntropyFootprintEmitter(KERNEL_CONTEXT *ctx);

EFI_STATUS TelemetryMind_RunAllPhases(KERNEL_CONTEXT *ctx);

#endif // TELEMETRY_MIND_H
