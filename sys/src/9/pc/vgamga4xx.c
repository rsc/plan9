
/*
 * Matrox G200, G400 and G450.
 * Written by Philippe Anel <xigh@free.fr>
 *
 *  2006-08-07 : Minor fix to allow the G200 cards to work fine. YDSTORG is now initialized.
 *             : Also support for 16 and 24 bit modes is added.
 *             : by Leonardo Valencia <leoval@anixcorp.com>
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	MATROX			= 0x102B,
	MGA550			= 0x2527,
	MGA4xx			= 0x0525,
	MGA200			= 0x0521,

	FCOL			= 0x1c24,
	FXRIGHT			= 0x1cac,
	FXLEFT			= 0x1ca8,
	YDST			= 0x1c90,
	YLEN			= 0x1c5c,
 	DWGCTL			= 0x1c00,
 		DWG_TRAP		= 0x04,
 		DWG_BITBLT		= 0x08,
 		DWG_ILOAD		= 0x09,
 		DWG_LINEAR		= 0x0080,
 		DWG_SOLID		= 0x0800,
 		DWG_ARZERO		= 0x1000,
 		DWG_SGNZERO		= 0x2000,
 		DWG_SHIFTZERO	= 0x4000,
		DWG_REPLACE		= 0x000C0000,
 		DWG_BFCOL		= 0x04000000,
 	SRCORG			= 0x2cb4,
	PITCH			= 0x1c8c,
	DSTORG			= 0x2cb8,
	YDSTORG			= 0x1c94,
	PLNWRT			= 0x1c1c,
	ZORG			= 0x1c0c,
	MACCESS			= 0x1c04,
	STATUS			= 0x1e14,
	FXBNDRY			= 0x1C84,
 	CXBNDRY			= 0x1C80,
	YTOP			= 0x1C98,
	YBOT			= 0x1C9C,
	YDSTLEN			= 0x1C88,
	AR0				= 0x1C60,
	AR1				= 0x1C64,
	AR2				= 0x1C68,
	AR3				= 0x1C6C,
	AR4				= 0x1C70,
	AR5				= 0x1C74,
	SGN				= 0x1C58,
		SGN_LEFT		= 1,
		SGN_UP			= 4,

	GO				= 0x0100,
 	FIFOSTATUS		= 0x1E10,
	CACHEFLUSH		= 0x1FFF,

	CRTCEXTIDX		= 0x1FDE,		/* CRTC Extension Index */
	CRTCEXTDATA		= 0x1FDF,		/* CRTC Extension Data */

	FILL_OPERAND	= 0x800c7804,
};

static Pcidev *
mgapcimatch(void)
{
	Pcidev *p;

	p = pcimatch(nil, MATROX, MGA4xx);
	if(p == nil)
		p = pcimatch(nil, MATROX, MGA550);
	if(p == nil)
		p = pcimatch(nil, MATROX, MGA200);
	return p;
}


static void
mgawrite8(VGAscr *scr, int index, uchar val)
{
	((uchar*)scr->mmio)[index] = val;
}

static uchar
mgaread8(VGAscr *scr, int index)
{
	return ((uchar*)scr->mmio)[index];
}

static uchar
crtcextset(VGAscr *scr, int index, uchar set, uchar clr)
{
	uchar tmp;

	mgawrite8(scr, CRTCEXTIDX, index);
	tmp = mgaread8(scr, CRTCEXTDATA);
	mgawrite8(scr, CRTCEXTIDX, index);
	mgawrite8(scr, CRTCEXTDATA, (tmp & ~clr) | set);

	return tmp;
}

