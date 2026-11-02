Texture2D<float> Depth : register(t0);
Texture2D<float2> PackedMotion : register(t1);
RWTexture2D<float> NormalizedDepth : register(u0);
RWTexture2D<uint2> NormalizedMotion : register(u1);
cbuffer Parameters : register(b0) { uint4 Rect; float4 Previous0, Previous1, Previous2, Previous3; float4 ProjectionJitter; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Rect.z || id.y >= Rect.w) return;
    uint2 at = id.xy + Rect.xy;
    float z = Depth.Load(int3(at,0));
    float2 encoded = PackedMotion.Load(int3(at,0));
    float2 motion = 0; uint valid = 3;
    if (isfinite(z) && z >= 0 && z <= 1) {
        if (encoded.x > 0) {
            motion = encoded * 4.008016 - 2.003978;
            valid = 2;
        } else {
            float2 current = (float2(id.xy) + .5) / float2(Rect.zw) * float2(2,-2) + float2(-1,1) - ProjectionJitter.xy;
            float4 previous = current.x * Previous0 + current.y * Previous1 + z * Previous2 + Previous3;
            if (all(isfinite(previous)) && abs(previous.w) > 1e-8) {
                motion = current - previous.xy / previous.w;
                if (all(isfinite(motion))) valid = 1;
            }
        }
    }
    NormalizedDepth[id.xy] = valid == 3 ? 0 : z;
    motion = valid == 3 ? float2(0,0) : motion;
    // Convert explicitly before UINT storage; scale NDC by this View's pixel dimensions.
    NormalizedMotion[id.xy] = uint2(f32tof16(motion.x*(-.5*Rect.z)),f32tof16(motion.y*(.5*Rect.w)));
}
