#define bif [branch] if

// Internal combiner commands
#define LOAD		0
#define SUB			1
#define MUL			2
#define ADD			3
#define INTER		4
#define NOOP		5

// Simplified combiner inputs

#define XECOMB_TEX0    0
#define XECOMB_PRIM    1
#define XECOMB_ENV     2
#define XECOMB_COLOR   3

#define XECOMB_ZERO    4
#define XECOMB_ONE     5
#define XECOMB_COMB    6
#define XECOMB_TEX1    7 
#define XECOMB_COMB_A  8
#define XECOMB_TEX0_A  9
#define XECOMB_TEX1_A  10
#define XECOMB_PRIM_A  11
#define XECOMB_ENV_A   12
#define XECOMB_COLOR_A 13
#define XECOMB_LOD     14

#define NOTIMPL_COLOR {1.0,0.0,1.0,1.0}

struct _IN
{
	float2 uv0: TEXCOORD0;
	float2 uv1: TEXCOORD1;
	float4 col : COLOR;
};

// color combiner
float4 cnum: register(c0);
float4 cops[12]: register(c1);
// alpha combiner
float4 anum: register(c16);
float4 aops[12]: register(c17);
// constant colors
float4 prim: register(c32);
float4 env: register(c33);
float4 lod: register(c34);

sampler tex0;
sampler tex1;

float4 main(_IN aData): COLOR {
	float4 res=NOTIMPL_COLOR;
	int i;
    float av;
    float3 cv;
	float4 aop,cop,t0,t1;

    t0=tex2D(tex0,aData.uv0);
    t1=tex2D(tex1,aData.uv1);

    float4 inputValues[] = {
		t0,                            // XECOMB_TEX0
		prim,                          // XECOMB_PRIM
		env,		                   // XECOMB_ENV
		aData.col,                     // XECOMB_COLOR
        {0,0,0,0},                     // XECOMB_ZERO
		{1,1,1,1},                     // XECOMB_ONE
        NOTIMPL_COLOR,                 // XECOMB_COMB
		t1,                            // XECOMB_TEX1
		NOTIMPL_COLOR,                 // XECOMB_COMB_A
        {t0.a,t0.a,t0.a,t0.a},         // XECOMB_TEX0_A
		{t1.a,t1.a,t1.a,t1.a},         // XECOMB_TEX1_A
        {prim.a,prim.a,prim.a,prim.a}, // XECOMB_PRIM_A
        {env.a,env.a,env.a,env.a},	   // XECOMB_ENV_A
        {aData.col.a,aData.col.a,aData.col.a,aData.col.a}, // XECOMB_COLOR_A
        lod,                           // XECOMB_LOD
    };

    [loop]
	for(i=0;i<12;++i){
        aop=aops[i];
        cop=cops[i];

        av=inputValues[aop.w].a;

        bif(aop.x<10)
        {
            res.a=aop.x*av+aop.y*res.a+aop.z*av*res.a;
        }
        else
        {
    		res.a=lerp(inputValues[aop.z].a,inputValues[aop.y].a,av);
        }

        cv=inputValues[cop.w].rgb;

        bif(cop.x<10)
        {
            res.rgb=cop.x*cv+cop.y*res.rgb+cop.z*cv*res.rgb;
        }
        else
        {
    		res.rgb=lerp(inputValues[cop.z].rgb,inputValues[cop.y].rgb,cv);
        }

        if (i==anum.y || i==cnum.y){
            inputValues[XECOMB_COMB]=res;
            inputValues[XECOMB_COMB_A]=float4(res.a,res.a,res.a,res.a);
        }

        if (i>=anum.x) break;

    }

    return res;
}
