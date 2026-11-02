// Smoothstep density integration, as used by FoveatedDLSS5; horizontal only.
cbuffer Layout:register(b0){uint Width,Height,WorkWidth;float CenterHalf;float Transition,Edge;float2 Jitter;};
Texture2D<float4> Source0:register(t0);
Texture2D<float4> Source1:register(t1);
Texture2D<float4> Source2:register(t2);
RWTexture2D<float4> Target0:register(u0);
RWTexture2D<float> Target1:register(u1);
RWTexture2D<float2> Target2:register(u2);
SamplerState LinearClamp:register(s0);
float integral(float r){
    if(r<=CenterHalf)return r;
    float q=r-CenterHalf;
    if(Transition<=0)return CenterHalf+q*Edge;
    float t=min(q/Transition,1);
    return r-(1-Edge)*(Transition*(t*t*t-.5*t*t*t*t)+max(q-Transition,0));
}
float packX(float x){float r=x-Width*.5;return WorkWidth*.5+sign(r)*integral(abs(r));}
float unpackX(float x){
    float q=x-WorkWidth*.5,r=abs(q);
    if(r>CenterHalf){
        float end=CenterHalf+Transition*(1+Edge)*.5;
        if(Transition<=0||r>=end)r=CenterHalf+Transition+(r-end)/Edge;
        else{
            float y=(r-CenterHalf)/Transition,t=saturate(y*2/(1+Edge));
            [unroll]for(uint i=0;i<6;i++){
                float f=t-(1-Edge)*(t*t*t-.5*t*t*t*t)-y;
                float d=1-(1-Edge)*t*t*(3-2*t);t=saturate(t-f/d);
            }
            r=CenterHalf+Transition*t;
        }
    }
    return Width*.5+sign(q)*r;
}
float2 nativePoint(uint2 p){return float2(unpackX(p.x+.5-Jitter.x)+Jitter.x,p.y+.5);}
[numthreads(8,8,1)]void PackColor(uint3 id:SV_DispatchThreadID){
    if(id.x>=WorkWidth||id.y>=Height)return;
    float2 p=nativePoint(id.xy),size=float2(Width,Height);
    float footprint=unpackX(id.x+1-Jitter.x)-unpackX(id.x-Jitter.x);
    float delta=max(footprint-1,0)*.375;
    float4 c=Source0.SampleLevel(LinearClamp,p/size,0);
    if(delta>1e-4)c=.5*c+.25*(Source0.SampleLevel(LinearClamp,(p+float2(delta,0))/size,0)+Source0.SampleLevel(LinearClamp,(p-float2(delta,0))/size,0));
    Target0[id.xy]=c;
}
[numthreads(8,8,1)]void PackGuides(uint3 id:SV_DispatchThreadID){
    if(id.x>=WorkWidth||id.y>=Height)return;
    float2 p=nativePoint(id.xy);int2 at=clamp(int2(floor(p)),int2(0,0),int2(Width,Height)-1);
    float2 v=Source2.Load(int3(at,0)).xy;
    float u=p.x-Jitter.x;
    Target1[id.xy]=Source1.Load(int3(at,0)).x;
    Target2[id.xy]=float2(packX(u+v.x)-packX(u),v.y);
}
[numthreads(8,8,1)]void UnpackEdits(uint3 id:SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    float2 p=float2(packX(id.x+.5-Jitter.x)+Jitter.x,id.y+.5)/float2(WorkWidth,Height);
    float3 n=Source0.SampleLevel(LinearClamp,p,0).rgb,b=Source1.SampleLevel(LinearClamp,p,0).rgb;
    bool valid=all(isfinite(n))&&all(n>=0)&&all(n<=1)&&all(isfinite(b))&&all(b>=0)&&all(b<=1);
    float3 delta=pow(max(n,0),2.2)-pow(max(b,0),2.2);
    Target0[id.xy]=float4(valid&&all(isfinite(delta))?delta:float3(0,0,0),0);
}
