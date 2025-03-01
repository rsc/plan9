/*
 * armv7/armv8-a long-descriptor memory management
 *
 * Three levels of page tables:
 * L0 table: 4 entries, each mapping 1GiB virtual space, as either
 *   a contiguous block, or
 *   an L1 page: 512 entries, each mapping 2MiB, as either
 *     a contiguous block, or
 *     an L2 page: 512 entries, each mapping 4KiB
 *
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "arm.h"

typedef uvlong LPTE;

#define L0X(va)		FEXT((va), 30, 2)
#define L1X(va)		FEXT((va), 21, 11)
#define L2X(va)		FEXT((va), 12, 9)

enum {
	L1lo		= L1X(UZERO),
	L1hi		= L1X(USTKTOP+2*MiB-1),
	L2size		= BY2PG,
	KMAPADDR	= 0xFFE00000,

	LXN			= 1LL<<54,
	LPXN		= 1LL<<53,
	LAF			= 1<<10,
	LShareOuter	= 2<<8,
	LShareInner	= 3<<8,
	LKrw		= 0<<6,
	LUrw		= 1<<6,
	LUro		= 3<<6,
	LMem		= 0<<2,
	LUncached	= 1<<2,
	LDevice		= 2<<2,
	LBlock		= 1<<0,
	LTable		= 3<<0,
	LPage		= 3<<0,

	MMem		= 0xFF,	/* MEM_WB */
	MUncached	= 0x44,	/* MEM_UC */
	MDevice		= 0x04,	/* DEV_nGnRE*/

	EAE_LPAE	= 1<<31,
	SH0_INNER	= 3<<12,
	ORGN0_WB	= 1<<10,
	IRGN0_WB	= 1<<8,
 
	LAttrDRAM	= LKrw | LAF | LShareInner | LMem,
	LAttrPhys	= LPXN | LAF | LShareOuter | LUncached,
	LAttrIO		= LKrw | LAF | LXN | LPXN | LShareOuter | LDevice,
	LAttrUser	= LPXN | LAF | LShareInner | LMem,
};

#define XPPN(pa)	((pa)&~(BY2PG-1) | (uvlong)((pa)&PTEHIMEM) << (32-8))
#define LPPN(pte)	((pte)&0xFFFFFF000ULL)

/*
 * TTBCR and MAIR0 register settings for LPAE (used in armv7.s)
 */
ulong mair0 = MMem<<0 | MUncached<<8 | MDevice<<16;
ulong ttbcr = EAElpae | SH0inner | ORGN0wb | IRGN0wb;

/*
 * Set up initial PTEs
 *   Called before main, with mmu off, to initialise cpu0's tables
 *   Called from launchinit to clone cpu0's tables for other cpus
 */
void
mmuinit(void *a)
{
	LPTE *l0, *l1, *l2;
	uintptr pa, pe, va;
	int i;

	l1 = (LPTE*)a;
	if((uintptr)l1 != PADDR(L1))
		memmove(l1, m->mmul1, L1SIZE);

	/*
	 * embed L0 table near end of L1 table
	 */
	l0 = (LPTE*)((uintptr)a + L1SIZE - 64);
	pa = PADDR(l1);
	for(i = 0; i < 4; i++){
		l0[i] = pa|LAttrDRAM|LTable;
		pa += BY2PG;
	}

	if((uintptr)l1 != PADDR(L1))
		return;

	/*
	 * map ram at KZERO, bounded above by VIRTPCI
	 */
	//l0[KZERO/GiB] = PHYSDRAM|LAttrDRAM|LBlock;
	//va = KZERO + GiB;
	va = KZERO;
	pe = VIRTPCI - KZERO;
	if(pe > soc.dramsize)
		pe = soc.dramsize;
	for(pa = PHYSDRAM; pa < PHYSDRAM+pe; pa += 2*MiB){
		l1[L1X(va)] = pa|LAttrDRAM|LBlock;
		va += 2*MiB;
	}

	/*
	 * identity map first 2MB of ram so mmu can be enabled
	 */
	l1[L1X(PHYSDRAM)] = PHYSDRAM|LAttrDRAM|LBlock;
	/*
	 * map i/o registers 
	 */
	va = VIRTIO;
	for(pa = soc.physio; pa < soc.physio+IOSIZE; pa += 2*MiB){
		l1[L1X(va)] = pa|LAttrIO|LBlock;
		va += 2*MiB;
	}
	pa = soc.armlocal;
	if(pa)
		l1[L1X(va)] = pa|LAttrIO|LBlock;
	/*
	 * pi4 hack: ether and pcie are in segment 0xFD5xxxxx not 0xFE5xxxxx
	 *           gisb is in segment 0xFC4xxxxx not FE4xxxxx (and we map it at va FE6xxxxx)
	 */
	va = VIRTIO + 0x400000;
	pa = soc.physio - 0x1000000 + 0x400000;
	l1[L1X(va)] = pa|LAttrIO|LBlock;
	va = VIRTIO + 0x600000;
	pa = soc.physio - 0x2000000 + 0x400000;
	l1[L1X(va)] = pa|LAttrIO|LBlock;
	
	/*
	 * double map exception vectors near top of virtual memory
	 * not ready for malloc, so alias first L1 page (which has only one entry)
	 * as temporary L2 page
	 */
	l2 = (LPTE*)PADDR(L1);
	//memset(l2, 0, BY2PG);
	va = HVECTORS;
	l2[L2X(va)] = PHYSDRAM|LAttrDRAM|LPage;
	l1[L1X(va)] = (uintptr)l2|LAttrDRAM|LTable;
}

