/*
 * PL011 UART (somewhat like 8250)
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

typedef struct Pinctl Pinctl;

struct Pinctl {
	uchar	txpin;
	uchar	rxpin;
	uchar	alt;
};

static Pinctl pinctl[] = {
	{ 14, 15, Alt0, },		/* rpi pins  8, 10 - shared with uartmini.c */
	{  0,  1, Alt4, },		/* rpi pins 27, 28 */
	{  4,  5, Alt4, },		/* rpi pins  7, 29 */
	{  8,  9, Alt4, },		/* rpi pins 24, 21 */
	{ 12, 13, Alt4, },		/* rpi pins 32, 33 */
};

enum {					/* registers */
	Dr		= 0x00>>2,	/* Data (RW) */
	Fr		= 0x18>>2,	/* Flag */
	Ibrd		= 0x24>>2,	/* Integer Baud Rate Divisor */
	Fbrd		= 0x28>>2,	/* Fractional Baud Rate Divisor */
	Lcrh		= 0x2C>>2,	/* Line Control */
	Cr		= 0x30>>2,	/* Control */
	Ifls		= 0x34>>2,	/* Interrupt FIFO Level Select */
	Imsc		= 0x38>>2,	/* Interrupt Mask Set Clear */
	Mis		= 0x40>>2,	/* Masked Interrupt Status */
	Icr		= 0x44>>2,	/* Interrupt Clear */
	Dmacr		= 0x48>>2,	/* DMA Control */
};

enum {					/* Dr error bits */
	Oe		= 0x800,	/* Overrun */
	Be		= 0x400,	/* Break */
	Pe		= 0x200,	/* Parity */
	Fe		= 0x100,	/* Framing */
};

enum {					/* Fr */
	Txfe		= 0x80,		/* Transmit FIFO Empty */
	Rxff		= 0x40,		/* Receive FIFO Full */
	Txff		= 0x20,		/* Transmit FIFO Full */
	Rxfe		= 0x10,		/* Receive FIFO Empty */
	Busy		= 0x08,		/* Transmitting Data */
	Cts		= 0x01,		/* Clear to Send */
};

enum {					/* Lcrh */
	Stp		= 0x80,		/* Stick Parity */
	WlsMASK		= 0x60,		/* Word Length Select */
	Wls8		= 0x60,		/*	8 bits/byte */
	Wls7		= 0x40,		/*	7 bits/byte */
	Wls6		= 0x20,		/*	6 bits/byte */
	Wls5		= 0x00,		/*	5 bits/byte */
	Fen		= 0x10,		/* FIFO enable */
	Stb		= 0x08,		/* 2 stop bits */
	Eps		= 0x04,		/* Even Parity Select */
	Pen		= 0x02,		/* Parity Enable */
	Brk		= 0x01,		/* Break */
};

enum {					/* Cr */
	Ctsen		= 0x8000,	/* Auto CTS */
	Rtsen		= 0x4000,	/* Auto RTS */
	Rts		= 0x0800,	/* Ready To Send */
	Dtr		= 0x0400,	/* Data Terminal Ready (unsupported) */
	Rxe		= 0x0200,	/* Receive Enable */
	Txe		= 0x0100,	/* Transmit Enable */
	Lbe		= 0x0080,	/* Loopback Enable */
	Uarten		= 0x0001,	/* UART Enable */
};

enum {					/* Ifls */
	RFIFO1		= 0<<3,		/* Rx FIFO trigger level 1/8 full */
	RFIFO2		= 1<<3,		/*	2/8 full */
	RFIFO4		= 2<<3,		/*	4/8 full */
	RFIFO6		= 3<<3,		/*	6/8 full */
	RFIFO7		= 4<<3,		/*	7/8 full */
	TFIFO1		= 0<<0,		/* Tx FIFO trigger level 1/8 full */
	TFIFO2		= 1<<0,		/*	2/8 full */
	TFIFO4		= 2<<0,		/*	4/8 full */
	TFIFO6		= 3<<0,		/*	6/8 full */
	TFIFO7		= 4<<0,		/*	7/8 full */
};

