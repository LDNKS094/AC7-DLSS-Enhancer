/*
 * Equations adapted from NVIDIA RTX Remix.
 * Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
// AC7 extension transfers only the edit relative to the actual quantized proxy.
cbuffer Settings : register(b0) {uint2 Base;uint2 Size;float Exposure;float3 Padding;};
Texture2D<float4> Hdr : register(t0);
Texture2D<float4> Prepared : register(t1);
Texture2D<float4> Neural : register(t2);
RWTexture2D<float4> Output : register(u0);
float3 tone(float3 x) {return saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));}
[numthreads(8,8,1)] void Prepare(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=Size))return;
    float3 x=Hdr.Load(int3(Base+id.xy,0)).rgb;
    Output[id.xy]=float4(pow(tone(x*Exposure),1.0/2.2),1);
}
[numthreads(8,8,1)] void Restore(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=Size))return;
    float3 x=Hdr.Load(int3(Base+id.xy,0)).rgb;
    float3 p=Prepared.Load(int3(id.xy,0)).rgb,n=Neural.Load(int3(id.xy,0)).rgb;
    float3 e=x*Exposure,t=tone(e);
    float peak=max(e.r,max(e.g,e.b));
    float shoulder=pow(max(t.r,max(t.g,t.b)),1.0/2.2);
    float mask=smoothstep(1,8,peak)*smoothstep(.75,.99,shoulder);
    float3 y=x+(1-mask)*(pow(max(n,0),2.2)-pow(max(p,0),2.2))*(e/max(t,1e-6))/Exposure;
    // Withhold invalid model output; never pass NaN/negative values into later game passes.
    bool valid=all(isfinite(n))&&all(n>=0)&&all(n<=1)&&all(isfinite(y));
    Output[id.xy]=float4(valid?clamp(y,0,65504):x,1);
}
