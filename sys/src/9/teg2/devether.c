#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#include "../port/netif.h"
#include "etherif.h"

static Ether *etherxx[MaxEther];

Chan*
etherattach(char* spec)
{
	int ctlrno;
	char *p;
	Chan *chan;

	ctlrno = 0;
	if(spec && *spec){
		ctlrno = strtoul(spec, &p, 0);
		if((ctlrno == 0 && p == spec) || *p != 0)
			error(Ebadarg);
		if(ctlrno < 0 || ctlrno >= MaxEther)
			error(Ebadarg);
	}
	if(etherxx[ctlrno] == 0)
		error(Enodev);

	chan = devattach('l', spec);
	if(waserror()){
		chanfree(chan);
		nexterror();
	}
	chan->dev = ctlrno;
	if(etherxx[ctlrno]->attach)
		etherxx[ctlrno]->attach(etherxx[ctlrno]);
	poperror();
	return chan;
}

static Walkqid*
etherwalk(Chan* chan, Chan* nchan, char** name, int nname)
{
	return netifwalk(etherxx[chan->dev], chan, nchan, name, nname);
}

static int
etherstat(Chan* chan, uchar* dp, int n)
{
	return netifstat(etherxx[chan->dev], chan, dp, n);
}

static Chan*
etheropen(Chan* chan, int omode)
{
	return netifopen(etherxx[chan->dev], chan, omode);
}

static void
ethercreate(Chan*, char*, int, ulong)
{
}

static void
etherclose(Chan* chan)
{
	netifclose(etherxx[chan->dev], chan);
}

static long
etherread(Chan* chan, void* buf, long n, vlong off)
{
	Ether *ether;
	ulong offset = off;

	ether = etherxx[chan->dev];
	if((chan->qid.type & QTDIR) == 0 && ether->ifstat){
		/*
		 * With some controllers it is necessary to reach
		 * into the chip to extract statistics.
		 */
		if(NETTYPE(chan->qid.path) == Nifstatqid)
			return ether->ifstat(ether, buf, n, offset);
		else if(NETTYPE(chan->qid.path) == Nstatqid)
			ether->ifstat(ether, buf, 0, offset);
	}

	return netifread(ether, chan, buf, n, offset);
}

static Block*
etherbread(Chan* chan, long n, ulong offset)
{
	return netifbread(etherxx[chan->dev], chan, n, offset);
}

static int
etherwstat(Chan* chan, uchar* dp, int n)
{
	return netifwstat(etherxx[chan->dev], chan, dp, n);
}

static void
etherrtrace(Netfile* f, Etherpkt* pkt, int len)
{
	int i, n;
	Block *bp;

	if(qwindow(f->in) <= 0)
		return;
	if(len > 58)
		n = 58;
	else
		n = len;
	bp = iallocb(64);
	if(bp == nil)
		return;
	memmove(bp->wp, pkt->d, n);
	i = TK2MS(MACHP(0)->ticks);
	bp->wp[58] = len>>8;
	bp->wp[59] = len;
	bp->wp[60] = i>>24;
	bp->wp[61] = i>>16;
	bp->wp[62] = i>>8;
	bp->wp[63] = i;
	bp->wp += 64;
	qpass(f->in, bp);
}

