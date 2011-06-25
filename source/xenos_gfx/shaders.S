.global inc_vs_size
.set inc_vs_size, 0f-inc_vs
.global inc_vs
inc_vs:
	.incbin "vs.bin"
0:

.align 4

.global inc_ps_fb_size
.set inc_ps_fb_size, 0f-inc_ps_fb
.global inc_ps_fb
inc_ps_fb:
	.incbin "ps_fb.bin"
0:

.align 4

.global inc_ps_combiner_size
.set inc_ps_combiner_size, 0f-inc_ps_combiner
.global inc_ps_combiner
inc_ps_combiner:
	.incbin "ps_combiner.bin"
0:

.align 4

.global inc_ps_combiner_1c_size
.set inc_ps_combiner_1c_size, 0f-inc_ps_combiner_1c
.global inc_ps_combiner_1c
inc_ps_combiner_1c:
	.incbin "ps_combiner_1c.bin"
0:

.align 4

.global inc_ps_combiner_1a_size
.set inc_ps_combiner_1a_size, 0f-inc_ps_combiner_1a
.global inc_ps_combiner_1a
inc_ps_combiner_1a:
	.incbin "ps_combiner_1a.bin"
0:

.align 4

.global inc_ps_combiner_1c1a_size
.set inc_ps_combiner_1c1a_size, 0f-inc_ps_combiner_1c1a
.global inc_ps_combiner_1c1a
inc_ps_combiner_1c1a:
	.incbin "ps_combiner_1c1a.bin"
0:

.align 4

.global inc_ps_combiner_slow_size
.set inc_ps_combiner_slow_size, 0f-inc_ps_combiner_slow
.global inc_ps_combiner_slow
inc_ps_combiner_slow:
	.incbin "ps_combiner_slow.bin"
0: