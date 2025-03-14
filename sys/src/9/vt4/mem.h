/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */
#define TiB		1099511627776ull	/* Tebi 0x0000010000000000 */
#define PiB		1125899906842624ull	/* Pebi 0x0004000000000000 */
#define EiB		1152921504606846976ull	/* Exbi 0x1000000000000000 */

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define BI2WD		32			/* bits per word */
#define BY2WD		4			/* bytes per word */
#define BY2V		8			/* bytes per vlong */
#define BY2SE		4			/* bytes per stack element */
#define BY2PG		4096			/* bytes per page */
#define PGSHIFT		12			/* log(BY2PG) */
#define STACKALIGN(sp)	((sp) & ~7)		/* bug: assure with alloc */

#define BY2PTE		8			/* bytes per pte entry */
#define BY2PTEG		64			/* bytes per pte group */

#define ICACHESIZE	16384			/* 0, 4, 8, 16, or 32 KB */
#define ICACHEWAYSIZE	(ICACHESIZE/2)		/* 2-way set associative */
#define ICACHELINELOG	5			/* 8 words (4 bytes) per line */
#define ICACHELINESZ	(1<<ICACHELINELOG)

#define DCACHESIZE	16384			/* 0, 4, 8, 16, or 32 KB */
#define DCACHEWAYSIZE	(DCACHESIZE/2)		/* 2-way set associative */
#define DCACHELINELOG	5			/* 8 words (4 bytes) per line */
#define DCACHELINESZ	(1<<DCACHELINELOG)

#define BLOCKALIGN	DCACHELINESZ		/* for ../port/allocb.c */

#define MAXMACH		2			/* max # cpus system can run */
#define MACHSIZE	BY2PG

/*
 * Time
 */
#define HZ		100			/* clock frequency */
#define MHz		1000000
#define TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

/*
 * IBM bit field order
 * used only to derive bit mask for interrupt vector numbers
 */
#define IBIT(n)	(1UL<<(31-(n)))

/*
 * Bit encodings for Machine State Register (MSR)
 */
#define MSR_AP	0x02000000	/* auxiliary processor available */
#define MSR_APE	0x00080000	/* APU exception enable */
#define MSR_WE	0x00040000	/* wait state enable */
#define MSR_CE	0x00020000	/* critical interrupt enable */
#define MSR_EE	0x00008000	/* enable external/decrementer interrupts */
#define MSR_PR	0x00004000	/* =1, user mode */
#define MSR_FP	0x00002000	/* floating-point available */
#define MSR_ME	0x00001000	/* enable machine check exceptions */
#define MSR_FE0	0x00000800	/* floating-point exception mode 0 */
#define MSR_DWE	0x00000400	/* debug wait enable */
#define MSR_DE	0x00000200	/* debug interrupts enable */
#define MSR_FE1	0x00000100	/* floating-point exception mode 1 */
#define MSR_IR	0x00000020	/* enable instruction address translation */
#define MSR_DR	0x00000010	/* enable data address translation */

/* state in user mode */
#define UMSR	(MSR_PR|MSR_DE|MSR_CE|MSR_EE|MSR_IR|MSR_DR)

/*
 * Exception Syndrome Register (ESR)
 */
#define ESR_MCI	0x80000000	/* instruction machine check */
#define ESR_PIL	0x08000000	/* program interrupt: illegal instruction */
#define ESR_PPR	0x04000000	/* program interrupt: privileged */
#define ESR_PTR	0x02000000	/* program interrupt: trap with successful compare */
#define ESR_PEU	0x01000000	/* program interrupt: unimplemented APU/FPU operation */
#define ESR_DST	0x00800000	/* data storage interrupt: store fault */
#define ESR_DIZ	0x00400000	/* data/instruction storage interrupt: zone fault */
#define ESR_PFP	0x00080000	/* program interrupt: FPU interrupt occurred */
#define ESR_PAP	0x00040000	/* program interrupt: APU interrupt occurred */
#define ESR_U0F	0x00008000	/* data storage interrupt: u0 fault */

/*
 * Interrupt vector offsets
 */