void
mmuinit1()
{
	LPTE *l1, *l2;
	uintptr va;

	l1 = m->mmull1;

	/*
	 * first L1 page: undo identity map and L2 alias
	 */
	memset(l1, 0, BY2PG);
	cachedwbtlb(l1, BY2PG);

	/*
	 * make a local mapping for highest 2MB of virtual space
	 * containing kmap area and exception vectors
	 */
	va = HVECTORS;
	m->kmapll2 = l2 = mallocalign(L2size, L2size, 0, 0);
	l2[L2X(va)] = PHYSDRAM|LAttrDRAM|LPage;
	l1[L1X(va)] = PADDR(l2)|LAttrDRAM|LTable;
	cachedwbtlb(&l1[L1X(va)], sizeof(LPTE));

	mmuinvalidate();
}

static int
nonzero(uint *p)
{
	int i;
	for(i = 0; i < BY2PG/sizeof(uint); i++)
		if(*p++ != 0)
			return 1;
	return 0;
}

static void
mmul2empty(Proc* proc, int clear)
{
	LPTE *l1;
	Page **l2, *page;

	l1 = m->mmull1;
	l2 = &proc->mmul2;
	for(page = *l2; page != nil; page = page->next){
		if(clear){
			if(l1[page->daddr] == Fault){
				l1[page->daddr] = XPPN(page->pa)|LAttrDRAM|LTable;
				coherence();
			}
			memset((void*)(0xFF000000 + L2size*page->daddr), 0, L2size);
		}
		l1[page->daddr] = Fault;
		l2 = &page->next;
	}
	coherence();
	*l2 = proc->mmul2cache;
	proc->mmul2cache = proc->mmul2;
	proc->mmul2 = nil;
}

static void
mmul1empty(void)
{
	LPTE *l1;

	/* clean out any user mappings still in l1 */
	if(m->mmul1lo > 0){
		if(m->mmul1lo == 1)
			m->mmull1[L1lo] = Fault;
		else
			memset(&m->mmull1[L1lo], 0, m->mmul1lo*sizeof(LPTE));
		m->mmul1lo = 0;
	}
	if(m->mmul1hi > 0){
		l1 = &m->mmull1[L1hi - m->mmul1hi];
		if(m->mmul1hi == 1)
			*l1 = Fault;
		else
			memset(l1, 0, m->mmul1hi*sizeof(LPTE));
		m->mmul1hi = 0;
	}
	if(m->kmapll2 != nil)
		memset(m->kmapll2, 0, NKMAPS*sizeof(LPTE));
}

void
mmuswitch(Proc* proc)
{
	int x;
	LPTE *l1;
	Page *page;

	if(proc != nil && proc->newtlb){
		mmul2empty(proc, 1);
		proc->newtlb = 0;
	}

	mmul1empty();

	/* move in new map */
	l1 = m->mmull1;
	if(proc != nil){
	  for(page = proc->mmul2; page != nil; page = page->next){
		x = page->daddr;
		l1[x] = XPPN(page->pa)|LAttrDRAM|LTable;
		if(x >= L1lo + m->mmul1lo && x < L1hi - m->mmul1hi){
			if(x+1 - L1lo < L1hi - x)
				m->mmul1lo = x+1 - L1lo;
			else
				m->mmul1hi = L1hi - x;
		}
	  }
	  if(proc->nkmap)
		memmove(m->kmapll2, proc->kmapltab, sizeof(proc->kmapltab));
	}

	/* make sure map is in memory */
	/* could be smarter about how much? */
	cachedwbtlb(&l1[L1X(UZERO)], (L1hi - L1lo)*sizeof(LPTE));
	if(proc != nil && proc->nkmap)
		cachedwbtlb(m->kmapll2, sizeof(proc->kmapltab));

	/* lose any possible stale tlb entries */
	mmuinvalidate();
}

