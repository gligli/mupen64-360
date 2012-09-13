#include "../main/winlnxdefs.h"
#include "../main/main.h"
#include <stdlib.h>
#include <string.h>
#include "fakegl.h"
#include "Combiner.h"
#include "Debug.h"
#include "xenos_combiner.h"
#include "xenos_gfx.h"

#include <xenos/xe.h>
extern struct XenosDevice *xe;

static int xeComb_input[2][21] = {
    {
        XECOMB_COMB, // COMBINED		
        XECOMB_TEX0, // TEXEL0			
        XECOMB_TEX1, // TEXEL1			
	    XECOMB_PRIM, // PRIMITIVE		
        XECOMB_COLOR, // SHADE			
	    XECOMB_ENV, // ENVIRONMENT		
        XECOMB_ZERO, // CENTER			
        XECOMB_ZERO, // SCALE			
        XECOMB_COMB_A, // COMBINED_ALPHA	
        XECOMB_TEX0_A, // TEXEL0_ALPHA	
        XECOMB_TEX1_A, // TEXEL1_ALPHA	
	    XECOMB_PRIM_A, // PRIMITIVE_ALPHA	
        XECOMB_COLOR_A, // SHADE_ALPHA		
	    XECOMB_ENV_A, // ENV_ALPHA		
        XECOMB_ZERO, // LOD_FRACTION	
        XECOMB_LOD, // PRIM_LOD_FRAC	
        XECOMB_ZERO, // NOISE			
        XECOMB_ZERO, // K4				
        XECOMB_ZERO, // K5				
        XECOMB_ONE, // ONE				
        XECOMB_ZERO, // ZERO			
    },
    {
        XECOMB_COMB, // COMBINED		
        XECOMB_TEX0, // TEXEL0			
        XECOMB_TEX1, // TEXEL1			
	    XECOMB_PRIM, // PRIMITIVE		
        XECOMB_COLOR, // SHADE			
	    XECOMB_ENV, // ENVIRONMENT		
        XECOMB_ZERO, // CENTER			
        XECOMB_ZERO, // SCALE			
        XECOMB_COMB, // COMBINED_ALPHA	
        XECOMB_TEX0, // TEXEL0_ALPHA	
        XECOMB_TEX1, // TEXEL1_ALPHA	
	    XECOMB_PRIM, // PRIMITIVE_ALPHA	
        XECOMB_COLOR, // SHADE_ALPHA		
	    XECOMB_ENV, // ENV_ALPHA		
        XECOMB_ZERO, // LOD_FRACTION	
	    XECOMB_LOD, // PRIM_LOD_FRAC	
        XECOMB_ZERO, // NOISE			
        XECOMB_ZERO, // K4				
        XECOMB_ZERO, // K5				
        XECOMB_ONE, // ONE				
        XECOMB_ZERO, // ZERO			
    }
};


static int xeComb_fastParam[21] ={
    XECOMB_F_COMB, // COMBINED		
    XECOMB_F_T0, // TEXEL0			
    XECOMB_F_T1, // TEXEL1			
    XECOMB_F_CST, // PRIMITIVE		
    XECOMB_F_COL, // SHADE			
    XECOMB_F_CST, // ENVIRONMENT		
    XECOMB_F_CST, // CENTER			
    XECOMB_F_CST, // SCALE			
    XECOMB_F_COMB, // COMBINED_ALPHA	
    XECOMB_F_T0,  // TEXEL0_ALPHA	
    XECOMB_F_T1,  // TEXEL1_ALPHA	
    XECOMB_F_CST, // PRIMITIVE_ALPHA	
    XECOMB_F_COL, // SHADE_ALPHA		
    XECOMB_F_CST, // ENV_ALPHA		
    XECOMB_F_CST, // LOD_FRACTION	
    XECOMB_F_CST, // PRIM_LOD_FRAC	
    XECOMB_F_CST, // NOISE			
    XECOMB_F_CST, // K4				
    XECOMB_F_CST, // K5				
    XECOMB_F_CST, // ONE				
    XECOMB_F_CST, // ZERO			
};

