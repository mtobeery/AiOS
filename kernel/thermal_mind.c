#include "kernel_shared.h"
#include "telemetry_mind.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

static UINTN gCpuTemps[128];
static UINTN gGpuTemps[128];
static UINTN gTempIdx = 0;
static UINT8 gCoolingBias = 0;
static UINT8 gSpikeCount = 0;
static UINT8 gFailoverTicks = 0;
static UINT8 gHeatLimitTicks = 0;
static UINT8 gRampBlock = 0;
static UINT8 gTrustSuppress = 0;
static UINTN gLastCoreTemps[8];

static VOID RecordTemps(UINTN cpu, UINTN gpu) {
    gCpuTemps[gTempIdx % 128] = cpu;
    gGpuTemps[gTempIdx % 128] = gpu;
    gTempIdx++;
}

static UINT64 ComputeMean(const UINTN *buf, UINTN count) {
    UINT64 sum = 0;
    for (UINTN i = 0; i < count; ++i) sum += buf[i];
    return sum / count;
}

static UINT64 ComputeStd(const UINTN *buf, UINTN count, UINT64 mean) {
    UINT64 var = 0;
    for (UINTN i = 0; i < count; ++i) {
        INT64 d = (INT64)buf[i] - (INT64)mean;
        var += (UINT64)(d * d);
    }
    return (UINT64)Sqrt64(var / count);
}

