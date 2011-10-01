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
float4x4 ortho: register (c4);

bool transform: register(b0);
bool isortho: register(b1);

_OUT main(_IN In )
{
  _OUT Out;

  if(transform){
	if(isortho)
	  Out.pos = mul(In.pos,ortho);
	else
	  Out.pos = mul(In.pos,persp);
  }else{
	  Out.pos = In.pos;
  }

  Out.col = In.col;
  Out.uv0 = In.uv0;
  Out.uv1 = In.uv1;
  return Out;
}