static bool xeComb_fastAlpha[21] ={
    false, // COMBINED		
    false, // TEXEL0			
    false, // TEXEL1			
    false, // PRIMITIVE		
    false, // SHADE			
    false, // ENVIRONMENT		
    false, // CENTER			
    false, // SCALE			
    true, // COMBINED_ALPHA	
    true, // TEXEL0_ALPHA	
    true, // TEXEL1_ALPHA	
    true, // PRIMITIVE_ALPHA	
    true, // SHADE_ALPHA		
    true, // ENV_ALPHA		
    false, // LOD_FRACTION	
    false, // PRIM_LOD_FRAC	
    false, // NOISE			
    false, // K4				
    false, // K5				
    false, // ONE				
    false, // ZERO			
};


static const char * combiner_opNames[]={
    "LOAD",
    "SUB",	
    "MUL",	
    "ADD",
    "INTER",
    "NOOP",
};

static const char * combiner_paramNames[]={
    "COMBINED",
    "TEXEL0",
    "TEXEL1",
    "PRIMITIVE",
    "SHADE",
    "ENVIRONMENT",
    "CENTER",
    "SCALE",
    "COMBINED_ALPHA",
    "TEXEL0_ALPHA",
    "TEXEL1_ALPHA",
    "PRIMITIVE_ALPHA",
    "SHADE_ALPHA",
    "ENV_ALPHA",
    "LOD_FRACTION",
    "PRIM_LOD_FRAC",
    "NOISE",
    "K4",
    "K5",
    "ONE",
    "ZERO",
};

void combinerDump(const char *name, Combiner * c){
	int i, j;
	printf("-- %s:\n", name);

	for (i=0; i<c->numStages; ++i)
	{
        printf("stage %d:\n", i);
        for (j=0; j<c->stage[i].numOps; ++j){
            printf("  %s",combiner_opNames[c->stage[i].op[j].op]);
            
            switch(c->stage[i].op[j].op){
                case LOAD:                
                case SUB:                
                case MUL:                
                case ADD:                
                    printf("(%s)\n",combiner_paramNames[c->stage[i].op[j].param1]);
                    break;
                case INTER:
                    printf("(%s,%s,%s)\n",combiner_paramNames[c->stage[i].op[j].param1],combiner_paramNames[c->stage[i].op[j].param2],combiner_paramNames[c->stage[i].op[j].param3]);
                    break;
            }
        }
	}
}

int simplifiedInput(int idx, int in, TxeCombiner * xc){
    int res=XECOMB_ZERO;

    if (in>=0 && in<(int)(sizeof(xeComb_input)/sizeof(int)))
        res=xeComb_input[idx][in];
    
    xc->usesT0|=(in==TEXEL0);
    xc->usesT0|=(in==TEXEL0_ALPHA);
    xc->usesT1|=(in==TEXEL1);
    xc->usesT1|=(in==TEXEL1_ALPHA);

    xc->usesSlow|=(in==COMBINED);
    xc->usesSlow|=(in==COMBINED_ALPHA);

    return res;
}



