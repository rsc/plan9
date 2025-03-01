#include "all.h"
#include <fcall.h>
#include <thread.h>
#include <9p.h>

char *mtpt = "/mnt/vmware";
char *srvname;
uint time0;

enum
{
	Qroot = 0,
	Qmousepoint,
	Qsnarf,
	Qgui,
	Qdev,
	Qtime,
	Qbintime,
	Qmsg,
};

typedef struct Tab Tab;
struct Tab
{
	int qid;
	char *name;
	uint perm;
	uint vers;
	void (*open)(Req*);
	void (*read)(Req*);
	void (*write)(Req*);
	void (*close)(Fid*);
};

static void
mousepointread(Req *r)
{
	char buf[32];
	Point p;

	p = getmousepoint();
	snprint(buf, sizeof buf, "%11d %11d ", p.x, p.y);
	readstr(r, buf);
	respond(r, nil);
}

static void
mousepointwrite(Req *r)
{
	char buf[32], *f[3];
	int nf, n;
	Point p;

	n = r->ifcall.count;
	if(n >= sizeof buf){
		respond(r, "write too large");
		return;
	}
	memmove(buf, r->ifcall.data, n);
	buf[n] = '\0';
	nf = tokenize(buf, f, nelem(f));
	if(nf != 2){
		respond(r, "bad point format");
		return;
	}
	p.x = atoi(f[0]);
	p.y = atoi(f[1]);
	setmousepoint(p);
	respond(r, nil);
}

static void
timeread(Req *r)
{
	char buf[32];
	uint sec, microsec, lag;

	gettime(&sec, &microsec, &lag);
	snprint(buf, sizeof buf, "%11d ", sec);
	readstr(r, buf);
	respond(r, nil);
}

static uvlong uvorder = 0x0001020304050607ULL;
static uchar*
vlong2le(uchar *t, vlong from)
{
	uchar *f, *o;
	int i;

	f = (uchar*)&from;
	o = (uchar*)&uvorder;
	for(i = 0; i < sizeof(vlong); i++)
		t[i] = f[o[i]];
	return t+sizeof(vlong);
}

static void
bintimeread(Req *r)
{
	uchar *b;
	int i, n;
	uint sec, microsec, lag;
	vlong nsec;

	b = (uchar*)r->ofcall.data;
	n = r->ifcall.count;

	i = 0;
	if(n >= 8){
		gettime(&sec, &microsec, &lag);
		nsec = sec*1000000000LL+microsec*1000LL;
		vlong2le(b, nsec);
		i = 8;
	}
	if(n >= 16){
		vlong2le(b+8, nsec);
		i = 16;
	}
	if(n >= 24){
		vlong2le(b+16, 1000000000LL);
		i = 24;
	}
	r->ofcall.count = i;
	respond(r, nil);
}

char *snarf;
int nsnarf;
char *tsnarf;
int ntsnarf;

static void
snarfread(Req *r)
{
	int i;

	if(r->ifcall.offset == 0){
		if(snarf)
			free(snarf);
		nsnarf = getsnarflength();
		snarf = emalloc9p(nsnarf+4+1);
		for(i=0; i<nsnarf; i+=4)
			*(uint*)(snarf+i) = getsnarfpiece();
		snarf[nsnarf] = '\0';
		nsnarf = strlen(snarf);	/* there's extra crap because we have to transfer 4 bytes at a time */
	}

	readbuf(r, snarf, nsnarf);
	respond(r, nil);
}