EFI_STATUS ThermalMind_Phase801_InitializeThermalBaseline(KERNEL_CONTEXT *ctx) {
    for (UINTN i = 0; i < 100; ++i) {
        UINTN c = Telemetry_GetTemperature();
        UINTN g = Telemetry_GetTemperature();
        gCpuTemps[i] = c;
        gGpuTemps[i] = g;
    }
    UINT64 cpu_mean = ComputeMean(gCpuTemps, 100);
    UINT64 gpu_mean = ComputeMean(gGpuTemps, 100);
    UINT64 cpu_std = ComputeStd(gCpuTemps, 100, cpu_mean);
    UINT64 gpu_std = ComputeStd(gGpuTemps, 100, gpu_mean);
    ctx->thermal_baseline.cpu_mean = cpu_mean;
    ctx->thermal_baseline.gpu_mean = gpu_mean;
    ctx->thermal_baseline.cpu_stddev = cpu_std;
    ctx->thermal_baseline.gpu_stddev = gpu_std;
    gTempIdx = 0;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase802_DetectGradientSpike(KERNEL_CONTEXT *ctx) {
    UINTN cpu = Telemetry_GetTemperature();
    UINTN gpu = Telemetry_GetTemperature();
    UINTN prev = gCpuTemps[(gTempIdx + 127) % 128];
    RecordTemps(cpu, gpu);
    if ((cpu > prev && cpu - prev > 10) || (prev > cpu && prev - cpu > 10)) {
        gSpikeCount++;
        gCoolingBias = 1;
        Telemetry_LogEvent("ThermSpike", cpu, gpu);
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase803_ModulateTrustWithHeat(KERNEL_CONTEXT *ctx) {
    UINTN cpu = gCpuTemps[(gTempIdx + 127) % 128];
    UINTN gpu = gGpuTemps[(gTempIdx + 127) % 128];
    if (cpu > 80 || gpu > 80) {
        ctx->meta_confidence = (UINT8)(ctx->meta_confidence * 70 / 100);
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase804_ScalePhaseExecution(KERNEL_CONTEXT *ctx) {
    if (ctx->thermal_rise_rate > 5) {
        UINTN delay = (ctx->thermal_rise_rate > 20) ? 5 : 1;
        MicroSecondDelay(delay * 1000);
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase805_CorrelateEntropyDisruption(KERNEL_CONTEXT *ctx) {
    UINTN idx = (gTempIdx + 127) % 128;
    UINTN prev = gCpuTemps[idx];
    UINTN cur = gCpuTemps[(idx + 1) % 128];
    if ((cur > prev ? cur - prev : prev - cur) > 7) {
        UINT64 eprev = ctx->entropy_phase_map[(ctx->total_phases + 99) % 100];
        UINT64 ecur = ctx->entropy_phase_map[ctx->total_phases % 100];
        if (eprev && ecur && eprev > ecur * 120 / 100)
            Telemetry_LogEvent("ThermEnt", (UINTN)(eprev - ecur), cur);
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase806_EvaluateSafetyMargin(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    UINTN margin = (100 > cur) ? (100 - cur) : 0;
    UINTN pct = margin * 100 / 100;
    if (pct < 10)
        Telemetry_LogEvent("ThermMargin", pct, cur);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase807_TraceThermalInfluence(KERNEL_CONTEXT *ctx) {
    UINT8 zone = (gCpuTemps[(gTempIdx + 127) % 128] > 85) ? 1 : 0;
    Telemetry_LogEvent("ThermInflu", zone, ctx->ai_state);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase808_AnalyzeThermalLatency(KERNEL_CONTEXT *ctx) {
    static UINT64 base = 0;
    UINT64 tsc = AsmReadTsc();
    if (!base)
        base = tsc;
    UINT64 diff = tsc - base;
    UINT64 pct = (base) ? diff * 100 / base : 0;
    Telemetry_LogEvent("ThermLat", (UINTN)pct, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase809_EmitResilienceScore(KERNEL_CONTEXT *ctx) {
    UINT64 under = 0;
    for (UINTN i = 0; i < gTempIdx && i < 128; ++i)
        if (gCpuTemps[i] < 85) under++;
    UINT64 score = 0;
    if (gTempIdx) {
        UINT64 slope = (UINT64)AbsoluteValue64(ctx->trust_slope_buffer[0]);
        score = under * 100 / gTempIdx;
        score = (score * (100 - slope % 100)) / 100;
    }
    Telemetry_LogEvent("ThermRes", (UINTN)score, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase810_ClusterThermalAnomalies(KERNEL_CONTEXT *ctx) {
    static UINT8 window[20];
    static UINTN idx = 0;
    window[idx % 20] = (gCoolingBias != 0);
    idx++;
    UINTN spikes = 0;
    for (UINTN i = 0; i < 20; ++i) spikes += window[i];
    if (spikes > 3)
        Telemetry_LogEvent("ThermCluster", spikes, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase811_TrainThermalCurveModel(KERNEL_CONTEXT *ctx) {
    UINT64 mean = ComputeMean(gCpuTemps, (gTempIdx < 64) ? gTempIdx : 64);
    ctx->thermal_rise_rate = (UINT64)mean;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase812_ModulateIntentWithHeat(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 90 && ctx->ai_scheduler_weight > 0)
        ctx->ai_scheduler_weight = ctx->ai_scheduler_weight * 85 / 100;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase813_AdviseIOThrottling(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 85)
        ctx->background_priority = 1;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase814_NormalizeTrustSpikes(KERNEL_CONTEXT *ctx) {
    static UINT64 last_trust = 0;
    UINT64 cur = ctx->trust_score;
    UINTN curt = gCpuTemps[(gTempIdx + 127) % 128];
    UINTN prevt = gCpuTemps[(gTempIdx + 126) % 128];
    if (cur > last_trust + 10 && prevt > curt + 15)
        ctx->trust_score = ctx->trust_score * 8 / 10;
    last_trust = ctx->trust_score;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase815_RankRootCauses(KERNEL_CONTEXT *ctx) {
    UINT64 cpu = gCpuTemps[(gTempIdx + 127) % 128];
    UINT64 gpu = gGpuTemps[(gTempIdx + 127) % 128];
    UINT64 io = ctx->io_miss_count;
    UINT64 mem = ctx->DescriptorCount;
    UINT64 scores[4] = {cpu, gpu, io, mem};
    Telemetry_LogEvent("ThermRoot", (UINTN)scores[0], (UINTN)scores[1]);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase816_LinkToPowerMind(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 88)
        Telemetry_LogEvent("ThermPower", cur, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase817_ResyncIntentUnderHeat(KERNEL_CONTEXT *ctx) {
    if (gHeatLimitTicks > 10) {
        ctx->ai_history[0] = gHeatLimitTicks;
        gHeatLimitTicks = 0;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase818_BuildEntropyThermalMap(KERNEL_CONTEXT *ctx) {
    UINTN t = gCpuTemps[(gTempIdx + 127) % 128];
    UINT64 e = ctx->EntropyScore;
    UINTN row = t % 10;
    UINTN col = e % 10;
    ctx->entropy_heatmap[row][col] = (ctx->entropy_heatmap[row][col] + e) / 2;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase819_SelfCorrectAIUnderHeat(KERNEL_CONTEXT *ctx) {
    static UINT8 hot = 0;
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 90) {
        hot++;
        if (hot > 20) {
            ctx->ai_scheduler_weight = ctx->ai_scheduler_weight * 75 / 100;
            ctx->ai_prediction_cache[0] = ctx->ai_prediction_cache[0] * 75 / 100;
        }
    } else {
        hot = 0;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase820_FinalizeThermalLogic(KERNEL_CONTEXT *ctx) {
    ctx->thermal_mind_ready = TRUE;
    Telemetry_LogEvent("ThermReady", gTempIdx, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase821_EmitThermalStressForecast(KERNEL_CONTEXT *ctx) {
    UINT64 slope = ctx->thermal_rise_rate;
    for (UINTN i = 0; i < 20; ++i)
        ctx->thermal_forecast[i] = slope * (i + 1);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase822_ClassifyHotZone(KERNEL_CONTEXT *ctx) {
    UINT64 cpu = gCpuTemps[(gTempIdx + 127) % 128];
    UINT64 gpu = gGpuTemps[(gTempIdx + 127) % 128];
    ctx->thermal_hot_zone_id = (cpu > gpu) ? 0 : 1;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase823_ActivateTrustShield(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 95) {
        gTrustSuppress = 5;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase824_AdjustConfidenceBands(KERNEL_CONTEXT *ctx) {
    UINT64 score = ctx->thermal_forecast[0];
    if (score > 75)
        ctx->meta_confidence = (UINT8)(ctx->meta_confidence * 80 / 100);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase825_ThrottleEntropyPhases(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 85) {
        for (UINTN i = 0; i < 5; ++i)
            if (ctx->entropy_prediction_weights[i] > 8)
                ctx->entropy_prediction_weights[i] -= 1;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase826_EmitImpactScores(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    ctx->thermal_phase_impact_map[ctx->total_phases % 100] = cur;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase827_ApplyHysteresisGate(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    UINTN prev = gCpuTemps[(gTempIdx + 126) % 128];
    if (prev > cur + 20)
        gRampBlock = 8;
    if (gRampBlock)
        gRampBlock--;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase828_LimitThermalReflex(KERNEL_CONTEXT *ctx) {
    static UINT8 last_conf = 100;
    UINT8 conf = ctx->meta_confidence;
    UINTN delta = (conf > last_conf) ? (conf - last_conf) : (last_conf - conf);
    UINTN tdiff = (gCpuTemps[(gTempIdx + 127) % 128] > gCpuTemps[(gTempIdx + 126) % 128]) ?
                  (gCpuTemps[(gTempIdx + 127) % 128] - gCpuTemps[(gTempIdx + 126) % 128]) :
                  (gCpuTemps[(gTempIdx + 126) % 128] - gCpuTemps[(gTempIdx + 127) % 128]);
    if (delta > 10 && tdiff < 5)
        ctx->meta_confidence = last_conf;
    last_conf = ctx->meta_confidence;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase829_ModelTrustTriangle(KERNEL_CONTEXT *ctx) {
    if ((gTempIdx % 16) == 0) {
        UINT64 signal = ctx->EntropyScore ^ ctx->trust_score ^ gCpuTemps[(gTempIdx + 127) % 128];
        if ((signal & 1) == 0)
            ctx->trust_score += 1;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase830_VerifyCoreSymmetry(KERNEL_CONTEXT *ctx) {
    UINT64 sum = 0;
    for (UINTN i = 0; i < 4; ++i) {
        UINTN t = Telemetry_GetTemperature();
        sum += t;
        gLastCoreTemps[i] = t;
    }
    UINT64 avg = sum / 4;
    for (UINTN i = 0; i < 4; ++i) {
        UINT64 d = (gLastCoreTemps[i] > avg) ? gLastCoreTemps[i] - avg : avg - gLastCoreTemps[i];
        if (d > 10)
            Telemetry_LogEvent("CoreAsym", i, (UINTN)d);
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase831_GenerateFingerprint(KERNEL_CONTEXT *ctx) {
    UINT64 hash = 0;
    for (UINTN i = 0; i < 10; ++i)
        hash ^= gCpuTemps[(gTempIdx + 128 - i - 1) % 128] * (i + 1);
    hash ^= ctx->EntropyScore ^ ctx->io_miss_count;
    ctx->thermal_phase_impact_map[ctx->total_phases % 100] ^= hash;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase832_TriggerFailover(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 99) {
        gFailoverTicks++;
        if (gFailoverTicks >= 3)
            ctx->thermal_safe_mode = TRUE;
    } else {
        gFailoverTicks = 0;
    }
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase833_AnticipateGPUHeatWave(KERNEL_CONTEXT *ctx) {
    UINT64 load = ctx->ai_scheduler_weight + ctx->EntropyScore;
    if (load > 50)
        Telemetry_LogEvent("GPUPreCool", (UINTN)load, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase834_TraceReflectionEffect(KERNEL_CONTEXT *ctx) {
    Telemetry_LogEvent("ThermRefl", gCpuTemps[(gTempIdx + 127) % 128], ctx->ai_state);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase835_EscalateSafeMode(KERNEL_CONTEXT *ctx) {
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur > 95)
        gHeatLimitTicks++;
    else
        gHeatLimitTicks = 0;
    if (gHeatLimitTicks > 5)
        ctx->thermal_safe_mode = TRUE;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase836_EmitDiscrepancyAlert(KERNEL_CONTEXT *ctx) {
    UINTN core = Telemetry_GetTemperature();
    UINTN pkg = Telemetry_GetTemperature();
    UINTN diff = (core > pkg) ? core - pkg : pkg - core;
    if (diff > 15)
        Telemetry_LogEvent("ThermDisc", diff, 0);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase837_PlanReentryTiming(KERNEL_CONTEXT *ctx) {
    static UINT8 stable = 0;
    UINTN cur = gCpuTemps[(gTempIdx + 127) % 128];
    if (cur < ctx->thermal_baseline.cpu_mean)
        stable++;
    else
        stable = 0;
    if (stable >= 20)
        gRampBlock = 0;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase838_EncodeThermalContext(KERNEL_CONTEXT *ctx) {
    UINTN cpu = gCpuTemps[(gTempIdx + 127) % 128];
    UINTN gpu = gGpuTemps[(gTempIdx + 127) % 128];
    ctx->ai_entropy_input[0] = cpu;
    ctx->ai_entropy_input[1] = gpu;
    ctx->ai_entropy_input[2] = (cpu > gpu) ? cpu - gpu : gpu - cpu;
    ctx->ai_entropy_input[3] = ctx->thermal_rise_rate;
    ctx->ai_entropy_input[4] = ctx->meta_confidence;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase839_TuneAIForHeatAvoidance(KERNEL_CONTEXT *ctx) {
    static UINT64 last_decision = 0;
    UINT64 cur = ctx->ai_history[0];
    if (cur > last_decision && gCpuTemps[(gTempIdx + 127) % 128] > ctx->thermal_baseline.cpu_mean)
        ctx->ai_scheduler_weight = ctx->ai_scheduler_weight * 80 / 100;
    last_decision = cur;
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_Phase840_FinalizeExecution(KERNEL_CONTEXT *ctx) {
    ctx->thermal_mind_finalized = TRUE;
    for (UINTN i = 0; i < 50 && i < gTempIdx; ++i)
        Telemetry_LogEvent("ThermHist", gCpuTemps[(gTempIdx + 128 - i - 1) % 128], i);
    return EFI_SUCCESS;
}

EFI_STATUS ThermalMind_RunAllPhases(KERNEL_CONTEXT *ctx) {
    EFI_STATUS Status = EFI_SUCCESS;
    for (UINTN phase = 801; phase <= 840; ++phase) {
        switch (phase) {
            case 801: Status = ThermalMind_Phase801_InitializeThermalBaseline(ctx); break;
            case 802: Status = ThermalMind_Phase802_DetectGradientSpike(ctx); break;
            case 803: Status = ThermalMind_Phase803_ModulateTrustWithHeat(ctx); break;
            case 804: Status = ThermalMind_Phase804_ScalePhaseExecution(ctx); break;
            case 805: Status = ThermalMind_Phase805_CorrelateEntropyDisruption(ctx); break;
            case 806: Status = ThermalMind_Phase806_EvaluateSafetyMargin(ctx); break;
            case 807: Status = ThermalMind_Phase807_TraceThermalInfluence(ctx); break;
            case 808: Status = ThermalMind_Phase808_AnalyzeThermalLatency(ctx); break;
            case 809: Status = ThermalMind_Phase809_EmitResilienceScore(ctx); break;
            case 810: Status = ThermalMind_Phase810_ClusterThermalAnomalies(ctx); break;
            case 811: Status = ThermalMind_Phase811_TrainThermalCurveModel(ctx); break;
            case 812: Status = ThermalMind_Phase812_ModulateIntentWithHeat(ctx); break;
            case 813: Status = ThermalMind_Phase813_AdviseIOThrottling(ctx); break;
            case 814: Status = ThermalMind_Phase814_NormalizeTrustSpikes(ctx); break;
            case 815: Status = ThermalMind_Phase815_RankRootCauses(ctx); break;
            case 816: Status = ThermalMind_Phase816_LinkToPowerMind(ctx); break;
            case 817: Status = ThermalMind_Phase817_ResyncIntentUnderHeat(ctx); break;
            case 818: Status = ThermalMind_Phase818_BuildEntropyThermalMap(ctx); break;
            case 819: Status = ThermalMind_Phase819_SelfCorrectAIUnderHeat(ctx); break;
            case 820: Status = ThermalMind_Phase820_FinalizeThermalLogic(ctx); break;
            case 821: Status = ThermalMind_Phase821_EmitThermalStressForecast(ctx); break;
            case 822: Status = ThermalMind_Phase822_ClassifyHotZone(ctx); break;
            case 823: Status = ThermalMind_Phase823_ActivateTrustShield(ctx); break;
            case 824: Status = ThermalMind_Phase824_AdjustConfidenceBands(ctx); break;
            case 825: Status = ThermalMind_Phase825_ThrottleEntropyPhases(ctx); break;
            case 826: Status = ThermalMind_Phase826_EmitImpactScores(ctx); break;
            case 827: Status = ThermalMind_Phase827_ApplyHysteresisGate(ctx); break;
            case 828: Status = ThermalMind_Phase828_LimitThermalReflex(ctx); break;
            case 829: Status = ThermalMind_Phase829_ModelTrustTriangle(ctx); break;
            case 830: Status = ThermalMind_Phase830_VerifyCoreSymmetry(ctx); break;
            case 831: Status = ThermalMind_Phase831_GenerateFingerprint(ctx); break;
            case 832: Status = ThermalMind_Phase832_TriggerFailover(ctx); break;
            case 833: Status = ThermalMind_Phase833_AnticipateGPUHeatWave(ctx); break;
            case 834: Status = ThermalMind_Phase834_TraceReflectionEffect(ctx); break;
            case 835: Status = ThermalMind_Phase835_EscalateSafeMode(ctx); break;
            case 836: Status = ThermalMind_Phase836_EmitDiscrepancyAlert(ctx); break;
            case 837: Status = ThermalMind_Phase837_PlanReentryTiming(ctx); break;
            case 838: Status = ThermalMind_Phase838_EncodeThermalContext(ctx); break;
            case 839: Status = ThermalMind_Phase839_TuneAIForHeatAvoidance(ctx); break;
            case 840: Status = ThermalMind_Phase840_FinalizeExecution(ctx); break;
            default: Status = EFI_INVALID_PARAMETER; break;
        }
        if (EFI_ERROR(Status)) {
            Telemetry_LogEvent("ThermMindErr", phase, Status);
            return Status;
        }
        if (gTrustSuppress)
            gTrustSuppress--;
        ctx->total_phases++;
    }
    return EFI_SUCCESS;
}