void combinerCompile(int idx, Combiner * c, TxeCombiner * xc){
    float t[XECOMB_COMBINER_SIZE][4];
    int i,j;
    int numOps;
    static int numColOps=0;

    numOps=0;

    for (i=0; i<c->numStages; ++i){
        for (j=0; j<c->stage[i].numOps; ++j){
            ++numOps;

            switch(c->stage[i].op[j].op){
                case LOAD:
                    t[numOps][0]=1;
                    t[numOps][1]=0;
                    t[numOps][2]=0;
                    t[numOps][3]=simplifiedInput(idx,c->stage[i].op[j].param1,xc);
                    break;
                case MUL:
                    t[numOps][0]=0;
                    t[numOps][1]=0;
                    t[numOps][2]=1;
                    t[numOps][3]=simplifiedInput(idx,c->stage[i].op[j].param1,xc);
                    break;
                case ADD:
                    t[numOps][0]=1;
                    t[numOps][1]=1;
                    t[numOps][2]=0;
                    t[numOps][3]=simplifiedInput(idx,c->stage[i].op[j].param1,xc);
                    break;
                case SUB:
                    t[numOps][0]=-1;
                    t[numOps][1]=1;
                    t[numOps][2]=0;
                    t[numOps][3]=simplifiedInput(idx,c->stage[i].op[j].param1,xc);
                    break;
                case INTER:
			        t[numOps][0]=42;
                    t[numOps][1]=simplifiedInput(idx,c->stage[i].op[j].param1,xc);
                    t[numOps][2]=simplifiedInput(idx,c->stage[i].op[j].param2,xc);
                    t[numOps][3]=simplifiedInput(idx,c->stage[i].op[j].param3,xc);
                    break;
            }
        }
	}

#ifdef DEBUG_COMBINERS
	printf("numOps %d %d\n",numOps,numColOps);
#endif
	
    xc->usesSlow|=numOps>4;

    for(i=numOps+1;i<12;++i){
		t[i][0]=0;
		t[i][1]=1;
		t[i][2]=0;
		t[i][3]=XECOMB_ZERO;
	}

	if (idx==XECOMB_COLOR_IDX)
        numColOps=numOps;
    else{
        numOps=MAX(numColOps,numOps);
		xc->numOps=numOps;
	}


    t[0][0]=numOps-1;
    t[0][1]=(c->numStages>1)?c->stage[0].numOps-1:-1;
    t[0][2]=0;
    t[0][3]=0;

	memcpy(xc->floats[idx],t,sizeof(t));

    if (xc->usesSlow){
		xc->op[idx]=0;
		return;
	}

    bool flags[128];
    int op=0,tmp;
    float signs[4];

    memset(flags,0,128);

    for (i=0; i<c->numStages; ++i){
        for (j=0; j<c->stage[i].numOps; ++j){
            
            tmp=xeComb_fastParam[c->stage[i].op[j].param1];

            flags[op*XECOMB_OP_SIZE+ 4]=tmp==XECOMB_F_T0 || tmp==XECOMB_F_T1;
            flags[op*XECOMB_OP_SIZE+ 5]=tmp==XECOMB_F_T0 || tmp==XECOMB_F_COL;
            //flags[op*OP_SIZE+ 6]=
            flags[op*XECOMB_OP_SIZE+ 7]=xeComb_fastAlpha[c->stage[i].op[j].param1];
            
            tmp=c->stage[i].op[j].op;

            signs[op]=(tmp==SUB)?-1.0f:1.0f;

            flags[op*XECOMB_OP_SIZE+ 0]=true;
            flags[op*XECOMB_OP_SIZE+ 1]=tmp==LOAD || tmp==ADD || tmp==SUB;
            flags[op*XECOMB_OP_SIZE+ 2]=tmp==LOAD || tmp==MUL;
            //flags[op*OP_SIZE+ 3]=

            if (tmp==INTER){
                tmp=xeComb_fastParam[c->stage[i].op[j].param2];

                flags[op*XECOMB_OP_SIZE+ 8]=tmp==XECOMB_F_T0 || tmp==XECOMB_F_T1;
                flags[op*XECOMB_OP_SIZE+ 9]=tmp==XECOMB_F_T0 || tmp==XECOMB_F_COL;
                //flags[op*OP_SIZE+10]=
                flags[op*XECOMB_OP_SIZE+11]=xeComb_fastAlpha[c->stage[i].op[j].param2];
            
                tmp=xeComb_fastParam[c->stage[i].op[j].param3];

                flags[op*XECOMB_OP_SIZE+12]=tmp==XECOMB_F_T0 || tmp==XECOMB_F_T1;
                flags[op*XECOMB_OP_SIZE+13]=tmp==XECOMB_F_T0 || tmp==XECOMB_F_COL;
                //flags[op*OP_SIZE+14]=
                flags[op*XECOMB_OP_SIZE+15]=xeComb_fastAlpha[c->stage[i].op[j].param3];
            }

            op++;
        }
    }

	memcpy(xc->signs[idx],signs,sizeof(signs));
	memcpy(xc->flags[idx],flags,sizeof(flags)/2);
	
    xc->op[idx]=op;
 }

