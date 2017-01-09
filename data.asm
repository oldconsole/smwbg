.include "hdr.asm"

.section ".rodata" superfree
tiles1:
.incbin "smw.dat"
.ends

.section ".rodata1" superfree
bg1map:
.incbin "smw.map"
palette:
.incbin "smw.clr"
.ends

