#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define MAPTILE_INDEX_MAKE(a) (((a).value.bits.indexHigh << 8) | ((a).value.bits.indexLow))

void settiles(unsigned int b, unsigned char *p1, unsigned int size);
void setmap(unsigned int b, unsigned char *p1);
void setpalette(unsigned char *pal);
void enablescreen();
void screenmode(unsigned char m);
void setsprite(unsigned int s, unsigned char x, unsigned char y, unsigned char t, unsigned char p);
void waitforvsync();
void delay(unsigned int d);
void resettimer();
void sync(unsigned int d);

/* init */
void snesc_init(void);

/* input */
#define TR_BUTTON	0x0010
#define TL_BUTTON	0x0020
#define Y_BUTTON	0x0040
#define X_BUTTON	0x0080
#define RIGHT_BUTTON	0x0100
#define LEFT_BUTTON	0x0200
#define DOWN_BUTTON	0x0400
#define UP_BUTTON	0x0800
#define START_BUTTON	0x1000
#define SELECT_BUTTON	0x2000
#define B_BUTTON	0x4000
#define A_BUTTON	0x8000
unsigned int getjoystatus(unsigned int j);
void clearjoy(unsigned int j);

/* string */
void writenum(unsigned long long num, unsigned char len, unsigned int *map, unsigned int p, unsigned int offset);
void writestring(const char *st, unsigned int *map, unsigned int p, unsigned int offset);

extern unsigned char snesc_timer_enabled;
extern unsigned int snesc_timers[16];
extern unsigned int snesc_controllers[4];
extern unsigned char snesc_do_copy;

struct oam_table1 {
  unsigned char x;
  unsigned char y;
  unsigned char t;
  unsigned char p;  
};

extern struct oam_table1 snesc_oam_table1[128];
extern unsigned int snesc_oam_table2[16];
extern unsigned char snesc_palette[512];

struct dma_transfer {
  union {
    struct {
      unsigned short addr; /* 0 + 1 */
      unsigned char bank; /* 2 */
      unsigned char type;
    } c;
    unsigned char *ptr;
  } src;
  unsigned short dest; /* 3 + 4 */
  unsigned short size; /* 5 + 6 */
};

extern struct dma_transfer snesc_dma_transfers[64];


extern void* __nmi_handler;

unsigned char snesc_do_copy; /* bit 7: do sprite copy, bit 6: do palette copy
                                bits 0-5: DMA transfer count */
unsigned char snesc_timer_enabled; /* each bit one timer */
unsigned int snesc_timers[16];

struct oam_table1 snesc_oam_table1[128];	/* OAM low table */
unsigned int snesc_oam_table2[16];	/* OAM high table */

unsigned char snesc_palette[512];	/* palette */

unsigned int snesc_controllers[4];	/* data from 4 controllers */

struct dma_transfer snesc_dma_transfers[64]; /* DMA transfers
                                            0 (src_addr, src_bank) src address (24 bit)
                                            3 (dest) dest address (16 bit)
                                            5 (size) size (16 bit)
                                            7 (type) type (8 bit, 0 == VRAM, 1 == OAM, 2 == CGRAM, >2 == ???) */

void do_dma(unsigned char do_flags)
{
  if (do_flags & 0x80) {	/* copy sprites? */
    *((unsigned short*)0x4300) = 0x400;	/* DMA channel 0 Bus B addr $2104 (OAM write) */
    /* the original code loads $8000, turning on sprite priority and
       messing up the gfx (some emulators don't emulate this
       correctly, which is probably why it has slipped through) */
    *(unsigned short*)0x2102 = 0;	/* OAM address 0 */
    *((void**)0x4302) = snesc_oam_table1; /* DMA source address (24 bits); hairy: writes 32 bits... */
    *(unsigned short*)0x4305 = 0x220;	/* DMA size */
    *(unsigned char*)0x420b = 1;	/* enable DMA 0 */
  }
  if (do_flags & 0x40) {	/* copy palette? */
    *(unsigned short*)0x4300 = 0x2200;	/* DMA channel 0 Bus B addr $2122 (CGRAM write) */
    *((void**)0x4302) = snesc_palette;	/* DMA source address; see above */
    *(unsigned short*)0x4305 = 0x200;	/* DMA size */
    *(unsigned char*)0x2121 = 0;	/* CGRAM address 0 */
    *(unsigned char*)0x420b = 1;	/* enable DMA 0 */
  }
}

