#ifndef AI_CORE_H
#define AI_CORE_H

#include <Uefi.h>
#include "kernel_shared.h"

EFI_STATUS AICore_ReportEvent(const CHAR8 *name);
EFI_STATUS AICore_ReportPhase(const CHAR8 *name, UINTN value);
EFI_STATUS AICore_RecordPhase(const CHAR8 *name, UINTN phase, UINTN value);
UINTN* AICore_SelectTopTasks(UINTN count);
EFI_STATUS AICore_PredictBurstLoad(UINTN *prob);
EFI_STATUS AICore_AttachToBootDNA(const CHAR8 *module, UINT64 trust);
EFI_STATUS AICore_FinalizeSchedulerMind(UINTN miss);
EFI_STATUS AICore_FinalizeMemoryMind(UINTN miss, UINT64 entropy);
EFI_STATUS AICore_FinalizeGpuMind(UINTN miss);
EFI_STATUS AICore_FinalizeIOMind(UINTN miss);
EFI_STATUS AICore_PredictTaskOrder(UINTN *tasks, UINTN count);
EFI_STATUS AICore_DrawProgress(const CHAR8 *task, UINTN pct);
EFI_STATUS AICore_SealMemory(const CHAR8 *name, UINTN size, UINT64 entropy);
EFI_STATUS AICore_CommitTrust(const CHAR8 *tag, UINT64 trust);

// AI Core initialization phases 861-900
EFI_STATUS AICore_InitPhase861_BootstrapAICore(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase862_SystemIntentRecognizer(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase863_TrustFusionEngine(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase864_EntropyFlowMapper(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase865_AnomalyRankingEngine(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase866_AIReplayFrameConstructor(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase867_PredictivePhaseForecaster(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase868_BootDNAPhaseAlignmentChecker(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase869_AIEntropyNormalizer(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase870_SchedulerTrustInfluenceAdvisor(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase871_AITrustIntentCalibrator(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase872_PredictiveLoadBalancer(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase873_EntropyBudgetAdvisor(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase874_ThreadOutcomeForecaster(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase875_AIOverrideRequestHandler(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase876_GlobalTrustSyncToTelemetry(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase877_AIConfidenceCurveEmitter(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase878_AIEntropySaturationTracker(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase879_AIAdvisoryHistoryIndexer(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase880_FinalizeAICoreBlockA(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase881_AIOutcomeTrustValidator(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase882_SelfDeviationDetector(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase883_EntropyCurveIntegrityCheck(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase884_TrustLoopbackVerifier(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase885_BootIntentEchoScanner(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase886_PredictiveFailureForecaster(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase887_AIHeuristicTuner(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase888_AITrustBoundaryObserver(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase889_SchedulerNudgeAgent(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase890_DeterministicModelAligner(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase891_EntropyReplayModelTrainer(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase892_AIFailureSuppressionFence(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase893_AIImpactScoreReporter(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase894_EntropyWeightRedistributor(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase895_PrecisionAdvisorPacker(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase896_TrustRecoveryModelBuilder(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase897_AIConsensusAdjuster(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase898_PhaseCorrectionForecaster(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase899_TelemetryCompressionAdvisor(KERNEL_CONTEXT *ctx);
EFI_STATUS AICore_InitPhase900_FinalizeAIBlockB(KERNEL_CONTEXT *ctx);

#endif // AI_CORE_H
