.global inc_about_size
.set inc_about_size, 0f-inc_about
.global inc_about
inc_about:
	.incbin "about.png"
0: