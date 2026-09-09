// Root candidate Custom node: TextureObject PlumeAxial, float2 UV,
// float Thrust, float ThrusterRadiance, float LayerEnergy (start .025).
// Only for plume_disks.json UV0: U radial fraction, V axial position.
float spool = saturate(Thrust);
float extent = 0.18 + 0.82 * sqrt(spool);
float axial = UV.y / max(extent, 0.001);
float profile = Texture2DSample(PlumeAxial, PlumeAxialSampler,
    float2(0.5, saturate(axial))).r;
float edge = 1.0 - smoothstep(0.05, 1.0, saturate(UV.x));
float tip = 1.0 - smoothstep(0.94, 1.0, axial);
float alpha = profile * edge * tip * spool * (0.55 + 0.45 * spool);
return float4(float3(0.55,0.7,1.0) * max(ThrusterRadiance,0.0) * max(LayerEnergy,0.0), saturate(alpha));