Block*
etheriq(Ether* ether, Block* bp, int fromwire)
{
	Etherpkt *pkt;
	ushort type;
	int len, multi, tome, fromme;
	Netfile **ep, *f, **fp, *fx;
	Block *xbp;

	ether->inpackets++;

	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	type = (pkt->type[0]<<8)|pkt->type[1];
	fx = 0;
	ep = &ether->f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multicast addresses */
	if(multi && memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) != 0 &&
	    ether->prom == 0){
		if(!activemulti(ether, pkt->d, sizeof(pkt->d))){
			if(fromwire){
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}
	/* is it for me? */
	tome = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	fromme = memcmp(pkt->s, ether->ea, sizeof(pkt->s)) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for(fp = ether->f; fp < ep; fp++){
		if((f = *fp) != nil && (f->type == type || f->type < 0) &&
		    (tome || multi || f->prom)){
			/* Don't want to hear bridged packets */
			if(f->bridge && !fromwire && !fromme)
				continue;
			if(!f->headersonly){
				if(fromwire && fx == 0)
					fx = f;
				else if(xbp = iallocb(len)){
					memmove(xbp->wp, pkt, len);
					xbp->wp += len;
					if(qpass(f->in, xbp) < 0)
						ether->soverflows++;
				}
				else
					ether->soverflows++;
			}
			else
				etherrtrace(f, pkt, len);
		}
	}

	if(fx){
		if(qpass(fx->in, bp) < 0)
			ether->soverflows++;
		return 0;
	}
	if(fromwire){
		freeb(bp);
		return 0;
	}
	return bp;
}

static int
etheroq(Ether* ether, Block* bp)
{
	int len, loopback, s;
	Etherpkt *pkt;

	ether->outpackets++;

	/*
	 * Check if the packet has to be placed back onto the input queue,
	 * i.e. if it's a loopback or broadcast packet or the interface is
	 * in promiscuous mode.
	 * If it's a loopback packet indicate to etheriq that the data isn't
	 * needed and return, etheriq will pass-on or free the block.
	 * To enable bridging to work, only packets that were originated
	 * by this interface are fed back.
	 */
	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	loopback = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	if(loopback || memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) == 0 || ether->prom){
		s = splhi();
		etheriq(ether, bp, 0);
		splx(s);
	}

	if(!loopback){
		qbwrite(ether->oq, bp);
		if(ether->transmit != nil)
			ether->transmit(ether);
	} else
		freeb(bp);

	return len;
}

static long
etherwrite(Chan* chan, void* buf, long n, vlong)
{
	Ether *ether;
	Block *bp;
	int nn, onoff;
	Cmdbuf *cb;

	ether = etherxx[chan->dev];
	if(NETTYPE(chan->qid.path) != Ndataqid) {
		nn = netifwrite(ether, chan, buf, n);
		if(nn >= 0)
			return nn;
		cb = parsecmd(buf, n);
		if(cb->f[0] && strcmp(cb->f[0], "nonblocking") == 0){
			if(cb->nf <= 1)
				onoff = 1;
			else
				onoff = atoi(cb->f[1]);
			qnoblock(ether->oq, onoff);
			free(cb);
			return n;
		}
		free(cb);
		if(ether->ctl!=nil)
			return ether->ctl(ether,buf,n);

		error(Ebadctl);
	}

	if(n > ether->maxmtu)
		error(Etoobig);
	if(n < ether->minmtu)
		error(Etoosmall);

	bp = allocb(n);
	if(waserror()){
		freeb(bp);
		nexterror();
	}
	memmove(bp->rp, buf, n);
	memmove(bp->rp+Eaddrlen, ether->ea, Eaddrlen);
	poperror();
	bp->wp += n;

	return etheroq(ether, bp);
}

static long
etherbwrite(Chan* chan, Block* bp, ulong)
{
	Ether *ether;
	long n;

	n = BLEN(bp);
	if(NETTYPE(chan->qid.path) != Ndataqid){
		if(waserror()) {
			freeb(bp);
			nexterror();
		}
		n = etherwrite(chan, bp->rp, n, 0);
		poperror();
		freeb(bp);
		return n;
	}
	ether = etherxx[chan->dev];

	if(n > ether->maxmtu){
		freeb(bp);
		error(Etoobig);
	}
	if(n < ether->minmtu){
		freeb(bp);
		error(Etoosmall);
	}

	return etheroq(ether, bp);
}

static struct {
	char*	type;
	int	(*reset)(Ether*);
} cards[MaxEther+1];