enum {					/* Imsc, Mis, Icr */
	Oeint		= 0x400,
	Beint		= 0x200,
	Peint		= 0x100,
	Feint		= 0x080,
	Rtint		= 0x040,
	Txint		= 0x020,
	Rxint		= 0x010,
	Ctsmint		= 0x002,
};
	

typedef struct Ctlr {
	u32int*	io;
	int	irq;
	int	iena;
	int	poll;

	ushort	sticky[Imsc+1];

	Lock;
	int	fena;
} Ctlr;

extern PhysUart pl011physuart;

/*
* pi4 has five pl011 uarts, other pi models have only one
*/
static Ctlr pl011ctlr[] = {
{	.io	= (u32int*)(VIRTIO+0x201000),
	.irq	= IRQpl011,
	.poll	= 0, },
{	.io	= (u32int*)(VIRTIO+0x201400),
	.irq	= IRQpl011,
	.poll	= 0, },
{	.io	= (u32int*)(VIRTIO+0x201600),
	.irq	= IRQpl011,
	.poll	= 0, },
{	.io	= (u32int*)(VIRTIO+0x201800),
	.irq	= IRQpl011,
	.poll	= 0, },
{	.io	= (u32int*)(VIRTIO+0x201A00),
	.irq	= IRQpl011,
	.poll	= 0, },
};

static Uart pl011uart[] = {
{	.regs	= &pl011ctlr[0],
	.name	= "uart1",
	.freq	= 0,	/* Not used */
	.baud	= 115200,
	.phys	= &pl011physuart,
	.next	= &pl011uart[1], },
{	.regs	= &pl011ctlr[1],
	.name	= "uart2",
	.freq	= 0,
	.baud	= 115200,
	.phys	= &pl011physuart,
	.next	= &pl011uart[2], },
{	.regs	= &pl011ctlr[2],
	.name	= "uart3",
	.freq	= 0,
	.baud	= 115200,
	.phys	= &pl011physuart,
	.next	= &pl011uart[3], },
{	.regs	= &pl011ctlr[3],
	.name	= "uart4",
	.freq	= 0,
	.baud	= 115200,
	.phys	= &pl011physuart,
	.next	= &pl011uart[4], },
{	.regs	= &pl011ctlr[4],
	.name	= "uart5",
	.freq	= 0,
	.baud	= 115200,
	.phys	= &pl011physuart,
	.next	= nil, },
};

static ulong pl011freq = 48000000;

#define csr8r(c, r)	((c)->io[r])
#define csr8w(c, r, v)	((c)->io[r] = (c)->sticky[r] | (v), coherence())
#define csr8o(c, r, v)	((c)->io[r] = (v), coherence())