void snesc_vblank(void)
{
  /* stuff pending for DMA? */
  unsigned char do_flags = snesc_do_copy;
  if(do_flags) {
    snesc_do_copy = 0;
    do_dma(do_flags);
    do_flags &= 0x3f;	/* mask out palette and sprite flags (used by do_dma) */
    if (do_flags) {
      int x = 0;
      while (do_flags) {
        switch(snesc_dma_transfers[x].src.c.type) {	/* type of transfer */
        case 0:	/* VRAM */
          *((unsigned short*)0x4300) = 0x1801;	/* 2 regs write once, Bus B addr $2118 (VRAM data write) */
          *((unsigned short*)0x2116) = snesc_dma_transfers[x].dest;	/* VRAM address */
          break;
        case 1:	/* OAM */
          *((unsigned short*)0x4300) = 0x400;	/* DMA OAM write */
          *((unsigned short*)0x2102) = snesc_dma_transfers[x].dest;	/* OAM address */
          break;
        case 2:	/* CGRAM */
          *((unsigned short*)0x4300) = 0x2200;	/* CGRAM write */
          *((unsigned char*)0x2122) = snesc_dma_transfers[x].dest;	/* CGRAM address (trunc'd to 8 bits) */
          break;
        }
        *((unsigned short*)0x4302) = snesc_dma_transfers[x].src.c.addr;	/* DMA source address (16-bit) */
        *((unsigned short*)0x4305) = snesc_dma_transfers[x].size;	/* DMA size */
        *((unsigned char*)0x4304) = snesc_dma_transfers[x].src.c.bank;	/* DMA source address (bank) */
        *((unsigned char*)0x420b) = 1;	/* enable DMA 0 */
        x++;
        do_flags--;
      }
    }
  }

  while(*((unsigned char*)0x4212) & 1) {}
  
  /* update input buffers */
  unsigned int pad;
  for(pad = 0; pad < 4 ; pad++) {
    snesc_controllers[pad] |= ((unsigned short*)0x4218)[pad];
  }
  
  /* timer ticks */
  unsigned char timers_enabled = snesc_timer_enabled;
  unsigned int tc = 0;
  while(timers_enabled & 1) {
    snesc_timers[tc]++;
    tc++;
    timers_enabled >>= 1;
  }
}

void snesc_init(void)
{
  int i;
  __nmi_handler = snesc_vblank;	/* register vblank handler */
  *((unsigned char*)0x4200) = 0x81; /* enable NMI, enable autojoy */
  snesc_timer_enabled = snesc_do_copy = snesc_controllers[0] = 0;

  /* snesc sprite init stuff */
  for(i = 0; i < 128; i++) {
    snesc_oam_table1[i].x = 0;
    snesc_oam_table1[i].y = 0xe8;
    snesc_oam_table1[i].t = 0;
    snesc_oam_table1[i].p = 0;
  }
  for(i = 0; i < 32; i+=2) {
    snesc_oam_table2[i] = 0;
  }
}

void snesc_vblank(void);

void enablescreen(void)
{
  *(unsigned char*)0x2105 = 1; /* BG mode 1 */
  *(unsigned char*)0x2107 = 0; //BG1 map location
  *(unsigned char*)0x2108 = 4; //BG2 map location
  *(unsigned char*)0x2109 = 8; //BG3 map location
  *(unsigned char*)0x210a = 0; //BG4 map location
  *(unsigned char*)0x210b = 0x11; /* BG1/2 tiles location */
  *(unsigned char*)0x210c = 2; /* BG3/4 tiles location */
  *(unsigned char*)0x212c = 0x17; /* enable BG1, BG2, OBJ display */
  *(unsigned char*)0x2100 = 0x80; /* force blank */
  *(unsigned char*)0x211a = 0; /*clear mode 7 settings */
  snesc_vblank();	/* flush pending DMA jobs before turning on screen */
  *(unsigned char*)0x2100 = 0xf; /* turn on screen, full brightness */
}

