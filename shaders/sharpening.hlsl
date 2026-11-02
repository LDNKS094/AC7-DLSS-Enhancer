// AMD FSR1 RCAS with local HDR normalization and a direct 0..1 amount.

/*
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
Texture2D<float4> Input:register(t0);
RWTexture2D<float4> Output:register(u0);
cbuffer C:register(b0){uint width,height;float amount;uint hdr;};
float3 tap(int2 p){return Input.Load(int3(clamp(p,int2(0,0),int2(width,height)-1),0)).rgb;}
[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){
    if(p.x>=width||p.y>=height)return;
    int2 at=int2(p.xy);float4 original=Input.Load(int3(at,0));
    if(!all(isfinite(original.rgb))){Output[p.xy]=original;return;}
    float3 b=tap(at+int2(0,-1)),d=tap(at+int2(-1,0)),f=tap(at+int2(1,0)),h=tap(at+int2(0,1));
    if(!all(isfinite(b))||!all(isfinite(d))||!all(isfinite(f))||!all(isfinite(h))){Output[p.xy]=original;return;}
    float3 peak=max(original.rgb,max(max(b,d),max(f,h)));
    float scale=hdr?max(1,max(peak.r,max(peak.g,peak.b))):1;
    float3 e=saturate(original.rgb/scale);b=saturate(b/scale);d=saturate(d/scale);f=saturate(f/scale);h=saturate(h/scale);
    float3 mn=min(min(b,d),min(f,h)),mx=max(max(b,d),max(f,h));
    float3 hitMin=min(mn,e)/max(4*mx,1e-6);
    float3 hitMax=(1-max(mx,e))/min(4*mn-4,-1e-6);
    float3 limit=max(-hitMin,hitMax);
    float lobe=max(-.1875,min(max(limit.r,max(limit.g,limit.b)),0))*amount;
    float3 luma=float3(.5,1,.5);float bl=dot(b,luma),dl=dot(d,luma),el=dot(e,luma),fl=dot(f,luma),hl=dot(h,luma);
    float range=max(max(max(bl,dl),max(fl,hl)),el)-min(min(min(bl,dl),min(fl,hl)),el);
    float noise=saturate(abs(.25*(bl+dl+fl+hl)-el)/max(range,1e-6));
    lobe*=1-.5*noise;
    float3 result=(e+lobe*(b+d+f+h))/(1+4*lobe);
    Output[p.xy]=float4(all(isfinite(result))?saturate(result)*scale:original.rgb,original.a);
}
