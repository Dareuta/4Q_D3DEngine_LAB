cbuffer DBG : register(b3)
{
    float4 dbgColor;   // 고정 출력색 (RGBA, linear)
};

float4 main() : SV_Target
{
    return dbgColor;
}