#define PS_FLAG(val,bit,en) {if(en) val|=1<<(bit);}

int combinerUpload(int idx, TxeCombiner * xc, u32 &shidx){
    int start=idx*XECOMB_COMBINER_SIZE;
	int i;

	if (xc->usesSlow) xeGfx_setCombinerConstantF(start,xc->floats[idx][0],xc->numOps+1);
   
	if (!xc->op[idx]) return 0;
	
	xeGfx_setCombinerConstantF(128+16*idx,xc->signs[2],1);
    for(i=0;i<64;++i) xeGfx_setCombinerConstantB(i+idx*64,xc->flags[idx][i]);
	
#if 1
    if (idx==XECOMB_COLOR_IDX)
    {
        shidx|=(xc->op[idx]-1) & 3;
        PS_FLAG(shidx,4,xc->flags[idx][0*4*4+2]);
        PS_FLAG(shidx,5,xc->flags[idx][1*4*4+2]);
        PS_FLAG(shidx,6,xc->flags[idx][2*4*4+2]);
        PS_FLAG(shidx,7,xc->flags[idx][3*4*4+2]);
    }
    else
    {
        shidx|=((xc->op[idx]-1)&3)<<2;
        PS_FLAG(shidx,8,xc->flags[idx][0*4*4+2]);
        PS_FLAG(shidx,9,xc->flags[idx][1*4*4+2]);
        PS_FLAG(shidx,10,xc->flags[idx][2*4*4+2]);
        PS_FLAG(shidx,11,xc->flags[idx][3*4*4+2]);
    }
#else
    if (idx==XECOMB_COLOR_IDX)
    {
        shidx|=(xc->op[idx]-1) & 3;
        PS_FLAG(shidx,4,xc->flags[idx][0*4*4+1]);
        PS_FLAG(shidx,5,xc->flags[idx][0*4*4+2]);
        PS_FLAG(shidx,6,xc->flags[idx][1*4*4+1]);
        PS_FLAG(shidx,7,xc->flags[idx][1*4*4+2]);
        PS_FLAG(shidx,8,xc->flags[idx][2*4*4+2]);
        PS_FLAG(shidx,9,xc->flags[idx][3*4*4+2]);
    }
    else
    {
        shidx|=((xc->op[idx]-1)&3)<<2;
        PS_FLAG(shidx,10,xc->flags[idx][0*4*4+1]);
        PS_FLAG(shidx,11,xc->flags[idx][0*4*4+2]);
        PS_FLAG(shidx,12,xc->flags[idx][1*4*4+1]);
        PS_FLAG(shidx,13,xc->flags[idx][1*4*4+2]);
        PS_FLAG(shidx,14,xc->flags[idx][2*4*4+2]);
        PS_FLAG(shidx,15,xc->flags[idx][3*4*4+2]);
    }
#endif  
    
	return xc->op[idx];
}

void combinerUploadColorConstants(int idx, Combiner * c){
    int i,j,op=0;
    float cols[20][4];
    memset(cols,0,sizeof(cols));

    cols[PRIMITIVE][0]=gDP.primColor.r;
    cols[PRIMITIVE][1]=gDP.primColor.g;
    cols[PRIMITIVE][2]=gDP.primColor.b;
    cols[PRIMITIVE][3]=gDP.primColor.a;

    cols[ENVIRONMENT][0]=gDP.envColor.r;
    cols[ENVIRONMENT][1]=gDP.envColor.g;
    cols[ENVIRONMENT][2]=gDP.envColor.b;
    cols[ENVIRONMENT][3]=gDP.envColor.a;

    cols[PRIMITIVE_ALPHA][0]=gDP.primColor.a;
    cols[PRIMITIVE_ALPHA][1]=gDP.primColor.a;
    cols[PRIMITIVE_ALPHA][2]=gDP.primColor.a;
    cols[PRIMITIVE_ALPHA][3]=gDP.primColor.a;

    cols[ENV_ALPHA][0]=gDP.envColor.a;
    cols[ENV_ALPHA][1]=gDP.envColor.a;
    cols[ENV_ALPHA][2]=gDP.envColor.a;
    cols[ENV_ALPHA][3]=gDP.envColor.a;

    cols[PRIM_LOD_FRAC][0]=gDP.primColor.l;
    cols[PRIM_LOD_FRAC][1]=gDP.primColor.l;
    cols[PRIM_LOD_FRAC][2]=gDP.primColor.l;
    cols[PRIM_LOD_FRAC][3]=gDP.primColor.l;

    cols[ONE][0]=1.0f;
    cols[ONE][1]=1.0f;
    cols[ONE][2]=1.0f;
    cols[ONE][3]=1.0f;

    for (i=0; i<c->numStages; ++i){
        for (j=0; j<c->stage[i].numOps; ++j){

            if (op<4) {
                xeGfx_setCombinerConstantF(129+16*idx+op*3,cols[c->stage[i].op[j].param1],1);
                if (c->stage[i].op[j].op==INTER){
                    xeGfx_setCombinerConstantF(129+16*idx+op*3+1,cols[c->stage[i].op[j].param2],1);
                    xeGfx_setCombinerConstantF(129+16*idx+op*3+2,cols[c->stage[i].op[j].param3],1);
                }
            }

            op++;
        }
    }
}



