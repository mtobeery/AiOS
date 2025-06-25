#include "kernel_shared.h"
#include "telemetry_mind.h"
#include "trust_mind.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

static UINT64 gSamples[64];
static UINTN  gSampleIdx = 0;
static UINT64 gPrevEntropy = 0;
static UINT64 gZeroTicks = 0;
static UINT64 gLastInjectionTick = 0;

static VOID RecordSample(UINT64 v) {
    gSamples[gSampleIdx % 64] = v;
    gSampleIdx++;
}

static VOID ComputeStats(UINTN count, UINT64 *mean, UINT64 *stddev) {
    if (count == 0) { *mean = *stddev = 0; return; }
    UINT64 sum = 0; for (UINTN i=0;i<count;i++) sum += gSamples[(gSampleIdx-i-1)%64];
    *mean = sum / count;
    UINT64 var = 0; for (UINTN i=0;i<count;i++){ INT64 d=gSamples[(gSampleIdx-i-1)%64]-*mean; var += (UINT64)(d*d); }
    *stddev = (UINT64)Sqrt64(var / count);
}

EFI_STATUS EntropyMind_Phase751_EvaluateSourceStrength(KERNEL_CONTEXT *ctx) {
    UINT64 src[3];
    src[0] = AsmReadTsc();
    src[1] = ctx->nvme_temperature ^ AsmReadTsc();
    src[2] = ctx->cpu_elapsed_tsc[0] ^ AsmReadTsc();
    for (UINTN i=0;i<3;i++) {
        UINTN bits=0; UINT64 v=src[i];
        for (UINTN b=0;b<64;b++) bits += (v>>b)&1ULL;
        ctx->entropy_source_score[i] = (UINT8)(bits%8);
    }
    RecordSample(src[0]^src[1]^src[2]);
    return EFI_SUCCESS;
}

EFI_STATUS EntropyMind_Phase752_CalibrateBaseline(KERNEL_CONTEXT *ctx) {
    UINT64 sum=0; UINT64 vals[64];
    for(UINTN i=0;i<64;i++){ vals[i]=AsmReadTsc(); sum+=vals[i]; RecordSample(vals[i]); }
    UINT64 mean=sum/64; UINT64 var=0; for(UINTN i=0;i<64;i++){ INT64 d=vals[i]-mean; var+=(UINT64)(d*d); }
    ctx->entropy_baseline.mean=mean;
    ctx->entropy_baseline.stddev=(UINT64)Sqrt64(var/64);
    return EFI_SUCCESS;
}

EFI_STATUS EntropyMind_Phase753_DetectSpikeAnomaly(KERNEL_CONTEXT *ctx) {
    UINT64 cur=AsmReadTsc();
    if(gPrevEntropy){ UINT64 diff=(cur>gPrevEntropy)?cur-gPrevEntropy:gPrevEntropy-cur; if(diff>4*ctx->entropy_baseline.stddev) Telemetry_LogEvent("EntropySpike",(UINTN)diff,(UINTN)AsmReadTsc()); }
    gPrevEntropy=cur; RecordSample(cur);
    return EFI_SUCCESS;
}

EFI_STATUS EntropyMind_Phase754_BuildHeatmapGrid(KERNEL_CONTEXT *ctx) {
    for(UINTN i=0;i<100;i++) ctx->entropy_heatmap[i/10][i%10]=gSamples[(gSampleIdx+63-i)%64];
    return EFI_SUCCESS;
}

