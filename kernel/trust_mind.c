// trust_mind.c - AiOS Trust Mind Phases 761-810

#include "kernel_shared.h"
#include "telemetry_mind.h"
#include "ai_core.h"

#define TRUST_RING_SIZE 32
#define MODULE_COUNT    6
#define THREAD_COUNT    256

static UINT64 gTrustRing[TRUST_RING_SIZE];
static UINTN  gTrustHead = 0;
static UINT64 gModuleTrust[MODULE_COUNT];
static UINT64 gThreadTrust[THREAD_COUNT];
static UINT64 gTrustScore = 50;

EFI_STATUS Trust_Reset(void) {
    ZeroMem(gTrustRing, sizeof(gTrustRing));
    ZeroMem(gModuleTrust, sizeof(gModuleTrust));
    ZeroMem(gThreadTrust, sizeof(gThreadTrust));
    gTrustHead = 0;
    gTrustScore = 50;
    return EFI_SUCCESS;
}

UINT64 Trust_GetCurrentScore(void) {
    return gTrustScore;
}

void Trust_AdjustScore(UINTN id, INTN delta) {
    INT64 new_score = (INT64)gTrustScore + delta;
    if (new_score < 0) new_score = 0;
    gTrustScore = (UINT64)new_score;

    if (id < THREAD_COUNT) {
        INT64 ts = (INT64)gThreadTrust[id] + delta;
        if (ts < 0) ts = 0;
        gThreadTrust[id] = (UINT64)ts;
    }

    gTrustRing[gTrustHead] = gTrustScore;
    gTrustHead = (gTrustHead + 1) % TRUST_RING_SIZE;
}

EFI_STATUS Trust_InitPhase761_BootstrapTrustMind(KERNEL_CONTEXT *ctx) {
    Trust_Reset();
    ctx->trust_score = gTrustScore;
    Telemetry_LogEvent("Trust_Bootstrap", (UINTN)gTrustScore, 0);
    AICore_ReportEvent("TrustMindBootstrap");
    return EFI_SUCCESS;
}

EFI_STATUS Trust_InitPhase767_TrustCollapsePreventer(KERNEL_CONTEXT *ctx) {
    static UINT64 last_scores[MODULE_COUNT];
    UINTN drops = 0;
    for (UINTN i = 0; i < MODULE_COUNT; ++i) {
        if (gModuleTrust[i] + 10 < last_scores[i])
            drops++;
        last_scores[i] = gModuleTrust[i];
    }
    if (drops > 3) {
        ctx->EntropyScore ^= 0xFFFFULL;
        Telemetry_LogEvent("TrustCollapse", drops, 0);
        AICore_ReportEvent("EntropyLock");
    }
    return EFI_SUCCESS;
}