static long
pl011status(Uart* uart, void* buf, long n, long offset)
{
	char *p;
	Ctlr *ctlr;
	ushort ier, lcr, mcr, msr;

	ctlr = uart->regs;
	p = malloc(READSTR);
	mcr = ctlr->sticky[Cr];
	msr = csr8r(ctlr, Fr);
	ier = ctlr->sticky[Imsc];
	lcr = ctlr->sticky[Lcrh];
	snprint(p, READSTR,
		"b%d c%d d%d e%d l%d m%d p%c r%d s%d i%d\n"
		"dev(%d) type(%d) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)%s\n",

		uart->baud,
		uart->hup_dcd,
		1,
		uart->hup_dsr,
		((lcr & WlsMASK) >> 5) + 5,
		(ier & Ctsmint) != 0,
		(lcr & Pen) ? ((lcr & Eps) ? 'e': 'o'): 'n',
		(mcr & Rts) != 0,
		(lcr & Stb) ? 2: 1,
		ctlr->fena,

		uart->dev,
		uart->type,
		uart->ferr,
		uart->oerr,
		uart->berr,
		uart->serr,
		(msr & Cts) ? " cts": ""
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static void
pl011fifo(Uart* uart, int level)
{
	Ctlr *ctlr;

	ctlr = uart->regs;

	/*
	 * Changing the FIFOena bit in Fcr flushes data
	 * from both receive and transmit FIFOs; there's
	 * no easy way to guarantee not losing data on
	 * the receive side, but it's possible to wait until
	 * the transmitter is really empty.
	 */
	ilock(ctlr);
	while(!(csr8r(ctlr, Fr) & Txfe))
		;

	/*
	 * Set the trigger level, default is the max.
	 * value.
	 */
	ctlr->fena = level;
	switch(level){
	case 0:
		break;
	case 4:
		level = RFIFO1|TFIFO7;
		break;
	case 8:
		level = RFIFO2|TFIFO6;
		break;
	case 16:
		level = RFIFO4|TFIFO4;
		break;
	case 24:
		level = RFIFO6|TFIFO2;
		break;
	case 28:
	default:
		level = RFIFO7|TFIFO1;
		break;
	}
	csr8w(ctlr, Ifls, level);
	if(ctlr->fena)
		ctlr->sticky[Lcrh] |= Fen;
	else
		ctlr->sticky[Lcrh] &= ~Fen;
	csr8w(ctlr, Lcrh, 0);
	iunlock(ctlr);
}

static void
pl011dtr(Uart* uart, int on)
{
	USED(uart);
	USED(on);
}

static void
pl011rts(Uart* uart, int on)
{
	Ctlr *ctlr;

	/*
	 * Toggle RTS.
	 */
	ctlr = uart->regs;
	if(on)
		ctlr->sticky[Cr] |= Rts;
	else
		ctlr->sticky[Cr] &= ~Rts;
	csr8w(ctlr, Cr, 0);
}

static void
pl011modemctl(Uart* uart, int on)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	ilock(&uart->tlock);
	if(on){
		ctlr->sticky[Imsc] |= Ctsmint;
		csr8w(ctlr, Imsc, 0);
		uart->modem = 1;
		uart->cts = csr8r(ctlr, Fr) & Cts;
	}
	else{
		ctlr->sticky[Imsc] &= ~Ctsmint;
		csr8w(ctlr, Imsc, 0);
		uart->modem = 0;
		uart->cts = 1;
	}
	iunlock(&uart->tlock);
}

static int
pl011parity(Uart* uart, int parity)
{
	int lcr;
	Ctlr *ctlr;

	ctlr = uart->regs;
	lcr = ctlr->sticky[Lcrh] & ~(Eps|Pen);

	switch(parity){
	case 'e':
		lcr |= Eps|Pen;
		break;
	case 'o':
		lcr |= Pen;
		break;
	case 'n':
		break;
	default:
		return -1;
	}
	ctlr->sticky[Lcrh] = lcr;
	csr8w(ctlr, Lcrh, 0);

	uart->parity = parity;

	return 0;
}

static int
pl011stop(Uart* uart, int stop)
{
	int lcr;
	Ctlr *ctlr;

	ctlr = uart->regs;
	lcr = ctlr->sticky[Lcrh] & ~Stb;

	switch(stop){
	case 1:
		break;
	case 2:
		lcr |= Stb;
		break;
	default:
		return -1;
	}
	ctlr->sticky[Lcrh] = lcr;
	csr8w(ctlr, Lcrh, 0);

	uart->stop = stop;

	return 0;
}

static int
pl011bits(Uart* uart, int bits)
{
	int lcr;
	Ctlr *ctlr;

	ctlr = uart->regs;
	lcr = ctlr->sticky[Lcrh] & ~WlsMASK;

	switch(bits){
	case 5:
		lcr |= Wls5;
		break;
	case 6:
		lcr |= Wls6;
		break;
	case 7:
		lcr |= Wls7;
		break;
	case 8:
		lcr |= Wls8;
		break;
	default:
		return -1;
	}
	ctlr->sticky[Lcrh] = lcr;
	csr8w(ctlr, Lcrh, 0);

	uart->bits = bits;

	return 0;
}

static int
pl011baud(Uart* uart, int baud)
{
	ulong bgc;
	Ctlr *ctlr;

	/*
	 * Set the Baud rate by calculating and setting the Baud rate
	 * Generator Constant. This will work with fairly non-standard
	 * Baud rates.
	 */
	if(pl011freq == 0 || baud <= 0)
		return -1;
	bgc = 16*baud;

	ctlr = uart->regs;
	csr8o(ctlr, Ibrd, pl011freq / bgc);
	csr8o(ctlr, Fbrd, pl011freq % bgc);
	/*
	 * Internally Ibrd Fbrd and Lcrh share a single register,
	 * updated only when Lcrh is written
	 */
	csr8w(ctlr, Lcrh, 0);
	uart->baud = baud;
	return 0;
}

static void
pl011break(Uart* uart, int ms)
{
	Ctlr *ctlr;

	if (up == nil)
		panic("pl011break: nil up");
	/*
	 * Send a break.
	 */
	if(ms <= 0)
		ms = 200;

	ctlr = uart->regs;
	csr8w(ctlr, Lcrh, Brk);
	tsleep(&up->sleep, return0, 0, ms);
	csr8w(ctlr, Lcrh, 0);
}

static void
pl011kick(Uart* uart)
{
	int i;
	Ctlr *ctlr;

	if(/* uart->cts == 0 || */ uart->blocked)
		return;

	ctlr = uart->regs;

	/*
	 *  128 here is an arbitrary limit to make sure
	 *  we don't stay in this loop too long.  If the
	 *  chip's output queue is longer than 128, too
	 *  bad -- presotto
	 */
	for(i = 0; i < 128; i++){
		if(csr8r(ctlr, Fr) & Txff)
			break;
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		csr8o(ctlr, Dr, *uart->op++);		/* start tx */
	}
	if(csr8r(ctlr, Fr) & Txfe)
		ctlr->sticky[Imsc] &= ~Txint;
	else
		ctlr->sticky[Imsc] |= Txint;
	csr8w(ctlr, Imsc, 0);			/* intr when done */
}

static void
pl011interrupt(Ureg*, void* arg)
{
	Ctlr *ctlr;
	Uart *uart;
	int iir, old, r;

	uart = arg;
	ctlr = uart->regs;
	for(iir = csr8r(ctlr, Mis); iir; iir = csr8r(ctlr, Mis)){
		if(iir & Ctsmint){
			r = csr8r(ctlr, Fr);
			if(1){
				ilock(&uart->tlock);
				old = uart->cts;
				uart->cts = r & Cts;
				if(old == 0 && uart->cts)
					uart->ctsbackoff = 2;
				iunlock(&uart->tlock);
			}
		}
		if(iir & Txint){
			uartkick(uart);
		}
		if(iir & (Oeint|Beint|Peint|Feint|Rtint|Rxint)){
			/*
			 * Consume any received data.
			 * If the received byte came in with a break,
			 * parity or framing error, throw it away;
			 * overrun is an indication that something has
			 * already been tossed.
			 */
			while(!(csr8r(ctlr, Fr) & Rxfe)){
				r = csr8r(ctlr, Dr);
				if(r & Oe)
					uart->oerr++;
				if(r & Pe)
					uart->perr++;
				if(r & Fe)
					uart->ferr++;
				if(!(r & (Be|Fe|Pe)))
					uartrecv(uart, r & 0xFF);
			}
		}
		csr8o(ctlr, Icr, iir);
	}
}

static void
pl011disable(Uart* uart)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	ctlr->sticky[Imsc] = 0;
	csr8w(ctlr, Imsc, 0);
	ctlr->sticky[Cr] = 0;
	csr8w(ctlr, Cr, 0);
}