void
flushmmu(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

void
mmurelease(Proc* proc)
{
	Page *page, *next;

	mmul2empty(proc, 0);
	for(page = proc->mmul2cache; page != nil; page = next){
		next = page->next;
		if(--page->ref)
			panic("mmurelease: page->ref %d", page->ref);
		pagechainhead(page);
	}
	if(proc->mmul2cache && palloc.r.p)
		wakeup(&palloc.r);
	proc->mmul2cache = nil;

	mmul1empty();

	/* make sure map is in memory */
	/* could be smarter about how much? */
	cachedwbtlb(&m->mmull1[L1X(UZERO)], (L1hi - L1lo)*sizeof(LPTE));

	/* lose any possible stale tlb entries */
	mmuinvalidate();
}

void
putmmu(uintptr va, uintptr pa, Page* page)
{
	int x, s;
	Page *pg;
	LPTE *l1, *pte;
	LPTE nx;
	static int chat = 0;

	/*
	 * disable interrupts to prevent flushmmu (called from hzclock)
	 * from clearing page tables while we are setting them
	 */
	s = splhi();
	x = L1X(va);
	l1 = &m->mmull1[x];
	pte = (LPTE*)(0xFF000000 + L2size*x);
	if(*l1 == Fault){
		/* l2 pages have 512 entries */
		if(up->mmul2cache == nil){
			spllo();
			pg = newpage(1, 0, 0);
			splhi();
			/* if newpage slept, we might be on a different cpu */
			l1 = &m->mmull1[x];
		}else{
			pg = up->mmul2cache;
			up->mmul2cache = pg->next;
		}
		pg->daddr = x;
		pg->next = up->mmul2;
		up->mmul2 = pg;

		*l1 = XPPN(pg->pa)|LAttrDRAM|LTable;
		cachedwbtlb(l1, sizeof *l1);

		/* force l2 page to memory */
		if(nonzero((uint*)pte))
		if(chat++ < 32){
			iprint("nonzero L2 page from cache pa %lux va %lux daddr %lux\n",
				pg->pa, pg->va, pg->daddr);
			memset(pte, 0, L2size);
		}
		cachedwbtlb(pte, L2size);

		if(x >= L1lo + m->mmul1lo && x < L1hi - m->mmul1hi){
			if(x+1 - L1lo < L1hi - x)
				m->mmul1lo = x+1 - L1lo;
			else
				m->mmul1hi = L1hi - x;
		}
	}

	/* protection bits are
	 *	PTERONLY|PTEVALID;
	 *	PTEWRITE|PTEVALID;
	 *	PTEWRITE|PTEUNCACHED|PTEVALID;
	 */
	nx = LAttrUser|LPage;
	if(pa & PTEUNCACHED)
		nx = LAttrPhys|LPage;
	if(pa & PTEWRITE)
		nx |= LUrw;
	else
		nx |= LUro;
	pte[L2X(va)] = XPPN(pa)|nx;
	if(0)if(chat++ < 32)
		iprint("putmmu %#p %lux => %llux\n", va, pa, pte[L2X(va)]);
	cachedwbtlb(&pte[L2X(va)], sizeof(LPTE));

	/* clear out the current entry */
	mmuinvalidateaddr(va);

	if(page->cachectl[m->machno] == PG_TXTFLUSH){
		/* pio() sets PG_TXTFLUSH whenever a text pg has been written */
		if(cankaddr(page->pa))
			cachedwbse((void*)(page->pa|KZERO), BY2PG);
		cacheiinvse((void*)page->va, BY2PG);
		page->cachectl[m->machno] = PG_NOFLUSH;
	}
	checkmmu(va, pa);
	splx(s);
}

/*
 * Return the number of bytes that can be accessed via KADDR(pa).
 * If pa is not a valid argument to KADDR, return 0.
 */
uintptr
cankaddr(uintptr pa)
{
	if(pa == 0)
		return 0;
	if((pa - PHYSDRAM) < VIRTPCI-KZERO)
		return PHYSDRAM + VIRTPCI-KZERO - pa;
	return 0;
}

uintptr
mmukmapx(uintptr va, uvlong pa, usize size)
{
	int o;
	usize n;
	LPTE *pte, *pte0;

	print("mmukmapx %#p => %llux (%lux)\n", va, pa, size);
	assert((va & (2*MiB-1)) == 0);
	o = pa & (2*MiB-1);
	pa -= o;
	size += o;
	pte = pte0 = &m->mmull1[L1X(va)];
	for(n = 0; n < size; n += 2*MiB)
		if(*pte++ != Fault)
			return 0;
	pte = pte0;
	for(n = 0; n < size; n += 2*MiB){
		*pte++ = (pa+n)|LAttrIO|LBlock;
		mmuinvalidateaddr(va+n);
	}
	cachedwbtlb(pte0, (uintptr)pte - (uintptr)pte0);
	return va + o;
}

uintptr
mmukmap(uintptr va, uintptr pa, usize size)
{
	return mmukmapx(va, pa, size);
}

void
checkmmu(uintptr va, uintptr pa)
{
	int x;
	LPTE *l1, *pte;

	x = L1X(va);
	l1 = &m->mmull1[x];
	if((*l1&LTable) != LTable){
		iprint("checkmmu cpu%d va=%lux l1 %p=%llux\n", m->machno, va, l1, *l1);
		return;
	}
	pte = (LPTE*)(0xFF000000 + L2size*x);
	pte += L2X(va);
	if(pa == ~0 || (pa != 0 && LPPN(*pte) != XPPN(pa))){
		iprint("checkmmu va=%lux pa=%lux l1 %p=%llux pte %p", va, pa, l1, *l1, pte);
		iprint("=%llux\n", *pte);
	}
}

KMap*
kmap(Page *p)
{
	int s, i;
	uintptr va;
	uvlong pa;

	pa = XPPN(p->pa);
	if(pa < VIRTPCI-KZERO)
		return KADDR(pa);
	if(up == nil)
		panic("kmap without up %#p", getcallerpc(&pa));
	s = splhi();
	if(up->nkmap == NKMAPS)
		panic("kmap overflow %#p", getcallerpc(&pa));
	for(i = 0; i < NKMAPS; i++)
		if(up->kmapltab[i] == 0)
			break;
	if(i == NKMAPS)
		panic("can't happen");
	up->nkmap++;
	va = KMAPADDR + i*BY2PG;
	up->kmapltab[i] = pa|LAttrDRAM|LPage;
	m->kmapll2[i] = up->kmapltab[i];
	cachedwbtlb(&m->kmapll2[i], sizeof(LPTE));
	mmuinvalidateaddr(va);
	splx(s);
	return (KMap*)va;
}

void
kunmap(KMap *k)
{
	int i;
	uintptr va;

	coherence();
	va = (uintptr)k;
	if(L1X(va) != L1X(KMAPADDR))
		return;
	/* wasteful: only needed for text pages aliased within data cache */
	cachedwbse((void*)va, BY2PG);
	i = L2X(va);
	up->kmapltab[i] = 0;
	m->kmapll2[i] = 0;
	up->nkmap--;
	cachedwbtlb(&m->kmapll2[i], sizeof(LPTE));
	mmuinvalidateaddr(va);
}

/*
 * Append pages with physical addresses above 4GiB to the freelist
 */
void
lpapageinit(void)
{
	int j;
	ulong npage;
	Page *pages, *p;
	uvlong pa;

	npage = conf.himem.npage;
	if(npage == 0)
		return;
	
	pages = xalloc(npage*sizeof(Page));
	if(pages == 0)
		panic("lpapageinit");

	p = pages;
	pa = conf.himem.base;
	for(j=0; j<npage; j++){
		p->prev = p-1;
		p->next = p+1;
		p->pa = pa;
		p->pa |= (pa >> (32-8)) & PTEHIMEM;
		p++;
		pa += BY2PG;
	}
	palloc.tail->next = pages;
	pages[0].prev = palloc.tail;
	palloc.tail = p - 1;
	palloc.tail->next = 0;

	palloc.freecount += npage;
	palloc.user += npage;

	/* Paging numbers */
	swapalloc.highwater = (palloc.user*5)/100;
	swapalloc.headroom = swapalloc.highwater + (swapalloc.highwater/4);

	print("%ldM extended memory: ", npage*(BY2PG/1024)/1024);
	print("%ldM user total\n", palloc.user*(BY2PG/1024)/1024);
}
