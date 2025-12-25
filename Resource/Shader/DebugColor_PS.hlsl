cbuffer DBG : register(b3)
{
    float4 dbgColor;
};

float4 main() : SV_Target
{
    return dbgColor;
}
