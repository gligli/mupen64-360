struct _IN
{
	float2 uv0: TEXCOORD0;
	float2 uv1: TEXCOORD1;
	float4 col : COLOR;
};

float4 main(_IN data): COLOR {
    return data.col;
}
