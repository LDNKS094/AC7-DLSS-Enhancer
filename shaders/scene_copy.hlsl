cbuffer C:register(b0){uint left,top,width,height,pad0,pad1,pad2,pad3;};
Texture2D<float4> Input:register(t0);Texture2D<float4> Low:register(t1);SamplerState Linear:register(s0);RWTexture2D<float4> Converted:register(u0);
[numthreads(8,8,1)]void Convert(uint3 p:SV_DispatchThreadID){if(p.x<width&&p.y<height){float4 c=Input.Load(int3(p.xy+uint2(left,top),0));Converted[p.xy]=c;}}
struct V{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V Vertex(uint id:SV_VertexID){V v;v.uv=float2((id<<1)&2,id&2);v.pos=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float4 Pixel(V v):SV_Target{float4 low=Low.SampleLevel(Linear,v.uv,0);float3 c=Input.SampleLevel(Linear,v.uv,0).rgb;c=all(isfinite(c))?c:low.rgb;return float4(c,low.a);}