void xeComb_init(){

}

void xeComb_uninit(){

}

TxeCombiner *xeComb_compile( Combiner *color, Combiner *alpha ){
	TxeCombiner *xeComb = (TxeCombiner*)malloc( sizeof( TxeCombiner ) );
	memset(xeComb,0,sizeof( TxeCombiner ));
	
    xeComb->color=*color;
    xeComb->alpha=*alpha;
	
#ifdef DEBUG_COMBINERS
	combinerDump("color",color);
#endif
	combinerCompile(XECOMB_COLOR_IDX,color,xeComb);

#ifdef DEBUG_COMBINERS
    combinerDump("alpha",alpha);
#endif
	combinerCompile(XECOMB_ALPHA_IDX,alpha,xeComb);
	
    return xeComb;
}

void xeComb_setCombiner( TxeCombiner *envCombiner ){
	u32 shidx=0;
	
	combiner.usesNoise=FALSE;
    combiner.usesT0=envCombiner->usesT0;
    combiner.usesT1=envCombiner->usesT1;

    combinerUpload(XECOMB_COLOR_IDX,envCombiner,shidx);
    combinerUpload(XECOMB_ALPHA_IDX,envCombiner,shidx);

    xeGfx_setCombinerConstantB(0,combiner.usesT0);
    xeGfx_setCombinerConstantB(64,combiner.usesT1);

	xeGfx_setCombinerShader(shidx,envCombiner->usesSlow);

//    combinerDump("color",&envCombiner->color);
//    combinerDump("alpha",&envCombiner->alpha);
}

void xeComb_updateColors( TxeCombiner* c ){
    float t[3][4];

	t[0][0]=gDP.primColor.r;
    t[0][1]=gDP.primColor.g;
    t[0][2]=gDP.primColor.b;
    t[0][3]=gDP.primColor.a;

	t[1][0]=gDP.envColor.r;
    t[1][1]=gDP.envColor.g;
    t[1][2]=gDP.envColor.b;
    t[1][3]=gDP.envColor.a;
    
    t[2][0]=gDP.primColor.l;
    t[2][1]=gDP.primColor.l;
    t[2][2]=gDP.primColor.l;
    t[2][3]=gDP.primColor.l;

    xeGfx_setCombinerConstantF(XECOMB_COMBINER_SIZE*2,t[0],3);

	combinerUploadColorConstants(XECOMB_COLOR_IDX,&c->color);
	combinerUploadColorConstants(XECOMB_ALPHA_IDX,&c->alpha);

/*	printf("prim %f %f %f %f\n",gDP.primColor.r,gDP.primColor.g,gDP.primColor.b,gDP.primColor.a);
	printf("env %f %f %f %f\n",gDP.envColor.r,gDP.envColor.g,gDP.envColor.b,gDP.envColor.a);*/
}

void xeComb_beginTextureUpdate(){

}

void xeComb_endTextureUpdate(){

}