static void
mga4xxenable(VGAscr* scr)
{
	Pcidev *pci;
	int size;
	int i, n, k;
	uchar *p;
	uchar x[16];
	uchar crtcext3;

	if(scr->mmio)
		return;

	pci = mgapcimatch();
	if(pci == nil)
		return;

	scr->mmio = vmap(pci->mem[1].bar&~0x0F, 16*1024);
	if(scr->mmio == nil)
		return;

	addvgaseg("mga4xxmmio", pci->mem[1].bar&~0x0F, pci->mem[1].size);

	/* need to map frame buffer here too, so vga can find memory size */
	if(pci->did == MGA4xx || pci->did == MGA550)
		size = 32*MB;
	else
		size = 8*MB;
	vgalinearaddr(scr, pci->mem[0].bar&~0x0F, size);

	if(scr->paddr){

		/* Find out how much memory is here, some multiple of 2 MB */

		/* First Set MGA Mode ... */
		crtcext3 = crtcextset(scr, 3, 0x80, 0x00);

		p = scr->vaddr;
		n = (size / MB) / 2;
		for(i = 0; i < n; i++){
			k = (2*i+1)*MB;
			p[k] = 0;
			p[k] = i+1;
			*((uchar*)scr->mmio + CACHEFLUSH) = 0;
			x[i] = p[k];
 		}
		for(i = 1; i < n; i++)
			if(x[i] != i+1)
				break;
		scr->apsize = 2*i*MB;	/* sketchy */
		addvgaseg("mga4xxscreen", scr->paddr, scr->apsize);
		crtcextset(scr, 3, crtcext3, 0xff);
	}
}

enum{
	Index		= 0x00,		/* Index */
	Data			= 0x0A,		/* Data */

	Cxlsb		= 0x0C,		/* Cursor X LSB */
	Cxmsb		= 0x0D,		/* Cursor X MSB */
	Cylsb		= 0x0E,		/* Cursor Y LSB */
	Cymsb		= 0x0F,		/* Cursor Y MSB */

	Icuradrl		= 0x04,		/* Cursor Base Address Low */
	Icuradrh		= 0x05,		/* Cursor Base Address High */
	Icctl			= 0x06,		/* Indirect Cursor Control */
};

static void
dac4xxdisable(VGAscr *scr)
{
	uchar *dac4xx;

	if(scr->mmio == 0)
		return;

	dac4xx = (uchar*)scr->mmio+0x3C00;

	*(dac4xx+Index) = Icctl;
	*(dac4xx+Data) = 0x00;
}

static void
dac4xxload(VGAscr *scr, Cursor *curs)
{
	int y;
	uchar *p;
	uchar *dac4xx;

	if(scr->mmio == 0)
		return;

	dac4xx = (uchar*)scr->mmio+0x3C00;

	dac4xxdisable(scr);

	p = (uchar*)scr->storage;
	for(y = 0; y < 64; y++){
		*p++ = 0; *p++ = 0; *p++ = 0;
		*p++ = 0; *p++ = 0; *p++ = 0;
		if(y < 16){
			*p++ = curs->set[1+y*2];
			*p++ = curs->set[y*2];
		} else{
			*p++ = 0; *p++ = 0;
		}

		*p++ = 0; *p++ = 0; *p++ = 0;
		*p++ = 0; *p++ = 0; *p++ = 0;
		if(y < 16){
			*p++ = curs->set[1+y*2]|curs->clr[1+2*y];
			*p++ = curs->set[y*2]|curs->clr[2*y];
		} else{
			*p++ = 0; *p++ = 0;
		}
	}
	scr->offset.x = 64 + curs->offset.x;
	scr->offset.y = 64 + curs->offset.y;

	*(dac4xx+Index) = Icctl;
	*(dac4xx+Data) = 0x03;
}

static int
dac4xxmove(VGAscr *scr, Point p)
{
	int x, y;
	uchar *dac4xx;

	if(scr->mmio == 0)
		return 1;

	dac4xx = (uchar*)scr->mmio + 0x3C00;

	x = p.x + scr->offset.x;
	y = p.y + scr->offset.y;

	*(dac4xx+Cxlsb) = x & 0xFF;
	*(dac4xx+Cxmsb) = (x>>8) & 0x0F;

	*(dac4xx+Cylsb) = y & 0xFF;
	*(dac4xx+Cymsb) = (y>>8) & 0x0F;

	return 0;
}

