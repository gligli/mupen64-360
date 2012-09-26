struct _IN
{
	float2 uv0: TEXCOORD0;
	float2 uv1: TEXCOORD1;
	float4 col : COLOR;
};

sampler tex0: register(s0);

bool use_tex: register(b0);

float4 main(_IN data): COLOR {

	if(use_tex)
	{
		return tex2D(tex0,data.uv0);
	}
	else
	{
		return data.col;
	}
}
