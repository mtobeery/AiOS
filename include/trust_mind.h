#ifndef TRUST_MIND_H
#define TRUST_MIND_H

#include <Uefi.h>
#include "kernel_shared.h"

EFI_STATUS Trust_Reset(void);
UINT64 Trust_GetCurrentScore(void);
void Trust_AdjustScore(UINTN id, INTN delta);
void Trust_Transfer(UINTN from, UINTN to, UINTN amount);

EFI_STATUS TrustPhase_Execute(KERNEL_CONTEXT *ctx, UINTN phase);

EFI_STATUS Trust_InitPhase761_BootstrapTrustMind(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase767_TrustCollapsePreventer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase811_EntropyWeightedTrustForecaster(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase812_TrustAnomalyDeterminizer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase813_AITrustStabilityClassifier(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase814_TrustCollapseProximityScanner(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase815_TrustViolationThresholdAdjuster(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase816_KernelSelfTrustReflector(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase817_TrustChainArbitrator(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase818_ThreadMultiPhaseTrustFusion(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase819_TrustCollapseRollbackEngine(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase820_EntropyOvertrustGuard(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase821_EntropyLockoutRecoveryPulse(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase822_TrustStallPhaseUnwinder(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase823_TrustEntropyWeightTrainer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase824_TrustNudgingEngine(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase825_TrustIntentFusionAgent(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase826_TrustBehaviorCorrupterShield(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase827_TrustRecoveryTrailEmitter(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase828_AITrustSealIssuer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase829_TrustSnapshotStreamPacker(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase830_FinalizeTrustMindBlockD(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase831_AITrustOverrideHandler(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase832_InterModuleTrustShadowDetector(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase833_MultiCoreTrustRebalancer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase834_HardwareAssistedTrustAccelerator(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase835_SystemTrustEntropyNormalizer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase836_TrustViolationBroadcastLimiter(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase837_TrustEntropySyncWithTelemetry(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase838_BootDNA_TrustPathAttacher(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase839_LastChanceTrustArbiter(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase840_EntropyDrivenTrustVelocityTracker(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase841_AITrustPathRebuilder(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase842_TrustLatencyBudgetController(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase843_TrustCorruptionParanoiaFence(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase844_AITrustConsensusVerifier(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase845_AITrustSuggestiveReplayAgent(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase846_TrustDriftCorrector(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase847_EntropyAlignedTrustStreamer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase848_ThreadTrustGhostScanner(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase849_ImmutableTrustStampEmitter(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase850_FinalizeTrustMind(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase851_InterKernelTrustChainEmitter(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase852_TelemetryAwareTrustRouteOptimizer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase853_BootTrustContinuityValidator(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase854_PeripheralTrustEchoSynchronizer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase855_NonlinearTrustDecaySimulator(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase856_RealTimeTSCTrustNormalizer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase857_TrustAwareSchedulerTokenIssuer(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase858_AITrustSealRegistrar(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase859_KernelTrustScoreExporterToDisplayCore(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase860_WriteUniversalRootTrustAnchor(KERNEL_CONTEXT *ctx);

EFI_STATUS TrustMind_Phase4151_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4152_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4153_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4154_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4155_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4156_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4157_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4158_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4159_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4160_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4161_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4162_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4163_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4164_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4165_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4166_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4167_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4168_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4169_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4170_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4171_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4172_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4173_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4174_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4175_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4176_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4177_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4178_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4179_Execute(KERNEL_CONTEXT *ctx);
EFI_STATUS TrustMind_Phase4180_Execute(KERNEL_CONTEXT *ctx);

#endif // TRUST_MIND_H