static void
snarfwrite(Req *r)
{
	if(r->ifcall.offset == 0){
		free(tsnarf);
		tsnarf = nil;
		ntsnarf = 0;
	}
	if(r->ifcall.offset > 100*1024){
		respond(r, "snarf buffer too long");
		return;
	}
	tsnarf = erealloc9p(tsnarf, ntsnarf+r->ifcall.count);
	memmove(tsnarf+ntsnarf, r->ifcall.data, r->ifcall.count);
	ntsnarf += r->ifcall.count;
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static void
snarfclose(Fid *fid)
{
	int i, n;

	if((fid->omode&3) == OREAD)
		return;

	// read old snarf - dunno why but it helps
	n = getsnarflength();
	for(i=0; i<n; i+=4)
		getsnarfpiece();

	setsnarflength(ntsnarf);
	for(i=0; i<ntsnarf; i+=4)
		setsnarfpiece(*(uint*)(tsnarf+i));
	free(tsnarf);
	tsnarf = nil;
	ntsnarf = 0;
}

typedef struct Bit Bit;
struct Bit 
{
	char *name;
	uint bit;
};

Bit guibit[] = {
	"autograb",	1,
	"autorelease",	2,
	"autoscroll",	4,
	"autoraise",	8,
	"copypaste",	0x10,
	"hidecursor",	0x20,
	"fullscreen",	0x40,
	"tofullscreen",	0x80,
	"towindow",	0x100,
	"autoraise-disabled",	0x200,
	"synctime",	0x400,
};

static void
guiread(Req *r)
{
	int i;
	char *s;
	Fmt fmt;
	uint val;

	val = getguistate();
	fmtstrinit(&fmt);
	for(i=0; i<nelem(guibit); i++)
		fmtprint(&fmt, "%s %s\n", guibit[i].name, (val & guibit[i].bit) ? "on" : "off");
	s = fmtstrflush(&fmt);
	readstr(r, s);
	free(s);
	respond(r, nil);
}

static void
guiwrite(Req *r)
{
	int i, on;
	uint v;
	Cmdbuf *cb;

	cb = parsecmd(r->ifcall.data, r->ifcall.count);
	if(cb->nf != 2){
		respondcmderror(r, cb, "bad gui ctl");
		free(cb);
		return;
	}

	if(strcmp(cb->f[1], "off")==0)
		on = 0;
	else if(strcmp(cb->f[1], "on") == 0)
		on = 1;
	else{
		respondcmderror(r, cb, "bad gui ctl");
		free(cb);
		return;
	}

	for(i=0; i<nelem(guibit); i++)
		if(strcmp(guibit[i].name, cb->f[0]) == 0)
			goto Have;
	respondcmderror(r, cb, "bad gui ctl");
	free(cb);
	return;

Have:
	v = getguistate();
	if(on)
		v |= guibit[i].bit;
	else
		v &= ~guibit[i].bit;
	setguistate(v);
	r->ofcall.count = r->ifcall.count;
	free(cb);
	respond(r, nil);
}

typedef struct Info Info;
struct Info
{
	char name[32];
	uint uid;
	uint enabled;
};
static int
getinfo(uint id, Info *p)
{
	uint i;

	for(i=0; i<sizeof(Info); i+=4)
		if(getdeviceinfo(id, i, (uint*)((uchar*)p+i)) == 0)
			return -1;
	return 0;
}

static void
devread(Req *r)
{
	int i;
	char *s;
	Fmt fmt;
	Info info;

	fmtstrinit(&fmt);
	memset(&info, 0, sizeof info);
	for(i=0; i<100; i++){
		if(getinfo(i, &info) < 0)
			break;
		fmtprint(&fmt, "%11d %q %s\n", info.uid, info.name, info.enabled ? "on" : "off");
	}
	s = fmtstrflush(&fmt);
	readstr(r, s);
	respond(r, nil);
	free(s);
}

static void
fsmsgread(Req *r)
{
	char *s;
	Msgchan *c;

	c = r->fid->aux;
	if(c == nil){
		respond(r, "message channel not open");
		return;
	}

	if(r->ifcall.offset == 0){
		if(recvmsg(c, &s) < 0){
			respond(r, "no messages waiting");
			return;
		}
	}
	if(c->a == nil){
		respond(r, "no messages waiting");
		return;
	}
	readbuf(r, c->a, c->na);
	respond(r, nil);
}

static void
fsmsgwrite(Req *r)
{
	char buf[32], *p;
	int n;
	Msgchan *c;

	if(r->ifcall.offset != 0){
		respond(r, "must write at offset zero");
		return;
	}

	r->ofcall.count = r->ifcall.count;
	c = r->fid->aux;
	if(c == nil){
		if(r->ifcall.count >= sizeof buf){
			respond(r, "bad message channel number");
			return;
		}
		memmove(buf, r->ifcall.data, r->ifcall.count);
		buf[r->ifcall.count] = 0;
		p = buf;
		n = strtol(buf, &p, 0);
		if(p == buf){
			respond(r, "bad message channel number");
			return;
		}
		c = openmsg(n);
		if(c == nil){
			respond(r, "could not open message channel");
			return;
		}
		r->fid->aux = c;
		respond(r, nil);
		return;
	}

	if(sendmsg(c, r->ifcall.data, r->ifcall.count) < 0){
		respond(r, "could not send message");
		return;
	}
	respond(r, nil);
}

static void
fsmsgclose(Fid *fid)
{
	Msgchan *c;

	c = fid->aux;
	if(c)
		closemsg(c);
}

Tab tab[] = {
	Qmousepoint, "mouse", 0666, 0, nil, mousepointread, mousepointwrite, nil,
	Qsnarf, "snarf", 0666, 0, nil, snarfread, snarfwrite, snarfclose,
	Qgui, "gui", 0666, 0, nil, guiread, guiwrite, nil,
	Qdev, "dev", 0444, 0, nil, devread, nil, nil,
	Qtime, "time", 0444, 0, nil, timeread, nil, nil,
	Qbintime, "bintime", 0444, 0, nil, bintimeread, nil, nil,
	Qmsg, "msg", 0666, 0, nil, fsmsgread, fsmsgwrite, fsmsgclose,
};

void
fsattach(Req *r)
{
	char *spec;

	spec = r->ifcall.aname;
	if(spec && spec[0]){
		respond(r, "invalid attach specifier");
		return;
	}
	r->ofcall.qid = (Qid){Qroot, 0, QTDIR};
	r->fid->qid = r->ofcall.qid;
	respond(r, nil);
}

char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i;

	switch((int)fid->qid.path){
	case Qroot:
		for(i=0; i<nelem(tab); i++){
			if(strcmp(name, tab[i].name) == 0){
				fid->qid.path = tab[i].qid;
				fid->qid.type = tab[i].perm>>24;;
				fid->qid.vers = tab[i].vers;
				*qid = fid->qid;
				return nil;
			}
		}
		break;
	}

	return "file not found";
}

