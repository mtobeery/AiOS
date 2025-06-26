#ifndef POWER_MIND_H
#define POWER_MIND_H

#include <Uefi.h>
#include "kernel_shared.h"

EFI_STATUS PowerMind_Phase901_ModelBatteryDischargeCurve(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase902_IntegrateEntropyTrust(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase903_ThrottlePhasesByPower(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase904_ControlSmartTDP(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase905_EvaluateIntentAlignment(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase906_DetectDrainAnomaly(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase907_VerifyVoltageStability(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase908_TuneForIdle(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase909_ForecastPhaseDemand(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase910_CalculateBatteryTrustIndex(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase911_LogEnergyTrustDeviation(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase912_TriggerSafeMode(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase913_MapReflectionCausality(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase914_EstimateChargeSlope(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase915_ModelEntropyImpact(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase916_SyncWithVoltageEvents(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase917_EmitFusionTelemetry(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase918_CompensatePowerDrift(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase919_ValidateThermalCoupling(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase920_ForecastIntentCost(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase921_IsolatePhaseEnergyAnomaly(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase922_SynthesizeConfidenceScore(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase923_AllocatePhasePower(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase924_OrchestrateSleepDecision(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase925_ScanBlindspots(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase926_OptimizeTrustPowerCost(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase927_ActivateOverpowerPrevention(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase928_LogConvergence(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase929_ApplyBackoffTimer(KERNEL_CONTEXT *ctx);
EFI_STATUS PowerMind_Phase930_FinalizePowerMind(KERNEL_CONTEXT *ctx);

EFI_STATUS PowerMind_RunAllPhases(KERNEL_CONTEXT *ctx);

#endif // POWER_MIND_H
