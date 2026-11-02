Texture2D<float4> Color:register(t0);
Texture2D<float4> Flat:register(t1);
Texture2D<float4> Tiles:register(t2);
SamplerState Linear:register(s0);
SamplerState Point:register(s1);
cbuffer Parameters:register(b0){
 float4 LowRect,HighRect;
 float4 Sizes; // inverse color allocation, inverse flat allocation
 float4 Motion; // native low-grid motion parameters
 float4 Alignment; // guide jitter in low pixels, inverse tile allocation
 uint Quality;uint3 Pad;
};
float4 Vertex(uint i:SV_VertexID):SV_Position{
 float2 uv=float2((i<<1)&2,i&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
float2 limitView(float2 u,float2 size){return clamp(u,.5/size,1-.5/size);}
float2 colorUV(float2 u){return (HighRect.xy+limitView(u,HighRect.zw)*HighRect.zw)*Sizes.xy;}
float2 guideUV(float2 u){
 float2 p=LowRect.xy+u*LowRect.zw+Alignment.xy;
 return clamp(p,LowRect.xy+.5,LowRect.xy+LowRect.zw-.5)*Sizes.zw;
}
// The native low-grid kernel is evaluated on high color through View coordinates.
float noise(float2 p,float frame){
 p+=frame*float2(47,17)*.695;
 return frac(52.9829189*frac(dot(p,float2(.06711056,.00583715))));
}
float speed(float2 v){return min(v.x*Motion.y,Motion.w);}
float weight(float center,float depth,float offset,float a,float b,float scale){
 float2 z=saturate(.5+float2(1,-1)*(depth-center));
 return dot(z,saturate(scale*float2(a,b)-max(offset-1,0)));
}
float4 Blur(float4 p:SV_Position):SV_Target{
 float2 u=(p.xy-HighRect.xy)/HighRect.zw;
 float2 low=LowRect.xy+u*LowRect.zw+Alignment.xy;
 float2 guidePixel=clamp(floor(low),LowRect.xy,LowRect.xy+LowRect.zw-1);
 float2 random=float2(noise(guidePixel,0),noise(guidePixel,1));
 float2 tile=(guidePixel-LowRect.xy+.5)/16+(random-.5)*.5;
 float2 count=ceil(LowRect.zw/16);
 tile=clamp(tile,.5,count-.5);
 float4 range=Tiles.SampleLevel(Point,tile*Alignment.zw,0)*Motion.y;
 float4 center=Color.SampleLevel(Linear,colorUV(u),0);
 float longest=dot(range.zw,range.zw);
 if(longest<.25)return center;
 uint steps=(Quality==1?4u:Quality==2?6u:Quality==3?8u:16u)>>1;
 float scale=steps*rsqrt(longest);
 bool fast=dot(range.xy,range.xy)>.4*longest;
 float3 guide=Flat.SampleLevel(Point,guideUV(u),0).xyz;
 float centerSpeed=speed(guide.xy);
 float4 sum=0;float total=0;
 [loop]for(uint i=0;i<steps;i++){
  float2 lengths=float(i)+.5+float2(random.x-.5,.5-random.x);
  float2 a=u+(lengths.x/steps)*range.zw/LowRect.zw;
  float2 b=u-(lengths.y/steps)*range.zw/LowRect.zw;
  float4 ca=Color.SampleLevel(Linear,colorUV(a),0),cb=Color.SampleLevel(Linear,colorUV(b),0);
  if(fast){sum+=ca+cb;continue;}
  float3 ga=Flat.SampleLevel(Point,guideUV(a),0).xyz;
  float3 gb=Flat.SampleLevel(Point,guideUV(b),0).xyz;
  float va=speed(ga.xy),vb=speed(gb.xy);
  float wa=weight(guide.z,ga.z,lengths.x,centerSpeed,va,scale);
  float wb=weight(guide.z,gb.z,lengths.x,centerSpeed,vb,scale);
  bool2 mirror=bool2(ga.z>gb.z,vb>va);
  wa=all(mirror)?wb:wa;wb=any(mirror)?wb:wa;
  sum+=wa*ca+wb*cb;total+=wa+wb;
 }
 float factor=.5/steps;
 float4 result=fast?sum*factor:sum*factor+(1-total*factor)*center;
 result.a=0;return result;
}

