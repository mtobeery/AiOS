#ifndef KERNEL_MIND_H
#define KERNEL_MIND_H

#include <Uefi.h>
#include "kernel_shared.h"

EFI_STATUS KernelMind_Phase951_BootstrapSelfAwareness(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase952_EvaluateTrustSlope(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase953_TrackMetaGoals(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase954_FuseSubsystemTrust(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase955_BuildCognitionGraph(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase956_ReflectIntrospectively(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase957_DetectTrustContradiction(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase958_SynthesizeConsciousState(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase959_BroadcastMetaTelemetry(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase960_MonitorSelfTrustStability(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase961_ResolveGoalConflict(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase962_ScorePhaseAwarenessDelta(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase963_TrackGoalMomentum(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase964_SelectConsciousPhase(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase965_RegulateSelfEntropy(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase966_GuardConsciousLoop(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase967_TuneSelfRecoveryBias(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase968_BuildTrustEntropyMap(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase969_EmitStabilityScore(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase970_DiagnoseGoalFailures(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase971_FinalizeFeedbackLoop(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase972_LimitReflectionVolatility(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase973_ComposeEgoVector(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase974_EmitExistentialCheckpoint(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase975_ExpandForecastHorizon(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase976_PreventTrustCollapse(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase977_ResyncIntent(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase978_AdvisePhaseOptimization(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase979_FinalizeMetaCognition(KERNEL_CONTEXT *ctx);
EFI_STATUS KernelMind_Phase980_EmitAwakeningSignal(KERNEL_CONTEXT *ctx);

EFI_STATUS KernelMind_RunAllPhases(KERNEL_CONTEXT *ctx);

#endif // KERNEL_MIND_H