static void
dac4xxenable(VGAscr *scr)
{
	uchar *dac4xx;
	ulong storage;

	if(scr->mmio == 0)
		return;
	dac4xx = (uchar*)scr->mmio+0x3C00;

	dac4xxdisable(scr);

	storage = (scr->apsize-4096)&~0x3ff;

	*(dac4xx+Index) = Icuradrl;
	*(dac4xx+Data) = 0xff & (storage >> 10);
	*(dac4xx+Index) = Icuradrh;
	*(dac4xx+Data) = 0xff & (storage >> 18);

	scr->storage = (ulong)scr->vaddr + storage;

	/* Show X11-Like Cursor */
	*(dac4xx+Index) = Icctl;
	*(dac4xx+Data) = 0x03;

	/* Cursor Color 0 : White */
	*(dac4xx+Index) = 0x08;
	*(dac4xx+Data)  = 0xff;
	*(dac4xx+Index) = 0x09;
	*(dac4xx+Data)  = 0xff;
	*(dac4xx+Index) = 0x0a;
	*(dac4xx+Data)  = 0xff;

	/* Cursor Color 1 : Black */
	*(dac4xx+Index) = 0x0c;
	*(dac4xx+Data)  = 0x00;
	*(dac4xx+Index) = 0x0d;
	*(dac4xx+Data)  = 0x00;
	*(dac4xx+Index) = 0x0e;
	*(dac4xx+Data)  = 0x00;

	/* Cursor Color 2 : Red */
	*(dac4xx+Index) = 0x10;
	*(dac4xx+Data)  = 0xff;
	*(dac4xx+Index) = 0x11;
	*(dac4xx+Data)  = 0x00;
	*(dac4xx+Index) = 0x12;
	*(dac4xx+Data)  = 0x00;

	/*
	 * Load, locate and enable the
	 * 64x64 cursor in X11 mode.
	 */
	dac4xxload(scr, &arrow);
	dac4xxmove(scr, ZP);
}

static void
mga4xxblank(VGAscr *scr, int blank)
{
	char *cp;
	uchar *mga;
	uchar seq1, crtcext1;

	/* blank = 0 -> turn screen on */
	/* blank = 1 -> turn screen off */

	if(scr->mmio == 0)
		return;
	mga = (uchar*)scr->mmio;

	if(blank == 0){
		seq1 = 0x00;
		crtcext1 = 0x00;
	} else {
		seq1 = 0x20;
		crtcext1 = 0x10;			/* Default value ... : standby */
		cp = getconf("*dpms");
		if(cp){
			if(cistrcmp(cp, "standby") == 0)
				crtcext1 = 0x10;
			else if(cistrcmp(cp, "suspend") == 0)
				crtcext1 = 0x20;
			else if(cistrcmp(cp, "off") == 0)
				crtcext1 = 0x30;
		}
	}

	*(mga + 0x1fc4) = 1;
	seq1 |= *(mga + 0x1fc5) & ~0x20;
	*(mga + 0x1fc5) = seq1;

	*(mga + 0x1fde) = 1;
	crtcext1 |= *(mga + 0x1fdf) & ~0x30;
	*(mga + 0x1fdf) = crtcext1;
}

static void
mgawrite32(uchar *mga, ulong reg, ulong val)
{
	*((ulong*)(&mga[reg])) = val;
}

static ulong
mgaread32(uchar *mga, ulong reg)
{
	return *((ulong*)(&mga[reg]));
}

static void
mga_fifo(uchar *mga, uchar n)
{
	ulong t;

#define Timeout 100
	for (t = 0; t < Timeout; t++)
		if ((mgaread32(mga, FIFOSTATUS) & 0xff) >= n)
			break;
	if (t >= Timeout)
		print("mga4xx: fifo timeout");
}

