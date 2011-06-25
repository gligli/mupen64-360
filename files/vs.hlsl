struct _IN
{
  float4 pos: POSITION;
  float2 uv0: TEXCOORD0;
  float2 uv1: TEXCOORD1;
  float4 col: COLOR;
};

struct _OUT
{
  float4 pos: POSITION;
  float2 uv0: TEXCOORD0;
  float2 uv1: TEXCOORD1;
  float4 col: COLOR;
};

float4x4 persp: register (c0);

_OUT main(_IN In )
{
  _OUT Out;

/*#define NEAR (-1.0)
#define FAR  (1.0)

float4x4 persp = {
	{1,0,0,0},
	{0,1,0,0},
	{0,0,FAR/(NEAR-FAR),0},
	{0,0,NEAR*FAR/(NEAR-FAR),1},
};*/

  Out.pos = mul(In.pos,persp);
  Out.col = In.col;
  Out.uv0 = In.uv0;
  Out.uv1 = In.uv1;
  return Out;
}