EFI_STATUS EntropyMind_Phase755_TrackPredictiveVariance(KERNEL_CONTEXT *ctx) {
    UINT64 mean,std; ComputeStats(5,&mean,&std); UINT64 actual=gSamples[(gSampleIdx-1)%64]; UINT64 delta=(actual>mean)?actual-mean:mean-actual; ctx->entropy_prediction_delta[gSampleIdx%5]= (INT64)delta; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase756_IntegrateWithTrust(KERNEL_CONTEXT *ctx) {
    UINT64 mean,std; ComputeStats(5,&mean,&std); if(std*100>ctx->entropy_baseline.stddev*130){ ctx->trust_score -= ctx->trust_score/6; } else { ctx->trust_score += ctx->trust_score/20; } return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase757_ScanThermalEntropyLink(KERNEL_CONTEXT *ctx) {
    static INTN cpu[10]; static INTN gpu[10]; static UINT64 ent[10]; static UINTN idx=0; cpu[idx]=Telemetry_GetTemperature(); gpu[idx]=Telemetry_GetTemperature(); ent[idx]=ctx->EntropyScore; idx=(idx+1)%10; INT64 sumx=0,sumy=0,sumxy=0,sumx2=0,sumy2=0; for(UINTN i=0;i<10;i++){ sumx+=cpu[i]; sumy+=ent[i]; sumxy+=cpu[i]*ent[i]; sumx2+=cpu[i]*cpu[i]; sumy2+=ent[i]*ent[i]; } INT64 num=10*sumxy-sumx*sumy; INT64 den=Sqrt64((10*sumx2-sumx*sumx)*(10*sumy2-sumy*sumy)); ctx->entropy_thermal_correlation=(den)?(UINT64)(num*100/den):0; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase758_RescueFlatline(KERNEL_CONTEXT *ctx) {
    if(ctx->EntropyScore==0) gZeroTicks++; else gZeroTicks=0; if(gZeroTicks>=3){ UINT64 mix=AsmReadTsc()^ctx->nvme_temperature; ctx->EntropyScore^=mix; gZeroTicks=0; } return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase759_ScoreEntropyDensity(KERNEL_CONTEXT *ctx) {
    for(UINTN s=0;s<3;s++){ UINT64 h=0; for(UINTN i=0;i<512;i++){ UINT8 b=(UINT8)(AsmReadTsc()>>(i%8)); for(UINTN b2=0;b2<8;b2++) if((b>>b2)&1) h++; } ctx->entropy_density_score[s]=(UINT8)(h%8); } return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase760_LogRecoveryTiming(KERNEL_CONTEXT *ctx) {
    static UINT64 start=0; if(ctx->EntropyScore<10){ if(!start) start=AsmReadTsc(); } else if(start){ ctx->entropy_recovery_time=AsmReadTsc()-start; start=0; } return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase761_BuildPhaseInfluenceMap(KERNEL_CONTEXT *ctx) { ctx->entropy_phase_map[ctx->total_phases%100]=ctx->EntropyScore^ctx->trust_score; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase762_CalculateResilienceFactor(KERNEL_CONTEXT *ctx) { UINT64 diff=(ctx->trust_score>ctx->EntropyScore)?ctx->trust_score-ctx->EntropyScore:ctx->EntropyScore-ctx->trust_score; ctx->entropy_resilience_factor=(diff>0)?(UINT8)(10000/(diff+1)%100):100; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase763_EmitFusionPulse(KERNEL_CONTEXT *ctx) { Telemetry_LogEvent("EntropyFusion",(UINTN)ctx->EntropyScore,(UINTN)ctx->trust_score); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase764_RefineStabilityWindow(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(10,&mean,&std); if(std>ctx->entropy_baseline.stddev) ctx->entropy_stability_window=5; else ctx->entropy_stability_window=10; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase765_AnalyzeMicroDrift(KERNEL_CONTEXT *ctx) { UINT64 mean1,sd1; ComputeStats(50,&mean1,&sd1); UINT64 mean2,sd2; ComputeStats(25,&mean2,&sd2); ctx->entropy_micro_drift=(INT64)mean2-(INT64)mean1; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase766_PrepareShockBuffer(KERNEL_CONTEXT *ctx) { for(UINTN i=0;i<16;i++) ctx->entropy_shock_buffer[i]=AsmReadTsc(); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase767_EstimateBlindspots(KERNEL_CONTEXT *ctx) { static UINT64 last_trust=0; if(ctx->EntropyScore<20 && ctx->trust_score>last_trust+10) ctx->entropy_blindspot_flags[ctx->total_phases%10]=1; last_trust=ctx->trust_score; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase768_ForecastEntropyLoad(KERNEL_CONTEXT *ctx) { UINT64 sum=0; for(UINTN i=0;i<4;i++) sum+=ctx->scheduler_load_prediction[i]; ctx->entropy_load_forecast=sum; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase769_ModulateRecoveryPriority(KERNEL_CONTEXT *ctx) { if(ctx->EntropyScore<ctx->entropy_baseline.mean/2) ctx->background_priority=1; else ctx->background_priority=0; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase770_VerifyInjection(KERNEL_CONTEXT *ctx) { UINT64 chk=0; for(UINTN i=0;i<16;i++){ UINT8 b=(UINT8)(AsmReadTsc()>>i); chk+=b; } ctx->entropy_thermal_correlation=chk; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase771_LogStabilityPulse(KERNEL_CONTEXT *ctx) { if((ctx->total_phases%16)==0){ UINT64 mean,std; ComputeStats(16,&mean,&std); ctx->entropy_stability_pulse[(ctx->total_phases/16)%16]=mean^std; } return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase772_SyncTrustEntropyDrift(KERNEL_CONTEXT *ctx) { INT64 d=(INT64)ctx->trust_score-(INT64)ctx->EntropyScore; ctx->trust_entropy_curve+=d/10; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase773_ClassifyControlMode(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(10,&mean,&std); if(std<5) ctx->entropy_control_mode=0; else if(std<20) ctx->entropy_control_mode=1; else ctx->entropy_control_mode=2; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase774_ApplyHysteresisLimits(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(3,&mean,&std); if(std*100<mean*5) return EFI_SUCCESS; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase775_BalancePredictorWeights(KERNEL_CONTEXT *ctx) { for(UINTN i=0;i<5;i++) ctx->entropy_prediction_weights[i]=ctx->entropy_prediction_delta[i]; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase776_LinkAIReflexWithEntropy(KERNEL_CONTEXT *ctx) { if(ctx->entropy_stability_window<5) ctx->ai_scheduler_weight++; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase777_DetectSpiralPattern(KERNEL_CONTEXT *ctx) { static UINTN count=0; if(ctx->EntropyScore<gPrevEntropy && ctx->trust_score<ctx->meta_trust_score){ count++; if(count>=3) Telemetry_LogEvent("EntropyTrustSpiral",count,0); } else count=0; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase778_TriggerEmergencyProtocol(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(5,&mean,&std); if(ctx->EntropyScore==0 && std>ctx->entropy_baseline.stddev && gZeroTicks>5) ctx->scheduler_pressure_mode=TRUE; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase779_SynthesizeRiskIntoConfidence(KERNEL_CONTEXT *ctx) { UINT64 idx=(ctx->entropy_baseline.stddev>0)?(ctx->entropy_baseline.stddev/ctx->entropy_baseline.mean*100):0; if(idx<50) ctx->meta_confidence=(UINT8)(ctx->meta_confidence*80/100); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase780_FinalizeEntropyLogic(KERNEL_CONTEXT *ctx) { ctx->entropy_mind_ready=TRUE; Telemetry_LogEvent("EntropyFinal",1,0); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase781_AnalyzeOscillationPatterns(KERNEL_CONTEXT *ctx) { UINT64 sum=0; for(UINTN i=0;i<64;i++) sum+=gSamples[i]; ctx->entropy_micro_drift=sum/64; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase782_EmitRecoveryIndex(KERNEL_CONTEXT *ctx) { if(ctx->entropy_recovery_time) ctx->entropy_resilience_factor=(UINT8)(100000/ctx->entropy_recovery_time); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase783_DetectAnomalyClusters(KERNEL_CONTEXT *ctx) { static UINTN spikes=0; if(ctx->entropy_prediction_delta[gSampleIdx%5]>4*(INT64)ctx->entropy_baseline.stddev) spikes++; else spikes=0; if(spikes>=3) ctx->entropy_cluster_count++; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase784_RebalancePhaseWeights(KERNEL_CONTEXT *ctx) { for(UINTN i=0;i<5;i++) ctx->entropy_weights[i]=ctx->entropy_prediction_delta[i]; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase785_AnticipateEntropyBurst(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(3,&mean,&std); ctx->entropy_burst_prediction=std; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase786_MonitorReinjectionCooldown(KERNEL_CONTEXT *ctx) { if(ctx->total_phases-gLastInjectionTick<8) Telemetry_LogEvent("ReinjCool",1,0); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase787_MapContextualNoise(KERNEL_CONTEXT *ctx) { ctx->entropy_context_noise[0]=ctx->device_entropy_map[0]; ctx->entropy_context_noise[1]=ctx->io_entropy_buffer[0]; ctx->entropy_context_noise[2]=ctx->cpu_elapsed_tsc[0]; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase788_CertifyEntropyUncertainty(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(5,&mean,&std); ctx->ai_uncertainty_input=std*(UINT64)ctx->entropy_prediction_delta[(gSampleIdx-1)%5]; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase789_TraceAIReflection(KERNEL_CONTEXT *ctx) { ctx->entropy_ai_reflection[ctx->total_phases%10]=ctx->ai_state^ctx->EntropyScore; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase790_BalanceTrendSlope(KERNEL_CONTEXT *ctx) { if(ctx->entropy_micro_drift>8 || ctx->entropy_micro_drift<-8) ctx->entropy_stability_window=8; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase791_ScaleSystemConfidence(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(5,&mean,&std); if(std>ctx->entropy_baseline.stddev) ctx->meta_confidence/=2; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase792_CheckEntropyConvergence(KERNEL_CONTEXT *ctx) { UINT64 diff=(ctx->scheduler_entropy_buffer[0]>ctx->scheduler_entropy_buffer[1])?ctx->scheduler_entropy_buffer[0]-ctx->scheduler_entropy_buffer[1]:ctx->scheduler_entropy_buffer[1]-ctx->scheduler_entropy_buffer[0]; if(diff*100/ctx->scheduler_entropy_buffer[0]>15) Telemetry_LogEvent("EntropyConv",(UINTN)diff,0); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase793_ApplyTrustPenaltyFromEntropy(KERNEL_CONTEXT *ctx) { if(ctx->entropy_micro_drift>ctx->entropy_baseline.stddev) ctx->trust_score-=ctx->trust_score/2; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase794_ExpandForecastHorizon(KERNEL_CONTEXT *ctx) { static UINTN flat=0; if(ctx->entropy_micro_drift==0) flat++; else flat=0; if(flat>=10) ctx->entropy_stability_window+=2; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase795_EmitRecoveryScore(KERNEL_CONTEXT *ctx) { if(ctx->entropy_recovery_time){ UINT64 score=(ctx->ai_effectiveness*100)/ctx->entropy_recovery_time; Telemetry_LogEvent("EntRecScore",(UINTN)score,0); } return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase796_ClassifyThreatScenario(KERNEL_CONTEXT *ctx) { UINT64 metric=ctx->EntropyScore+ctx->nvme_temperature+ctx->io_miss_count; ctx->entropy_control_mode=(metric%3); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase797_SuppressOverreaction(KERNEL_CONTEXT *ctx) { if(ctx->entropy_prediction_delta[(gSampleIdx-1)%5]*10<ctx->entropy_baseline.mean && ctx->meta_trust_score==ctx->trust_score) return EFI_SUCCESS; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase798_LimitEntropySaturation(KERNEL_CONTEXT *ctx) { UINT64 sum=0; for(UINTN i=0;i<16;i++) sum+=ctx->scheduler_entropy_buffer[i]; if(sum>ctx->entropy_baseline.mean*9) ctx->scheduler_pressure_mode=TRUE; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase799_ReconcileEntropyMismatch(KERNEL_CONTEXT *ctx) { UINT64 mean,std; ComputeStats(5,&mean,&std); UINT64 pred=mean; UINT64 actual=gSamples[(gSampleIdx-1)%64]; UINT64 err=(pred>actual)?pred-actual:actual-pred; if(err*100>20*pred) ctx->entropy_stability_window++; return EFI_SUCCESS; }

EFI_STATUS EntropyMind_Phase800_FinalizeMind(KERNEL_CONTEXT *ctx) { ctx->entropy_mind_locked=TRUE; gZeroTicks=0; ZeroMem(gSamples,sizeof(gSamples)); Telemetry_LogEvent("EntropyMindLock",1,0); return EFI_SUCCESS; }

EFI_STATUS EntropyMind_RunAllPhases(KERNEL_CONTEXT *ctx) {
    EFI_STATUS Status = EFI_SUCCESS;
    for (UINTN phase = 751; phase <= 800; ++phase) {
        switch (phase) {
            case 751: Status = EntropyMind_Phase751_EvaluateSourceStrength(ctx); break;
            case 752: Status = EntropyMind_Phase752_CalibrateBaseline(ctx); break;
            case 753: Status = EntropyMind_Phase753_DetectSpikeAnomaly(ctx); break;
            case 754: Status = EntropyMind_Phase754_BuildHeatmapGrid(ctx); break;
            case 755: Status = EntropyMind_Phase755_TrackPredictiveVariance(ctx); break;
            case 756: Status = EntropyMind_Phase756_IntegrateWithTrust(ctx); break;
            case 757: Status = EntropyMind_Phase757_ScanThermalEntropyLink(ctx); break;
            case 758: Status = EntropyMind_Phase758_RescueFlatline(ctx); break;
            case 759: Status = EntropyMind_Phase759_ScoreEntropyDensity(ctx); break;
            case 760: Status = EntropyMind_Phase760_LogRecoveryTiming(ctx); break;
            case 761: Status = EntropyMind_Phase761_BuildPhaseInfluenceMap(ctx); break;
            case 762: Status = EntropyMind_Phase762_CalculateResilienceFactor(ctx); break;
            case 763: Status = EntropyMind_Phase763_EmitFusionPulse(ctx); break;
            case 764: Status = EntropyMind_Phase764_RefineStabilityWindow(ctx); break;
            case 765: Status = EntropyMind_Phase765_AnalyzeMicroDrift(ctx); break;
            case 766: Status = EntropyMind_Phase766_PrepareShockBuffer(ctx); break;
            case 767: Status = EntropyMind_Phase767_EstimateBlindspots(ctx); break;
            case 768: Status = EntropyMind_Phase768_ForecastEntropyLoad(ctx); break;
            case 769: Status = EntropyMind_Phase769_ModulateRecoveryPriority(ctx); break;
            case 770: Status = EntropyMind_Phase770_VerifyInjection(ctx); break;
            case 771: Status = EntropyMind_Phase771_LogStabilityPulse(ctx); break;
            case 772: Status = EntropyMind_Phase772_SyncTrustEntropyDrift(ctx); break;
            case 773: Status = EntropyMind_Phase773_ClassifyControlMode(ctx); break;
            case 774: Status = EntropyMind_Phase774_ApplyHysteresisLimits(ctx); break;
            case 775: Status = EntropyMind_Phase775_BalancePredictorWeights(ctx); break;
            case 776: Status = EntropyMind_Phase776_LinkAIReflexWithEntropy(ctx); break;
            case 777: Status = EntropyMind_Phase777_DetectSpiralPattern(ctx); break;
            case 778: Status = EntropyMind_Phase778_TriggerEmergencyProtocol(ctx); break;
            case 779: Status = EntropyMind_Phase779_SynthesizeRiskIntoConfidence(ctx); break;
            case 780: Status = EntropyMind_Phase780_FinalizeEntropyLogic(ctx); break;
            case 781: Status = EntropyMind_Phase781_AnalyzeOscillationPatterns(ctx); break;
            case 782: Status = EntropyMind_Phase782_EmitRecoveryIndex(ctx); break;
            case 783: Status = EntropyMind_Phase783_DetectAnomalyClusters(ctx); break;
            case 784: Status = EntropyMind_Phase784_RebalancePhaseWeights(ctx); break;
            case 785: Status = EntropyMind_Phase785_AnticipateEntropyBurst(ctx); break;
            case 786: Status = EntropyMind_Phase786_MonitorReinjectionCooldown(ctx); break;
            case 787: Status = EntropyMind_Phase787_MapContextualNoise(ctx); break;
            case 788: Status = EntropyMind_Phase788_CertifyEntropyUncertainty(ctx); break;
            case 789: Status = EntropyMind_Phase789_TraceAIReflection(ctx); break;
            case 790: Status = EntropyMind_Phase790_BalanceTrendSlope(ctx); break;
            case 791: Status = EntropyMind_Phase791_ScaleSystemConfidence(ctx); break;
            case 792: Status = EntropyMind_Phase792_CheckEntropyConvergence(ctx); break;
            case 793: Status = EntropyMind_Phase793_ApplyTrustPenaltyFromEntropy(ctx); break;
            case 794: Status = EntropyMind_Phase794_ExpandForecastHorizon(ctx); break;
            case 795: Status = EntropyMind_Phase795_EmitRecoveryScore(ctx); break;
            case 796: Status = EntropyMind_Phase796_ClassifyThreatScenario(ctx); break;
            case 797: Status = EntropyMind_Phase797_SuppressOverreaction(ctx); break;
            case 798: Status = EntropyMind_Phase798_LimitEntropySaturation(ctx); break;
            case 799: Status = EntropyMind_Phase799_ReconcileEntropyMismatch(ctx); break;
            case 800: Status = EntropyMind_Phase800_FinalizeMind(ctx); break;
            default: Status = EFI_INVALID_PARAMETER; break;
        }
        if (EFI_ERROR(Status)) {
            Telemetry_LogEvent("EntropyMindErr", phase, Status);
            return Status;
        }
        ctx->total_phases++;
    }
    return EFI_SUCCESS;
}

