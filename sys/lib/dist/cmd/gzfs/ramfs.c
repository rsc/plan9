#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

static char Ebad[] = "something bad happened";
static char Enomem[] = "no memory";

int allowed = 1;

typedef struct Ramfile	Ramfile;
struct Ramfile {
	char *data;
	int ndata;
};

int
fshasperm(File *file, char *user, int access)
{
	return allowed || hasperm(file, user, access);
}

void
fsread(Req *r)
{
	Ramfile *rf;
	vlong offset;
	long count;

	rf = r->fid->file->aux;
	offset = r->ifcall.offset;
	count = r->ifcall.count;

//print("read %ld %lld\n", *count, offset);
	if(offset >= rf->ndata){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}

	if(offset+count >= rf->ndata)
		count = rf->ndata - offset;

	memmove(r->ofcall.data, rf->data+offset, count);
	r->ofcall.count = count;
	respond(r, nil);
}

void
fswrite(Req *r)
{
	void *v;
	Ramfile *rf;
	vlong offset;
	long count;

	rf = r->fid->file->aux;
	offset = r->ifcall.offset;
	count = r->ifcall.count;

	if(offset+count >= rf->ndata){
		v = realloc(rf->data, offset+count);
		if(v == nil){
			respond(r, Enomem);
			return;
		}
		rf->data = v;
		rf->ndata = offset+count;
		r->fid->file->length = rf->ndata;
	}
	memmove(rf->data+offset, r->ifcall.data, count);
	r->ofcall.count = count;
	respond(r, nil);
}

void
fscreate(Req *r)
{
	Ramfile *rf;
	File *f;

	if(f = createfile(r->fid->file, r->ifcall.name, r->fid->uid, r->ifcall.perm, nil)){
		rf = emalloc9p(sizeof *rf);
		f->aux = rf;
		r->fid->file = f;
		r->ofcall.qid = f->qid;
		respond(r, nil);
		return;
	}
	respond(r, Ebad);
}

void
fsopen(Req *r)
{
	Ramfile *rf;

	rf = r->fid->file->aux;

	if(rf && (r->ifcall.mode&OTRUNC)){
		rf->ndata = 0;
		r->fid->file->length = 0;
	}

	respond(r, nil);
}

void
fsdestroyfile(File *f)
{
	Ramfile *rf;

//fprint(2, "clunk\n");
	rf = f->aux;
	if(rf){
		free(rf->data);
		free(rf);
	}
}

void
fswstat(Req *r)
{
	File *f, *f1;
	Ramfile *rf;
	void *v;

	f = r->fid->file;
	rf = f->aux;
	if(r->d.name && r->d.name[0] && strcmp(r->d.name, f->name) != 0){
		incref(f->parent);
		f1 = walkfile(f->parent, r->d.name);
		if(f1 != nil) {
			closefile(f1);
			respond(r, "name already exists");
			return;
		}
		free(f->name);
		f->name = estrdup9p(r->d.name);
	}
	if((ulong)~r->d.mode)
		f->mode = r->d.mode;
	if((ulong)~r->d.atime)
		f->atime = r->d.atime;
	if((ulong)~r->d.mtime)
		f->mtime = r->d.mtime;
	if((vlong)~r->d.length && r->d.length != f->length){
		v = realloc(rf->data, r->d.length);
		if(v == nil){
			respond(r, Enomem);
			return;
		}
		rf->data = v;
		rf->ndata = r->d.length;
		f->length = rf->ndata;
	}
	if(r->d.uid && r->d.uid[0]){
		free(f->uid);
		f->uid = estrdup9p(r->d.uid);
	}
	if(r->d.gid && r->d.gid[0]){
		free(f->gid);
		f->gid = estrdup9p(r->d.gid);
	}
	respond(r, nil);
}

Srv fs = {
	.open=	fsopen,
	.read=	fsread,
	.write=	fswrite,
	.create=	fscreate,
	.wstat=	fswstat,
	.hasperm=	fshasperm,
};

void
ramfsusage(void)
{
	fprint(2, "usage: ramfs [-D] [-s srvname] [-m mtpt]\n");
	exits("usage");
}

void
ramfsmain(int argc, char **argv)
{
	char *addr = nil;
	char *srvname = nil;
	char *mtpt = nil;
	Qid q;

	fs.tree = alloctree(nil, nil, DMDIR|0777, fsdestroyfile);
	q = fs.tree->root->qid;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'a':
		addr = EARGF(ramfsusage());
		break;
	case 'i':
		fs.nopipe = 1;
		fs.infd = 1;
		fs.outfd = 1;
		fs.srvfd = 0;
		break;
	case 's':
		srvname = EARGF(ramfsusage());
		break;
	case 'm':
		mtpt = EARGF(ramfsusage());
		break;
	default:
		ramfsusage();
	}ARGEND;

	if(argc)
		ramfsusage();

	if(chatty9p)
		fprint(2, "ramsrv.nopipe %d srvname %s mtpt %s\n", fs.nopipe, srvname, mtpt);
	if(addr == nil && srvname == nil && mtpt == nil)
		sysfatal("must specify -a, -s, or -m option");
	if(addr)
		listensrv(&fs, addr);

	if(srvname || mtpt)
		postmountsrv(&fs, srvname, mtpt, MREPL|MCREATE);
}
