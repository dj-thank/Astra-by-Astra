// World-attached 32-sided transverse slices; U radius, V exhaust-axis position.
// Inputs: UV, PlumeAxial, Thrust, ThrusterRadiance, LayerEnergy,
// EyeExposure, DisplayRadiance, ViewAngleWeight, FictionalTint, PulsePhase.
float spool = saturate(Thrust);
float extent = 0.18 + 0.82 * sqrt(spool);
float axial = UV.y / max(extent, 0.001);
float4 axialTexel = Texture2DSample(PlumeAxial, PlumeAxialSampler,
    float2(0.5, saturate(axial)));
float radial = saturate(UV.x);
float edge = (1.0 - smoothstep(0.0, 1.0, radial));
float profile = lerp(axialTexel.r, axialTexel.g, 0.35);
float tip = 1.0 - smoothstep(0.94, 1.0, axial);
float alpha = profile * edge * tip * spool * (0.55 + 0.45 * spool) * saturate(ViewAngleWeight);
alpha *= exp2(-3.0*saturate(axial));
float exposure = clamp(EyeExposure, 0.000001, 64.0);
float target = max(DisplayRadiance, 0.0);
float radiance = min(max(max(ThrusterRadiance, 0.0), target / exposure),
    min(1.5 * target / exposure, 2000000.0));
float warmCore = exp(-6.0 * radial * radial) * (1.0 - 0.65 * saturate(axial));
float3 tint = lerp(float3(0.30,0.52,1.0), float3(1.0,0.69,0.34), warmCore);
float pulse = 1.0 + 0.025 * sin(PulsePhase * 11.0);
return float4(lerp(float3(1.0,1.0,1.0),tint,saturate(FictionalTint)) * radiance *
    max(LayerEnergy,0.0) * pulse, saturate(alpha));