static int
mga4xxfill(VGAscr *scr, Rectangle r, ulong color)
{
	uchar *mga;

	if(scr->mmio == 0)
		return 0;
	mga = (uchar*)scr->mmio;

	mga_fifo(mga, 7);
	mgawrite32(mga, DWGCTL, 0);
	mgawrite32(mga, FCOL, color);
	mgawrite32(mga, FXLEFT, r.min.x);
	mgawrite32(mga, FXRIGHT, r.max.x);
	mgawrite32(mga, YDST, r.min.y);
	mgawrite32(mga, YLEN, Dy(r));
	mgawrite32(mga, DWGCTL + GO, FILL_OPERAND);

	while(mgaread32(mga, STATUS) & 0x00010000)
		;

	return 1;
}

static int
mga4xxscroll(VGAscr *scr, Rectangle dr, Rectangle sr)
{
	uchar * mga;
	int pitch;
 	int width, height;
	ulong start, end, sgn;
	Point sp, dp;

	if(scr->mmio == 0)
		return 0;
	mga = (uchar*)scr->mmio;

	assert(Dx(sr) == Dx(dr) && Dy(sr) == Dy(dr));

	sp = sr.min;
	dp = dr.min;
	if(eqpt(sp, dp))
		return 1;

	pitch = Dx(scr->gscreen->r);
	width = Dx(sr);
	height = Dy(sr);
	sgn = 0;

	if(dp.y > sp.y && dp.y < sp.y + height){
		sp.y += height - 1;
		dp.y += height - 1;
		sgn |= SGN_UP;
	}

	width--;
	start = end = sp.x + (sp.y * pitch);

	if(dp.x > sp.x && dp.x < sp.x + width){
		start += width;
		sgn |= SGN_LEFT;
	}
	else
		end += width;

	mga_fifo(mga, 8);
	mgawrite32(mga, DWGCTL, 0);
	mgawrite32(mga, SGN, sgn);
	mgawrite32(mga, AR5, sgn & SGN_UP ? -pitch : pitch);
	mgawrite32(mga, AR0, end);
	mgawrite32(mga, AR3, start);
	mgawrite32(mga, FXBNDRY, ((dp.x + width) << 16) | dp.x);
	mgawrite32(mga, YDSTLEN, (dp.y << 16) | height);
	mgawrite32(mga, DWGCTL + GO, DWG_BITBLT | DWG_SHIFTZERO | DWG_BFCOL | DWG_REPLACE);

	while(mgaread32(mga, STATUS) & 0x00010000)
		;

	return 1;
}

static void
mga4xxdrawinit(VGAscr *scr)
{
	uchar *mga;

	if(scr->mmio == 0)
		return;

	mga = (uchar*)scr->mmio;

	mgawrite32(mga, SRCORG, 0);
	mgawrite32(mga, DSTORG, 0);
	mgawrite32(mga, YDSTORG, 0);
	mgawrite32(mga, ZORG, 0);
	mgawrite32(mga, PLNWRT, ~0);
	mgawrite32(mga, FCOL, 0xffff0000);
	mgawrite32(mga, CXBNDRY, 0xFFFF0000);
	mgawrite32(mga, YTOP, 0);
	mgawrite32(mga, YBOT, 0x01FFFFFF);
	mgawrite32(mga, PITCH, Dx(scr->gscreen->r) & ((1 << 13) - 1));
	switch(scr->gscreen->depth){
	case 8:
		mgawrite32(mga, MACCESS, 0);
		break;
	case 16:
		mgawrite32(mga, MACCESS, 1);
		break;
	case 24:
		mgawrite32(mga, MACCESS, 3);
		break;
	case 32:
		mgawrite32(mga, MACCESS, 2);
		break;
	default:
		return;		/* depth not supported ! */
	}
	scr->fill = mga4xxfill;
	scr->scroll = mga4xxscroll;
	scr->blank = mga4xxblank;
}

VGAdev vgamga4xxdev = {
	"mga4xx",
	mga4xxenable,		/* enable */
	0,					/* disable */
	0,					/* page */
	0,					/* linear */
	mga4xxdrawinit,
};

VGAcur vgamga4xxcur = {
	"mga4xxhwgc",
	dac4xxenable,
	dac4xxdisable,
	dac4xxload,
	dac4xxmove,
};
