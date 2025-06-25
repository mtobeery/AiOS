#include "kernel_shared.h"
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#define TELEMETRY_RING_SIZE 128
#define TELEMETRY_PHASE_MAX 760

typedef struct {
    CHAR8  name[32];
    UINTN  a;
    UINTN  b;
    UINT64 timestamp;
    UINTN  phase;
} TELEMETRY_EVENT;

static TELEMETRY_EVENT gTelemetryRing[TELEMETRY_RING_SIZE];
static UINTN gTelemetryHead = 0;
static UINT64 gEntropyCurve[TELEMETRY_PHASE_MAX];
static UINT64 gTrustTimeline[TELEMETRY_PHASE_MAX];
static UINTN gTelemetryRate = 1;
static UINT8 gSuppressed[TELEMETRY_RING_SIZE];

static VOID CopyName(CHAR8 *dst, const CHAR8 *src, UINTN len) {
    UINTN i = 0;
    for (; i + 1 < len && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void Telemetry_LogEvent(const CHAR8 *name, UINTN a, UINTN b) {
    CopyName(gTelemetryRing[gTelemetryHead].name, name, sizeof(gTelemetryRing[gTelemetryHead].name));
    gTelemetryRing[gTelemetryHead].a = a;
    gTelemetryRing[gTelemetryHead].b = b;
    gTelemetryRing[gTelemetryHead].timestamp = AsmReadTsc();
    gTelemetryRing[gTelemetryHead].phase = gTelemetryHead;
    gTelemetryHead = (gTelemetryHead + 1) % TELEMETRY_RING_SIZE;
    Print(L"[TEL] %a %u %u\n", name, a, b);
}

UINTN Telemetry_GetTemperature(void) {
    return 60 + (AsmReadTsc() % 40);
}

EFI_STATUS Telemetry_InitPhase711_BootstrapTelemetryMind(KERNEL_CONTEXT *ctx) {
    ZeroMem(gTelemetryRing, sizeof(gTelemetryRing));
    ZeroMem(gEntropyCurve, sizeof(gEntropyCurve));
    ZeroMem(gTrustTimeline, sizeof(gTrustTimeline));
    ZeroMem(gSuppressed, sizeof(gSuppressed));
    gTelemetryHead = 0;
    gTelemetryRate = 1;
    Telemetry_LogEvent("Telemetry_Bootstrap", ctx->MemoryMapSize, ctx->DescriptorCount);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase712_EntropyDeltaRecorder(KERNEL_CONTEXT *ctx) {
    static UINT64 last = 0;
    UINT64 cur = ctx->EntropyScore;
    UINT64 delta = (cur >= last) ? (cur - last) : (last - cur);
    last = cur;
    UINTN idx = ctx->total_phases % 16;
    gEntropyCurve[idx] = delta;
    Telemetry_LogEvent("EntropyDelta", (UINTN)delta, idx);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase713_AIAnomalyMapper(KERNEL_CONTEXT *ctx) {
    UINTN phase = ctx->total_phases;
    gEntropyCurve[phase] = ctx->EntropyScore;
    gTrustTimeline[phase] = ctx->trust_score;
    Telemetry_LogEvent("AIAnomaly", (UINTN)ctx->trust_score, (UINTN)ctx->EntropyScore);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase714_TrustDropTriggerTracker(KERNEL_CONTEXT *ctx) {
    static UINT64 last = 0;
    if (ctx->trust_score + 10 < last) {
        Telemetry_LogEvent("TrustDrop", (UINTN)(last - ctx->trust_score), 0);
    }
    last = ctx->trust_score;
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase715_PerPhaseLogCompressor(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("LogCompress", gTelemetryHead, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase716_TelemetryThrottler(KERNEL_CONTEXT *ctx) {
    if (ctx->scheduler_pressure_mode)
        gTelemetryRate = 2;
    else
        gTelemetryRate = 1;
    Telemetry_LogEvent("Throttle", gTelemetryRate, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase717_LogEntropyBudgetManager(KERNEL_CONTEXT *ctx) {
    UINTN budget = 100;
    if (ctx->EntropyScore > 1000)
        budget = 50;
    Telemetry_LogEvent("EntropyBudget", budget, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase718_ModuleEventWindowTracker(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("EventWindow", ctx->total_phases, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase719_TrustCurveSnapshotLogger(KERNEL_CONTEXT *ctx) {
    UINTN phase = ctx->total_phases;
    gTrustTimeline[phase] = ctx->trust_score;
    Telemetry_LogEvent("TrustSnap", phase, (UINTN)ctx->trust_score);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase720_TelemetryFrameAssembler(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("FrameAsm", gTelemetryHead, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase721_AnomalyRebroadcastAgent(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("AnomRebroadcast", ctx->MissCount, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase722_TrustEntropyHeatmapEmitter(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("Heatmap", (UINTN)ctx->EntropyScore, (UINTN)ctx->trust_score);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase723_AITrainingDataExtractor(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("TrainingExtract", ctx->total_phases, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase724_EntropyCollapseChronicle(KERNEL_CONTEXT *ctx) {
    if (ctx->EntropyScore < 10)
        Telemetry_LogEvent("EntropyCollapse", (UINTN)ctx->EntropyScore, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase725_ModuleSilenceDetector(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("SilenceDetect", ctx->MissCount, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase726_PeripheralTrustEchoLogger(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("TrustEcho", ctx->io_trust_map[0], 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase727_PrecisionLogRouter(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("LogRouter", gTelemetryHead, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase728_SilentPhaseDeltaLogger(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("SilentDelta", ctx->total_phases, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase729_MemoryLeakTraceTracker(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("MemLeakTrace", ctx->MemoryMapSize, 0);
    return EFI_SUCCESS;
}

EFI_STATUS Telemetry_InitPhase730_BootEntropyFootprintEmitter(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("BootEntropy", (UINTN)ctx->EntropyScore, 0);
    return EFI_SUCCESS;
}

EFI_STATUS TelemetryMind_RunAllPhases(KERNEL_CONTEXT *ctx) {
    EFI_STATUS Status;
    if ((Status = Telemetry_InitPhase711_BootstrapTelemetryMind(ctx))) return Status;
    if ((Status = Telemetry_InitPhase712_EntropyDeltaRecorder(ctx))) return Status;
    if ((Status = Telemetry_InitPhase713_AIAnomalyMapper(ctx))) return Status;
    if ((Status = Telemetry_InitPhase714_TrustDropTriggerTracker(ctx))) return Status;
    if ((Status = Telemetry_InitPhase715_PerPhaseLogCompressor(ctx))) return Status;
    if ((Status = Telemetry_InitPhase716_TelemetryThrottler(ctx))) return Status;
    if ((Status = Telemetry_InitPhase717_LogEntropyBudgetManager(ctx))) return Status;
    if ((Status = Telemetry_InitPhase718_ModuleEventWindowTracker(ctx))) return Status;
    if ((Status = Telemetry_InitPhase719_TrustCurveSnapshotLogger(ctx))) return Status;
    if ((Status = Telemetry_InitPhase720_TelemetryFrameAssembler(ctx))) return Status;
    if ((Status = Telemetry_InitPhase721_AnomalyRebroadcastAgent(ctx))) return Status;
    if ((Status = Telemetry_InitPhase722_TrustEntropyHeatmapEmitter(ctx))) return Status;
    if ((Status = Telemetry_InitPhase723_AITrainingDataExtractor(ctx))) return Status;
    if ((Status = Telemetry_InitPhase724_EntropyCollapseChronicle(ctx))) return Status;
    if ((Status = Telemetry_InitPhase725_ModuleSilenceDetector(ctx))) return Status;
    if ((Status = Telemetry_InitPhase726_PeripheralTrustEchoLogger(ctx))) return Status;
    if ((Status = Telemetry_InitPhase727_PrecisionLogRouter(ctx))) return Status;
    if ((Status = Telemetry_InitPhase728_SilentPhaseDeltaLogger(ctx))) return Status;
    if ((Status = Telemetry_InitPhase729_MemoryLeakTraceTracker(ctx))) return Status;
    if ((Status = Telemetry_InitPhase730_BootEntropyFootprintEmitter(ctx))) return Status;
    return EFI_SUCCESS;
}

