#include "Shared.hlsli"

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    //===========================================
    
    float4 wpos = mul(float4(input.Pos, 1.0f), World);
    output.WorldPos = wpos.xyz;    
    output.PosH = mul(mul(wpos, View), Projection);        
    
    //===========================================
    
    output.NormalW = normalize(mul(float4(input.Norm, 0.0f), WorldInvTranspose).xyz);
    float sign = input.Tang.w; // w값(좌수 / 우수)임    
    output.TangentW = float4(normalize(mul(input.Tang.xyz, (float3x3) World)), sign);    
    
    output.Tex = input.Tex; // 이건 그냥 그대로 넘겨줌 여기서 쓰는거 아님

    //===========================================
    
    return output;
}