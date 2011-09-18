@echo off

del *.bin

rshadercompiler vs.hlsl vs.bin /vs

rshadercompiler ps_fb.hlsl ps_fb.bin /ps

rshadercompiler ps_combiner.hlsl ps_combiner.bin /ps

echo #define NUM_COL_OPS 1 > temp
echo #define NUM_ALPHA_OPS 1 >> temp
type ps_combiner.hlsl >> temp
rshadercompiler temp ps_combiner_1c1a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 1 > temp                  
echo #define NUM_ALPHA_OPS 2 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_1c2a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 1 > temp                  
echo #define NUM_ALPHA_OPS 3 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_1c3a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 1 > temp                  
echo #define NUM_ALPHA_OPS 4 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_1c4a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 2 > temp                  
echo #define NUM_ALPHA_OPS 1 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_2c1a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 2 > temp                  
echo #define NUM_ALPHA_OPS 2 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_2c2a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 2 > temp                  
echo #define NUM_ALPHA_OPS 3 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_2c3a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 2 > temp                  
echo #define NUM_ALPHA_OPS 4 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_2c4a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 3 > temp                  
echo #define NUM_ALPHA_OPS 1 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_3c1a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 3 > temp                  
echo #define NUM_ALPHA_OPS 2 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_3c2a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 3 > temp                  
echo #define NUM_ALPHA_OPS 3 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_3c3a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 3 > temp                  
echo #define NUM_ALPHA_OPS 4 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_3c4a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 4 > temp                  
echo #define NUM_ALPHA_OPS 1 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_4c1a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 4 > temp                  
echo #define NUM_ALPHA_OPS 2 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_4c2a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 4 > temp                  
echo #define NUM_ALPHA_OPS 3 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_4c3a.bin /ps > NUL
                                                   
echo #define NUM_COL_OPS 4 > temp                  
echo #define NUM_ALPHA_OPS 4 >> temp               
type ps_combiner.hlsl >> temp                      
rshadercompiler temp ps_combiner_4c4a.bin /ps > NUL

rshadercompiler ps_combiner_slow.hlsl ps_combiner_slow.bin /ps

del ..\build\shaders.o

rem pause