#define INT_RESET	0x0100		/* Critical input interrupt */
#define INT_MCHECK	0x0200		/* Machine check */
#define INT_DSI		0x0300		/* Data storage interrupt */
#define INT_ISI		0x0400		/* Instruction storage interrupt */
#define INT_EI		0x0500		/* External interrupt */
#define INT_ALIGN	0x0600		/* Alignment */
#define INT_PROG	0x0700		/* Program */
#define INT_FPU		0x0800		/* FPU unavailable */
#define INT_DEC		0x0900		/* UNUSED on 405? */
#define INT_SYSCALL	0x0C00		/* System call */
#define INT_TRACE	0x0D00		/* UNUSED on 405? */
#define INT_FPA		0x0E00		/* UNUSED on 405? */
#define INT_APU		0x0F20		/* APU unavailable */
#define INT_PIT		0x1000		/* PIT interrupt */
#define INT_FIT		0x1010		/* FIT interrupt */
#define INT_WDT		0x1020		/* Watchdog timer */
#define INT_DMISS	0x1100		/* Data TLB miss */
#define INT_IMISS	0x1200		/* Instruction TLB miss */
#define INT_DEBUG	0x2000		/* Debug */

/*
 * Magic registers
 */
#define MACH		30		/* R30 is m-> */
#define USER		29		/* R29 is up-> */

/*
 * Virtual MMU
 */
#define PTEMAPMEM	(1024*1024)
#define PTEPERTAB	(PTEMAPMEM/BY2PG)
#define SEGMAPSIZE	1984
#define SSEGMAPSIZE	16
#define PPN(x)		((x)&~(BY2PG-1))

#define PTEVALID	(1<<0)
#define PTEWRITE	(1<<1)
#define PTERONLY	(0<<1)
#define PTEUNCACHED	(1<<2)

/*
 * Physical MMU
 */
#define NTLB		64	/* number of entries */
#define NTLBPID		256	/* number of hardware pids (0 = global) */

/* TLBHI */
#define TLBEPN(x)	((x) & ~0x3FF)
#define TLB1K		(0<<7)
#define TLB4K		(1<<7)
#define TLB16K		(2<<7)
#define TLB64K		(3<<7)
#define TLB256K		(4<<7)
#define TLB1MB		(5<<7)
#define TLB4MB		(6<<7)
#define TLB16MB		(7<<7)
#define TLBVALID	(1<<6)
#define TLBLE		(1<<5)	/* little-endian */
#define TLBU0		(1<<4)	/* user-defined attribute */

/* TLBLO */
#define TLBRPN(x)	((x) & ~0x3FF)
#define TLBEX		(1<<9)	/* execute enable */
#define TLBWR		(1<<8)	/* write enable */
#define TLBZONE(x)	((x)<<4)
#define TLBW		(1<<3)	/* write-through */
#define TLBI		(1<<2)	/* cache inhibit */
#define TLBM		(1<<1)	/* memory coherent */
/*
 * WARNING: applying TLBG to instruction pages will cause ISI traps
 * on Xilinx 405s, despite the words of the Xilinx manual (p. 155).
 */
#define TLBG		(1<<0)	/* guarded */

/*
 * software TLB (for quick reload by [id]tlbmiss)
 */
#define STLBLOG		10
#define STLBSIZE	(1<<STLBLOG)

/*
 * Address spaces
 */
#define KSEG0		0x80000000
#define KSEG1		0xA0000000		/* uncached alias for KZERO */
#define KSEGM		0xE0000000		/* mask to check segment */
#define KZERO		KSEG0			/* base of kernel address space */
#define KTZERO		(KZERO+0x100000)	/* first address in kernel text */

#define TSTKTOP		KZERO			/* top of temporary stack */
#define	TSTKSIZ 	256	/* pages in new stack; limits exec args */

#define UZERO		0			/* base of user address space */
#define UTZERO		(UZERO+0x100000)	/* first address in user text */
#define UTROUND(t)	ROUNDUP((t), 0x100000)
#define USTKTOP		(TSTKTOP-TSTKSIZ*BY2PG)	/* byte just beyond user stack */

#define	CONFADDR	(KZERO+0x2200)		/* info passed from boot loader */
#define	REBOOTADDR (CONFADDR+BOOTLINELEN+BOOTARGSLEN) /* reboot code - physical address */

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * On the power pc, we can use 0x2000+ε through 0x4000.
 */
#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	1024

#define KSTACK		8192  /* Size of kernel stack (not proc 0's in Mach) */

#define OCMZERO	0x40000000			/* on-chip memory (virtual and physical--see 405EP p 5-1) */
#define USTKSIZE	(4*1024*1024)		/* size of user stack */
#define UREGSIZE	((8+40)*4)

#include "physmem.h"

#define getpgcolor(a)	0		/* ../port/page.c */