void
fsstat(Req *r)
{
	int i, q;
	Dir *d;

	d = &r->d;
	memset(d, 0, sizeof *d);
	q = r->fid->qid.path;
	d->qid = r->fid->qid;
	switch(q){
	case Qroot:
		d->name = estrdup9p("/");
		d->mode = DMDIR|0777;
		break;

	default:
		for(i=0; i<nelem(tab); i++){
			if(tab[i].qid == q){
				d->qid.vers = tab[i].vers;
				d->qid.type = tab[i].perm>>24;
				d->mode = tab[i].perm;
				goto Out;
			}
		}
		respond(r, "file not found");
	}

Out:
	d->atime = d->mtime = time0;
	d->uid = estrdup9p("vmware");
	d->gid = estrdup9p("vmware");
	d->muid = estrdup9p("");
	respond(r, nil);
}

int
dirgen(int off, Dir *d, void*)
{
	if(off >= nelem(tab))
		return -1;

	memset(d, 0, sizeof *d);
	d->atime = d->mtime = time0;
	d->name = estrdup9p(tab[off].name);
	d->mode = tab[off].perm;
	d->qid.path = tab[off].qid;
	d->qid.vers = tab[off].vers;
	d->qid.type = d->mode>>24;
	d->uid = estrdup9p("vmware");
	d->gid = estrdup9p("vmware");
	d->muid = estrdup9p("");
	return 0;
}

void
fsread(Req *r)
{
	int i, q;

	q = r->fid->qid.path;
	switch(q){
	case Qroot:
		dirread9p(r, dirgen, nil);
		respond(r, nil);
		return;

	default:
		for(i=0; i<nelem(tab); i++)
			if(tab[i].qid == q)
				goto Have;

		respond(r, "cannot happen in fsread");
		return;

	Have:
		if(tab[i].read == nil){
			respond(r, "no read function");
			return;
		}
		tab[i].read(r);
		return;
	}
}

void
fswrite(Req *r)
{
	int i, q;

	q = r->fid->qid.path;
	for(i=0; i<nelem(tab); i++)
		if(tab[i].qid == q){
			if(tab[i].write == nil){
				respond(r, "no write function");
				return;
			}
			tab[i].write(r);
			return;
		}

	respond(r, "cannot happen in fswrite");
}

void
fsopen(Req *r)
{
	int i, q;

	q = r->fid->qid.path;
	for(i=0; i<nelem(tab); i++)
		if(tab[i].qid == q){
			switch(r->ifcall.mode&3){
			case OREAD:
				if(!(tab[i].perm&0400))
					goto Eperm;
				break;
			case OWRITE:
				if(!(tab[i].perm&0200))
					goto Eperm;
				break;
			case ORDWR:
				if((tab[i].perm&0600) != 0600)
					goto Eperm;
				break;
			case OEXEC:
			Eperm:
				respond(r, "permission denied");
				return;
			}
			if(tab[i].open)
				tab[i].open(r);
			else
				respond(r, nil);
			return;
		}

	/* directory */
	if(r->ifcall.mode != OREAD)
		respond(r, "permission denied");
	else
		respond(r, nil);
}

void
fsdestroyfid(Fid *fid)
{
	int i, q;

	q = fid->qid.path;
	for(i=0; i<nelem(tab); i++)
		if(tab[i].qid == q){
			if(tab[i].close)
				tab[i].close(fid);
			break;
		}
}

Srv fs = {
	.attach=	fsattach,
	.open=	fsopen,
	.read=	fsread,
	.write=	fswrite,
	.stat=	fsstat,
	.walk1=	fswalk1,
	.destroyfid=	fsdestroyfid,
};

void
usage(void)
{
	fprint(2, "usage: aux/vmware [-s srvname] [-m mtpt]\n");
	exits("usage");
}

void
nohwaccel(void)
{
	int fd;
	
	if((fd = open("#v/vgactl", OWRITE)) < 0)
		return;
	fprint(fd, "hwaccel off");
}

void
main(int argc, char **argv)
{
	quotefmtinstall();

	time0 = time(0);

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	if(setjmp(backdoorjmp))
		sysfatal("VMware backdoor call failed");

	if(getversion() < 0)
		sysfatal("no vmware");
	nohwaccel();
	postmountsrv(&fs, srvname, mtpt, MREPL);
}
