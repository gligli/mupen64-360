struct _IN
{
	float2 uv0: TEXCOORD0;
	float2 uv1: TEXCOORD1;
	float4 col : COLOR;
};

sampler tex0: register(s0);

float4 main(_IN data): COLOR {

	float4 t=tex2D(tex0,data.uv0);

/*	if(t.a)
		return t;*/

	if(!t.r && !t.g && !t.b && !t.a)
		t=float4(1,1,1,1);

	return data.col*t;
}