static void
pl011enable(Uart* uart, int ie)
{
	Ctlr *ctlr;
	Pinctl *p;

	ctlr = uart->regs;
	p = &pinctl[uart->dev - pl011uart[0].dev];
	gpiosel(p->txpin, p->alt);
	gpiosel(p->rxpin, p->alt);
	gpiopulloff(p->txpin);
	gpiopullup(p->rxpin);

	csr8o(ctlr, Cr, 0);
	ctlr->sticky[Lcrh] = Wls8;		/* no parity */
	csr8w(ctlr, Lcrh, 0);
	pl011baud(uart, uart->baud);

	/*
	 * Enable interrupts and turn on DTR and RTS.
	 * Be careful if this is called to set up a polled serial line
	 * early on not to try to enable interrupts as interrupt-
	 * -enabling mechanisms might not be set up yet.
	 */
	if(ie){
		if(ctlr->iena == 0 && !ctlr->poll){
			intrenable(ctlr->irq, pl011interrupt, uart, 0, uart->name);
			ctlr->iena = 1;
		}
		ctlr->sticky[Imsc] = Rxint|Rtint;
	}
	else{
		ctlr->sticky[Imsc] = 0;
	}
	csr8w(ctlr, Imsc, 0);
	
	ctlr->sticky[Cr] = Uarten|Rxe|Txe|Rts;
	csr8w(ctlr, Cr, 0);
}