void
addethercard(char* t, int (*r)(Ether*))
{
	static int ncard;

	if(ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

int
parseether(uchar *to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	for(i = 0; i < Eaddrlen; i++){
		if(*p == 0)
			return -1;
		nip[0] = *p++;
		if(*p == 0)
			return -1;
		nip[1] = *p++;
		nip[2] = 0;
		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return 0;
}

static void
etherreset(void)
{
	Ether *ether;
	int i, n, ctlrno;
	char name[KNAMELEN], buf[128];

	for(ether = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if(ether == 0)
			ether = malloc(sizeof(Ether));
		memset(ether, 0, sizeof(Ether));
		ether->ctlrno = ctlrno;
		ether->mbps = 10;
		ether->minmtu = ETHERMINTU;
		ether->maxmtu = ETHERMAXTU;

		if(archether(ctlrno, ether) <= 0)
			continue;

		if(isaconfig("ether", ctlrno, ether) == 0){
//			free(ether);
//			return nil;
			continue;
		}
		for(n = 0; cards[n].type; n++){
			if(cistrcmp(cards[n].type, ether->type))
				continue;
			for(i = 0; i < ether->nopt; i++)
				if(cistrncmp(ether->opt[i], "ea=", 3) == 0){
					if(parseether(ether->ea,
					    &ether->opt[i][3]) == -1)
						memset(ether->ea, 0, Eaddrlen);
				} else if(cistrcmp(ether->opt[i],
				    "100BASE-TXFD") == 0)
					ether->mbps = 100;
			if(cards[n].reset(ether))
				break;
			snprint(name, sizeof(name), "ether%d", ctlrno);

			if(ether->interrupt != nil && ether->irq >= 0)
				intrenable(ether->irq, ether->interrupt,
					ether, 0, name);

			i = snprint(buf, sizeof buf,
				"#l%d: %s: %dMbps port %#lux irq %d",
				ctlrno, ether->type, ether->mbps, ether->port,
				ether->irq);
			if(ether->mem)
				i += snprint(buf+i, sizeof buf - i,
					" addr %#lux", PADDR(ether->mem));
			if(ether->size)
				i += snprint(buf+i, sizeof buf - i,
					" size %#luX", ether->size);
			i += snprint(buf+i, sizeof buf - i,
				": %2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux",
				ether->ea[0], ether->ea[1], ether->ea[2],
				ether->ea[3], ether->ea[4], ether->ea[5]);
			snprint(buf+i, sizeof buf - i, "\n");
			iprint("%s", buf);  /* it may be too early for print */

			if(ether->mbps >= 1000)
				netifinit(ether, name, Ntypes, 4*1024*1024);
			else if(ether->mbps >= 100)
				netifinit(ether, name, Ntypes, 1024*1024);
			else
				netifinit(ether, name, Ntypes, 65*1024);
			if(ether->oq == 0)
				ether->oq = qopen(ether->limit, Qmsg, 0, 0);
			if(ether->oq == 0)
				panic("etherreset %s", name);
			ether->alen = Eaddrlen;
			memmove(ether->addr, ether->ea, Eaddrlen);
			memset(ether->bcast, 0xFF, Eaddrlen);

			etherxx[ctlrno] = ether;
			ether = 0;
			break;
		}
	}
	if(ether)
		free(ether);
}

static void
ethershutdown(void)
{
	Ether *ether;
	int i;

	for(i = 0; i < MaxEther; i++){
		ether = etherxx[i];
		if(ether == nil)
			continue;
		if(ether->shutdown == nil) {
			print("#l%d: no shutdown function\n", i);
			continue;
		}
		(*ether->shutdown)(ether);
	}
}


#define POLY 0xedb88320

/* really slow 32 bit crc for ethers */
ulong
ethercrc(uchar *p, int len)
{
	int i, j;
	ulong crc, b;

	crc = 0xffffffff;
	for(i = 0; i < len; i++){
		b = *p++;
		for(j = 0; j < 8; j++){
			crc = (crc>>1) ^ (((crc^b) & 1) ? POLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

void
dumpoq(Queue *oq)
{
	if (oq == nil)
		print("no outq! ");
	else if (qisclosed(oq))
		print("outq closed ");
	else if (qfull(oq))
		print("outq full ");
	else
		print("outq %d ", qlen(oq));
}

void
dumpnetif(Netif *netif)
{
	print("netif %s ", netif->name);
	print("limit %d mbps %d link %d ",
		netif->limit, netif->mbps, netif->link);
	print("inpkts %lld outpkts %lld errs %d\n",
		netif->inpackets, netif->outpackets,
		netif->crcs + netif->oerrs + netif->frames + netif->overflows +
		netif->buffs + netif->soverflows);
}

Dev etherdevtab = {
	'l',
	"ether",

	etherreset,
	devinit,
	ethershutdown,
	etherattach,
	etherwalk,
	etherstat,
	etheropen,
	ethercreate,
	etherclose,
	etherread,
	etherbread,
	etherwrite,
	etherbwrite,
	devremove,
	etherwstat,
};