void screenmode(unsigned char m)
{
	*(unsigned char*)0x2105 = m;
}

void waitforvsync(void)
{
	snesc_timer_enabled |= 1;
	snesc_timers[0] = 0;
	while(!snesc_timers[0]) {}
}

void delay(unsigned int d)
{
	snesc_timer_enabled |= 1;
	snesc_timers[0] = 0;
	while (snesc_timers[0] < d) {}
}

void setpalette(unsigned char *pal)
{
	memcpy(snesc_palette, pal, 0x200);
	snesc_do_copy |= 0x40;
}

void setsprite(unsigned int s, unsigned char x, unsigned char y, unsigned char t, unsigned char p)
{
	struct oam_table1* spr = &snesc_oam_table1[s];
	spr->x = x;
	spr->y = y;
	spr->t = t;
	spr->p = p | 16 | 32;
	snesc_do_copy |= 0x80;
}

void sync(unsigned int d)
{
	while (snesc_timers[0] < d) {}
}

void resettimer()
{
	snesc_timer_enabled |= 1;
	snesc_timers[0] = 0;
}

void settiles(unsigned int b, unsigned char *p1, unsigned int size)
{
	unsigned int idx = snesc_do_copy & 0x3f;
	struct dma_transfer *tr = &snesc_dma_transfers[idx];
	unsigned int idx2;
	struct dma_transfer *tr2;
	/* tile data */
	tr->src.ptr = p1;
	tr->src.c.type = 0;	/* src.ptr and type overlap, so type must be set after */
	tr->dest = (b + 1) << 12;
	tr->size =  size;
	/* signal the NMI to copy data to VRAM */
	snesc_do_copy++;
}


void setmap(unsigned int b, unsigned char *p1)
{
	struct dma_transfer *tr = &snesc_dma_transfers[snesc_do_copy & 0x3f];
	/* tile data */
	tr->src.ptr = p1;
	tr->src.c.type = 0;
	tr->dest = b << 10;
	tr->size = 0x800;

	/* signal the NMI to copy data to VRAM */
	snesc_do_copy++;
}

#define OUTLIT(x) (*((char*)0xfffe) = (x))
void write(char* s) {
	while(*s) OUTLIT(*s++);
}

void writestring(const char *st, unsigned int *map, unsigned int p, unsigned int offset)
{
	unsigned int sp = p; /* start position */
	unsigned int c;
	while ((c = *st)) {
		if (c == '\n') {
			sp += 0x20;
			p = sp;
		}
		else {
			map[p] = c + offset;
			p++;
		}
		st++;
	}
}

void writenum(unsigned long long num, unsigned char len, unsigned int *map, unsigned int p, unsigned int offset)
{
	unsigned char figure;
	p += len - 1;
	if (!num) {
		map[p] = offset;
		return;
	}
	while (len && num) {
		figure = num % 10;
		if (num || figure)
			map[p] = figure + offset;
		num /= 10;
		p--;
		len--;
	}
}

unsigned int getjoystatus(unsigned int j)
{
	return snesc_controllers[j];
}

void clearjoy(unsigned int j)
{
	snesc_controllers[j] = 0;
}

extern unsigned char tiles1[];
extern unsigned char bg1map[];
extern unsigned char palette[];

#define TM(c, p) (((c) & 0x3ff) | (((p + 1) & 7) << 10))

int main() {
	snesc_init();
	setmap(0, (unsigned char*)bg1map);
	settiles(0, tiles1, 28672);
	setpalette((unsigned char*)palette);
	enablescreen();
	delay(50); 
	while (getjoystatus(0) == 0)
		continue;
	while(1) {
		resettimer();
		delay(1);
		sync(1);
	}
}