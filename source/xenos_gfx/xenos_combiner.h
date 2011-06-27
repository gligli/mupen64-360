#ifndef XENOS_COMBINER_H
#define XENOS_COMBINER_H

#define XECOMB_COLOR_IDX 0
#define XECOMB_ALPHA_IDX 1

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

#define XECOMB_F_CST  0
#define XECOMB_F_T0   1
#define XECOMB_F_T1   2
#define XECOMB_F_COL  3
#define XECOMB_F_COMB 4

#define XECOMB_COMBINER_SIZE 16
#define XECOMB_OP_SIZE 16


struct TxeCombiner
{
    Combiner color,alpha;    
	float floats[2][XECOMB_COMBINER_SIZE][4];
	int numOps[2];
	
    bool flags[2][64];
	float signs[2][4];
	int op[2];
	
	BOOL usesT0,usesT1,usesSlow;
};

void xeComb_init();
void xeComb_uninit();
TxeCombiner *xeComb_compile( Combiner *color, Combiner *alpha );
void xeComb_setCombiner( TxeCombiner *envCombiner );
void xeComb_updateColors( TxeCombiner* );
void xeComb_beginTextureUpdate();
void xeComb_endTextureUpdate();
#endif