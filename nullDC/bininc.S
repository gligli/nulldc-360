.global inc_vs_size
.set inc_vs_size, 0f-inc_vs
.global inc_vs
inc_vs:
	.incbin "vs.bin"
0:

.align 4

.global inc_ps_size
.set inc_ps_size, 0f-inc_ps
.global inc_ps
inc_ps:
	.incbin "ps.bin"
0:
