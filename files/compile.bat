@echo off

del *.bin

rshadercompiler vs.hlsl vs.bin /vs

rshadercompiler ps_fb.hlsl ps_fb.bin /ps

rshadercompiler ps_combiner.hlsl ps_combiner.bin /ps

echo #define WITH_1C > temp
type ps_combiner.hlsl >> temp
rshadercompiler temp ps_combiner_1c.bin /ps > NUL

echo #define WITH_1A > temp
type ps_combiner.hlsl >> temp
rshadercompiler temp ps_combiner_1a.bin /ps > NUL

echo #define WITH_1C > temp
echo #define WITH_1A >> temp
type ps_combiner.hlsl >> temp
rshadercompiler temp ps_combiner_1c1a.bin /ps > NUL

rshadercompiler ps_combiner_slow.hlsl ps_combiner_slow.bin /ps

del ..\build\shaders.o

rem pause