static Uart*
pl011pnp(void)
{
	Uart *uart;
	Ctlr *ctlr;

	uart = pl011uart;
	ctlr = &pl011ctlr[1];
	csr8o(ctlr, Cr, Ctsen|Rtsen);
	if(csr8r(ctlr, Cr) != (Ctsen|Rtsen))
		/* uarts 2-5 don't exist */
		uart->next = nil;
	csr8o(ctlr, Cr, 0);
	return uart;
}

static int
pl011getc(Uart* uart)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	while((csr8r(ctlr, Fr) & Rxfe))
		delay(1);
	return csr8r(ctlr, Dr) & 0xFF;
}

static void
pl011putc(Uart* uart, int c)
{
	int i;
	Ctlr *ctlr;

	ctlr = uart->regs;
	for(i = 0; !(csr8r(ctlr, Fr) & Txfe) && i < 128; i++)
		delay(1);
	csr8o(ctlr, Dr, (uchar)c);
	for(i = 0; !(csr8r(ctlr, Fr) & Txfe) && i < 128; i++)
		delay(1);
}

void
pl011consinit(void)
{
	Uart *uart;
	int n;
	char *p, *cmd;

	if((p = getconf("console")) == nil)
		return;
	n = strtoul(p, &cmd, 0);
	if(p == cmd)
		return;
	if(n < 1 || n > 5)
		return;
	uart = &pl011uart[n-1];

	if(!uart->enabled)
		(*uart->phys->enable)(uart, 0);
	uartctl(uart, "l8 pn s1");
	if(*cmd != '\0')
		uartctl(uart, cmd);

	consuart = uart;
	uart->console = 1;
}

void (*pl011init)(void) = pl011consinit;

PhysUart pl011physuart = {
	.name		= "pl011",
	.pnp		= pl011pnp,
	.enable		= pl011enable,
	.disable	= pl011disable,
	.kick		= pl011kick,
	.dobreak	= pl011break,
	.baud		= pl011baud,
	.bits		= pl011bits,
	.stop		= pl011stop,
	.parity		= pl011parity,
	.modemctl	= pl011modemctl,
	.rts		= pl011rts,
	.dtr		= pl011dtr,
	.status		= pl011status,
	.fifo		= pl011fifo,
	.getc		= pl011getc,
	.putc		= pl011putc,
};
