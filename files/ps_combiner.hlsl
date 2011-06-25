#define bif [branch] if

struct _IN
{
	float2 uv0: TEXCOORD0;
	float2 uv1: TEXCOORD1;
	float4 col : COLOR;
};

float4 csgns: register (c128);
float4 asgns: register (c144);

float4 ccsts[4][3]: register (c129);
float4 acsts[4][3]: register (c145);

bool cops[4][4][4]:register (b0);
bool aops[4][4][4]:register (b64);

sampler tex0: register (s0);
sampler tex1: register (s1);

#define COLOR_OPERATION(op) {                                                  \
    bif (cops[op][1][0]){                                                      \
        bif (cops[op][1][1])                                                   \
            v0=t0;                                                             \
        else                                                                   \
            v0=t1;                                                             \
    }else{                                                                     \
        bif (cops[op][1][1])                                                   \
            v0=aData.col;                                                      \
        else                                                                   \
            v0=ccsts[op][0];                                                   \
    }                                                                          \
    bif (cops[op][1][3]) v0=float4(v0.a,v0.a,v0.a,v0.a);                       \
                                                                               \
    bif (cops[op][0][1]){                                                      \
        bif (cops[op][0][2])                                                   \
            res.rgb=v0.rgb;                                                    \
        else                                                                   \
            res.rgb=res.rgb+csgns[op]*v0.rgb;                                  \
    }else{                                                                     \
        bif (cops[op][0][2])                                                   \
            res.rgb=res.rgb*v0.rgb;                                            \
        else{                                                                  \
                                                                               \
            bif (cops[op][2][0]){                                              \
                bif (cops[op][2][1])                                           \
                    v1=t0;                                                     \
                else                                                           \
                    v1=t1;                                                     \
            }else{                                                             \
                bif (cops[op][2][1])                                           \
                    v1=aData.col;                                              \
                else                                                           \
                    v1=ccsts[op][1];                                           \
            }                                                                  \
            bif (cops[op][2][3]) v1=float4(v1.a,v1.a,v1.a,v1.a);               \
                                                                               \
            bif (cops[op][3][0]){                                              \
                bif (cops[op][3][1])                                           \
                    v2=t0;                                                     \
                else                                                           \
                    v2=t1;                                                     \
            }else{                                                             \
                bif (cops[op][3][1])                                           \
                    v2=aData.col;                                              \
                else                                                           \
                    v2=ccsts[op][2];                                           \
            }                                                                  \
            bif (cops[op][3][3]) v2=float4(v2.a,v2.a,v2.a,v2.a);               \
                                                                               \
            res.rgb=lerp(v1.rgb,v0.rgb,v2.rgb);                                \
        }                                                                      \
    }                                                                          \
}                                                                              

#define ALPHA_OPERATION(op) {                                                  \
    bif (aops[op][1][0]){                                                      \
        bif (aops[op][1][1])                                                   \
            v0.a=t0.a;                                                         \
        else                                                                   \
            v0.a=t1.a;                                                         \
    }else{                                                                     \
        bif (aops[op][1][1])                                                   \
            v0.a=aData.col.a;                                                  \
        else                                                                   \
            v0.a=acsts[op][0].a;                                               \
    }                                                                          \
                                                                               \
    bif (aops[op][0][1]){                                                      \
        bif (aops[op][0][2])                                                   \
            res.a=v0.a;                                                        \
        else                                                                   \
            res.a=res.a+asgns[op]*v0.a;                                        \
    }else{                                                                     \
        bif (aops[op][0][2])                                                   \
            res.a=res.a*v0.a;                                                  \
        else{                                                                  \
                                                                               \
            bif (aops[op][2][0]){                                              \
                bif (aops[op][2][1])                                           \
                    v1.a=t0.a;                                                 \
                else                                                           \
                    v1.a=t1.a;                                                 \
            }else{                                                             \
                bif (aops[op][2][1])                                           \
                    v1.a=aData.col.a;                                          \
                else                                                           \
                    v1.a=acsts[op][1].a;                                       \
            }                                                                  \
                                                                               \
            bif (aops[op][3][0]){                                              \
                bif (aops[op][3][1])                                           \
                    v2.a=t0.a;                                                 \
                else                                                           \
                    v2.a=t1.a;                                                 \
            }else{                                                             \
                bif (aops[op][3][1])                                           \
                    v2.a=aData.col.a;                                          \
                else                                                           \
                    v2.a=acsts[op][2].a;                                       \
            }                                                                  \
                                                                               \
            res.a=lerp(v1.a,v0.a,v2.a);                                        \
        }                                                                      \
    }                                                                          \
}                                                                              


float4 main(_IN aData): COLOR {
    float4 v0 = {0,0,0,0}, v1 = {0,0,0,0}, v2 = {0,0,0,0};
    float4 t0 = {0,0,0,0}, t1 = {0,0,0,0};
    float4 res = {1,0,1,1};

    t0=tex2D(tex0,aData.uv0);
    t1=tex2D(tex1,aData.uv1);

    COLOR_OPERATION(0)

    bif (cops[1][0][0]){
        COLOR_OPERATION(1)

#ifndef WITH_1C 
        bif (cops[2][0][0]){
            COLOR_OPERATION(2)

            bif (cops[3][0][0]){
                COLOR_OPERATION(3)
            }
        }
#endif
    }

    ALPHA_OPERATION(0)

    bif (aops[1][0][0]){
        ALPHA_OPERATION(1)

#ifndef WITH_1A
        bif (aops[2][0][0]){
            ALPHA_OPERATION(2)

            bif (aops[3][0][0]){
                ALPHA_OPERATION(3)
            }
        }
#endif
    }

    return res;
}
