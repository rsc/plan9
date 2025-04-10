#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include "dat.h"
#include "fns.h"

Buffer	snarfbuf;

/*
 * These functions get called as:
 *
 *	fn(et, t, argt, flag1, flag1, flag2, s, n);
 *
 * Where the arguments are:
 *
 *	et: the Text* in which the executing event (click) occurred
 *	t: the Text* containing the current selection (Edit, Cut, Snarf, Paste)
 *	argt: the Text* containing the argument for a 2-1 click.
 *	e->flag1: from Exectab entry
 * 	e->flag2: from Exectab entry
 *	s: the command line remainder (e.g., "x" if executing "Dump x")
 *	n: length of s  (s is *not* NUL-terminated)
 */

void	del(Text*, Text*, Text*, int, int, Rune*, int);
void	delcol(Text*, Text*, Text*, int, int, Rune*, int);
void	dump(Text*, Text*, Text*, int, int, Rune*, int);
void	edit(Text*, Text*, Text*, int, int, Rune*, int);
void	exit(Text*, Text*, Text*, int, int, Rune*, int);
void	fontx(Text*, Text*, Text*, int, int, Rune*, int);
void	get(Text*, Text*, Text*, int, int, Rune*, int);
void	id(Text*, Text*, Text*, int, int, Rune*, int);
void	incl(Text*, Text*, Text*, int, int, Rune*, int);
void	indent(Text*, Text*, Text*, int, int, Rune*, int);
void	kill(Text*, Text*, Text*, int, int, Rune*, int);
void	local(Text*, Text*, Text*, int, int, Rune*, int);
void	look(Text*, Text*, Text*, int, int, Rune*, int);
void	newcol(Text*, Text*, Text*, int, int, Rune*, int);
void	paste(Text*, Text*, Text*, int, int, Rune*, int);
void	put(Text*, Text*, Text*, int, int, Rune*, int);
void	putall(Text*, Text*, Text*, int, int, Rune*, int);
void	sendx(Text*, Text*, Text*, int, int, Rune*, int);
void	sort(Text*, Text*, Text*, int, int, Rune*, int);
void	tab(Text*, Text*, Text*, int, int, Rune*, int);
void	zeroxx(Text*, Text*, Text*, int, int, Rune*, int);

typedef struct Exectab Exectab;
struct Exectab
{
	Rune	*name;
	void	(*fn)(Text*, Text*, Text*, int, int, Rune*, int);
	int		mark;
	int		flag1;
	int		flag2;
};

Exectab exectab[] = {
	{ L"Cut",		cut,		TRUE,	TRUE,	TRUE	},
	{ L"Del",		del,		FALSE,	FALSE,	XXX		},
	{ L"Delcol",	delcol,	FALSE,	XXX,		XXX		},
	{ L"Delete",	del,		FALSE,	TRUE,	XXX		},
	{ L"Dump",	dump,	FALSE,	TRUE,	XXX		},
	{ L"Edit",		edit,		FALSE,	XXX,		XXX		},
	{ L"Exit",		exit,		FALSE,	XXX,		XXX		},
	{ L"Font",		fontx,	FALSE,	XXX,		XXX		},
	{ L"Get",		get,		FALSE,	TRUE,	XXX		},
	{ L"ID",		id,		FALSE,	XXX,		XXX		},
	{ L"Incl",		incl,		FALSE,	XXX,		XXX		},
	{ L"Indent",	indent,	FALSE,	XXX,		XXX		},
	{ L"Kill",		kill,		FALSE,	XXX,		XXX		},
	{ L"Load",		dump,	FALSE,	FALSE,	XXX		},
	{ L"Local",		local,	FALSE,	XXX,		XXX		},
	{ L"Look",		look,		FALSE,	XXX,		XXX		},
	{ L"New",		new,		FALSE,	XXX,		XXX		},
	{ L"Newcol",	newcol,	FALSE,	XXX,		XXX		},
	{ L"Paste",		paste,	TRUE,	TRUE,	XXX		},
	{ L"Put",		put,		FALSE,	XXX,		XXX		},
	{ L"Putall",		putall,	FALSE,	XXX,		XXX		},
	{ L"Redo",		undo,	FALSE,	FALSE,	XXX		},
	{ L"Send",		sendx,	TRUE,	XXX,		XXX		},
	{ L"Snarf",		cut,		FALSE,	TRUE,	FALSE	},
	{ L"Sort",		sort,		FALSE,	XXX,		XXX		},
	{ L"Tab",		tab,		FALSE,	XXX,		XXX		},
	{ L"Undo",		undo,	FALSE,	TRUE,	XXX		},
	{ L"Zerox",	zeroxx,	FALSE,	XXX,		XXX		},
	{ nil, 			nil,		0,		0,		0		},
};

Exectab*
lookup(Rune *r, int n)
{
	Exectab *e;
	int nr;

	r = skipbl(r, n, &n);
	if(n == 0)
		return nil;
	findbl(r, n, &nr);
	nr = n-nr;
	for(e=exectab; e->name; e++)
		if(runeeq(r, nr, e->name, runestrlen(e->name)) == TRUE)
			return e;
	return nil;
}

int
isexecc(int c)
{
	if(isfilec(c))
		return 1;
	return c=='<' || c=='|' || c=='>';
}

void
execute(Text *t, uint aq0, uint aq1, int external, Text *argt)
{
	uint q0, q1;
	Rune *r, *s;
	char *b, *a, *aa;
	Exectab *e;
	int c, n, f;
	Runestr dir;

	q0 = aq0;
	q1 = aq1;
	if(q1 == q0){	/* expand to find word (actually file name) */
		/* if in selection, choose selection */
		if(t->q1>t->q0 && t->q0<=q0 && q0<=t->q1){
			q0 = t->q0;
			q1 = t->q1;
		}else{
			while(q1<t->file->nc && isexecc(c=textreadc(t, q1)) && c!=':')
				q1++;
			while(q0>0 && isexecc(c=textreadc(t, q0-1)) && c!=':')
				q0--;
			if(q1 == q0)
				return;
		}
	}
	r = runemalloc(q1-q0);
	bufread(t->file, q0, r, q1-q0);
	e = lookup(r, q1-q0);
	if(!external && t->w!=nil && t->w->nopen[QWevent]>0){
		f = 0;
		if(e)
			f |= 1;
		if(q0!=aq0 || q1!=aq1){
			bufread(t->file, aq0, r, aq1-aq0);
			f |= 2;
		}
		aa = getbytearg(argt, TRUE, TRUE, &a);
		if(a){
			if(strlen(a) > EVENTSIZE){	/* too big; too bad */
				free(aa);
				free(a);
				warning(nil, "`argument string too long\n");
				return;
			}
			f |= 8;
		}
		c = 'x';
		if(t->what == Body)
			c = 'X';
		n = aq1-aq0;
		if(n <= EVENTSIZE)
			winevent(t->w, "%c%d %d %d %d %.*S\n", c, aq0, aq1, f, n, n, r);
		else
			winevent(t->w, "%c%d %d %d 0 \n", c, aq0, aq1, f);
		if(q0!=aq0 || q1!=aq1){
			n = q1-q0;
			bufread(t->file, q0, r, n);
			if(n <= EVENTSIZE)
				winevent(t->w, "%c%d %d 0 %d %.*S\n", c, q0, q1, n, n, r);
			else
				winevent(t->w, "%c%d %d 0 0 \n", c, q0, q1);
		}
		if(a){
			winevent(t->w, "%c0 0 0 %d %s\n", c, utflen(a), a);
			if(aa)
				winevent(t->w, "%c0 0 0 %d %s\n", c, utflen(aa), aa);
			else
				winevent(t->w, "%c0 0 0 0 \n", c);
		}
		free(r);
		free(aa);
		free(a);
		return;
	}
	if(e){
		if(e->mark && seltext!=nil)
		if(seltext->what == Body){
			seq++;
			filemark(seltext->w->body.file);
		}
		s = skipbl(r, q1-q0, &n);
		s = findbl(s, n, &n);
		s = skipbl(s, n, &n);
		(*e->fn)(t, seltext, argt, e->flag1, e->flag2, s, n);
		free(r);
		return;
	}

	b = runetobyte(r, q1-q0);
	free(r);
	dir = dirname(t, nil, 0);
	if(dir.nr==1 && dir.r[0]=='.'){	/* sigh */
		free(dir.r);
		dir.r = nil;
		dir.nr = 0;
	}
	aa = getbytearg(argt, TRUE, TRUE, &a);
	if(t->w)
		incref(t->w);
	run(t->w, b, dir.r, dir.nr, TRUE, aa, a, FALSE);
}

char*
printarg(Text *argt, uint q0, uint q1)
{
	char *buf;

	if(argt->what!=Body || argt->file->name==nil)
		return nil;
	buf = emalloc(argt->file->nname+32);
	if(q0 == q1)
		sprint(buf, "%.*S:#%d", argt->file->nname, argt->file->name, q0);
	else
		sprint(buf, "%.*S:#%d,#%d", argt->file->nname, argt->file->name, q0, q1);
	return buf;
}

char*
getarg(Text *argt, int doaddr, int dofile, Rune **rp, int *nrp)
{
	int n;
	Expand e;
	char *a;

	*rp = nil;
	*nrp = 0;
	if(argt == nil)
		return nil;
	a = nil;
	textcommit(argt, TRUE);
	if(expand(argt, argt->q0, argt->q1, &e)){
		free(e.bname);
		if(e.nname && dofile){
			e.name = runerealloc(e.name, e.nname+1);
			if(doaddr)
				a = printarg(argt, e.q0, e.q1);
			*rp = e.name;
			*nrp = e.nname;
			return a;
		}
		free(e.name);
	}else{
		e.q0 = argt->q0;
		e.q1 = argt->q1;
	}
	n = e.q1 - e.q0;
	*rp = runemalloc(n+1);
	bufread(argt->file, e.q0, *rp, n);
	if(doaddr)
		a = printarg(argt, e.q0, e.q1);
	*nrp = n;
	return a;
}

char*
getbytearg(Text *argt, int doaddr, int dofile, char **bp)
{
	Rune *r;
	int n;
	char *aa;

	*bp = nil;
	aa = getarg(argt, doaddr, dofile, &r, &n);
	if(r == nil)
		return nil;
	*bp = runetobyte(r, n);
	free(r);
	return aa;
}

void
newcol(Text *et, Text*, Text*, int, int, Rune*, int)
{
	Column *c;

	c = rowadd(et->row, nil, -1);
	if(c)
		winsettag(coladd(c, nil, nil, -1));
}

void
delcol(Text *et, Text*, Text*, int, int, Rune*, int)
{
	int i;
	Column *c;
	Window *w;

	c = et->col;
	if(c==nil || colclean(c)==0)
		return;
	for(i=0; i<c->nw; i++){
		w = c->w[i];
		if(w->nopen[QWevent]+w->nopen[QWaddr]+w->nopen[QWdata]+w->nopen[QWxdata] > 0){
			warning(nil, "can't delete column; %.*S is running an external command\n", w->body.file->nname, w->body.file->name);
			return;
		}
	}
	rowclose(et->col->row, et->col, TRUE);
}

void
del(Text *et, Text*, Text *argt, int flag1, int, Rune *arg, int narg)
{
	Window *w;
	char *name, *p;
	Plumbmsg *pm;

	if(et->col==nil || et->w == nil)
		return;
	if(flag1 || et->w->body.file->ntext>1 || winclean(et->w, FALSE)){
		w = et->w;
		name = getname(&w->body, argt, arg, narg, TRUE);
		if(name && plumbsendfd >= 0){
			pm = emalloc(sizeof(Plumbmsg));
			pm->src = estrdup("acme");
			pm->dst = estrdup("close");
			pm->wdir = estrdup(name);
			if(p = strrchr(pm->wdir, '/'))
				*p = '\0';
			pm->type = estrdup("text");
			pm->attr = nil;
			pm->data = estrdup(name);
			pm->ndata = strlen(pm->data);
			if(pm->ndata < messagesize-1024)
				plumbsend(plumbsendfd, pm);
			else
				plumbfree(pm);
		}
		colclose(et->col, et->w, TRUE);
	}
}

void
sort(Text *et, Text*, Text*, int, int, Rune*, int)
{
	if(et->col)
		colsort(et->col);
}

uint
seqof(Window *w, int isundo)
{
	/* if it's undo, see who changed with us */
	if(isundo)
		return w->body.file->seq;
	/* if it's redo, see who we'll be sync'ed up with */
	return fileredoseq(w->body.file);
}

void
undo(Text *et, Text*, Text*, int flag1, int, Rune*, int)
{
	int i, j;
	Column *c;
	Window *w;
	uint seq;

	if(et==nil || et->w== nil)
		return;
	seq = seqof(et->w, flag1);
	if(seq == 0){
		/* nothing to undo */
		return;
	}
	/*
	 * Undo the executing window first. Its display will update. other windows
	 * in the same file will not call show() and jump to a different location in the file.
	 * Simultaneous changes to other files will be chaotic, however.
	 */
	winundo(et->w, flag1);
	for(i=0; i<row.ncol; i++){
		c = row.col[i];
		for(j=0; j<c->nw; j++){
			w = c->w[j];
			if(w == et->w)
				continue;
			if(seqof(w, flag1) == seq)
				winundo(w, flag1);
		}
	}
}

char*
getname(Text *t, Text *argt, Rune *arg, int narg, int isput)
{
	char *s;
	Rune *r;
	int i, n, promote;
	Runestr dir;

	getarg(argt, FALSE, TRUE, &r, &n);
	promote = FALSE;
	if(r == nil)
		promote = TRUE;
	else if(isput){
		/* if are doing a Put, want to synthesize name even for non-existent file */
		/* best guess is that file name doesn't contain a slash */
		promote = TRUE;
		for(i=0; i<n; i++)
			if(r[i] == '/'){
				promote = FALSE;
				break;
			}
		if(promote){
			t = argt;
			arg = r;
			narg = n;
		}
	}
	if(promote){
		n = narg;
		if(n <= 0){
			s = runetobyte(t->file->name, t->file->nname);
			return s;
		}
		/* prefix with directory name if necessary */
		dir.r = nil;
		dir.nr = 0;
		if(n>0 && arg[0]!='/'){
			dir = dirname(t, nil, 0);
			if(dir.nr==1 && dir.r[0]=='.'){	/* sigh */
				free(dir.r);
				dir.r = nil;
				dir.nr = 0;
			}
		}
		if(dir.r){
			r = runemalloc(dir.nr+n+1);
			runemove(r, dir.r, dir.nr);
			free(dir.r);
			if(dir.nr>0 && r[dir.nr]!='/' && n>0 && arg[0]!='/')
				r[dir.nr++] = '/';
			runemove(r+dir.nr, arg, n);
			n += dir.nr;
		}else{
			r = runemalloc(n+1);
			runemove(r, arg, n);
		}
	}
	s = runetobyte(r, n);
	free(r);
	if(strlen(s) == 0){
		free(s);
		s = nil;
	}
	return s;
}

void
zeroxx(Text *et, Text *t, Text*, int, int, Rune*, int)
{
	Window *nw;
	int c, locked;

	locked = FALSE;
	if(t!=nil && t->w!=nil && t->w!=et->w){
		locked = TRUE;
		c = 'M';
		if(et->w)
			c = et->w->owner;
		winlock(t->w, c);
	}
	if(t == nil)
		t = et;
	if(t==nil || t->w==nil)
		return;
	t = &t->w->body;
	if(t->w->isdir)
		warning(nil, "%.*S is a directory; Zerox illegal\n", t->file->nname, t->file->name);
	else{
		nw = coladd(t->w->col, nil, t->w, -1);
		/* ugly: fix locks so w->unlock works */
		winlock1(nw, t->w->owner);
	}
	if(locked)
		winunlock(t->w);
}

void
get(Text *et, Text *t, Text *argt, int flag1, int, Rune *arg, int narg)
{
	char *name;
	Rune *r;
	int i, n, dirty, samename, isdir;
	Window *w;
	Text *u;
	Dir *d;

	if(flag1)
		if(et==nil || et->w==nil)
			return;
	if(!et->w->isdir && (et->w->body.file->nc>0 && !winclean(et->w, TRUE)))
		return;
	w = et->w;
	t = &w->body;
	name = getname(t, argt, arg, narg, FALSE);
	if(name == nil){
		warning(nil, "no file name\n");
		return;
	}
	if(t->file->ntext>1){
		d = dirstat(name);
		isdir = (d!=nil && (d->qid.type & QTDIR));
		free(d);
		if(isdir){
			warning(nil, "%s is a directory; can't read with multiple windows on it\n", name);
			return;
		}
	}
	r = bytetorune(name, &n);
	for(i=0; i<t->file->ntext; i++){
		u = t->file->text[i];
		/* second and subsequent calls with zero an already empty buffer, but OK */
		textreset(u);
		windirfree(u->w);
	}
	samename = runeeq(r, n, t->file->name, t->file->nname);
	textload(t, 0, name, samename);
	if(samename){
		t->file->mod = FALSE;
		dirty = FALSE;
	}else{
		t->file->mod = TRUE;
		dirty = TRUE;
	}
	for(i=0; i<t->file->ntext; i++)
		t->file->text[i]->w->dirty = dirty;
	free(name);
	free(r);
	winsettag(w);
	t->file->unread = FALSE;
	for(i=0; i<t->file->ntext; i++){
		u = t->file->text[i];
		textsetselect(&u->w->tag, u->w->tag.file->nc, u->w->tag.file->nc);
		textscrdraw(u);
	}
}

void
putfile(File *f, int q0, int q1, Rune *namer, int nname)
{
	uint n, m;
	Rune *r;
	char *s, *name, *p;
	int i, fd, q;
	Dir *d, *d1;
	Window *w;
	Plumbmsg *pm;
	int isapp;

	w = f->curtext->w;
	name = runetobyte(namer, nname);
	d = dirstat(name);
	if(d!=nil && runeeq(namer, nname, f->name, f->nname)){
		/* f->mtime+1 because when talking over NFS it's often off by a second */
		if(f->dev!=d->dev || f->qidpath!=d->qid.path || f->mtime+1<d->mtime){
			f->dev = d->dev;
			f->qidpath = d->qid.path;
			f->mtime = d->mtime;
			if(f->unread)
				warning(nil, "%s not written; file already exists\n", name);
			else
				warning(nil, "%s modified%s%s since last read\n", name, d->muid[0]?" by ":"", d->muid);
			goto Rescue1;
		}
	}
	fd = create(name, OWRITE, 0666);
	if(fd < 0){
		warning(nil, "can't create file %s: %r\n", name);
		goto Rescue1;
	}
	r = fbufalloc();
	s = fbufalloc();
	free(d);
	d = dirfstat(fd);
	isapp = (d!=nil && d->length>0 && (d->qid.type&QTAPPEND));
	if(isapp){
		warning(nil, "%s not written; file is append only\n", name);
		goto Rescue2;
	}

	for(q=q0; q<q1; q+=n){
		n = q1 - q;
		if(n > BUFSIZE/UTFmax)
			n = BUFSIZE/UTFmax;
		bufread(f, q, r, n);
		m = snprint(s, BUFSIZE+1, "%.*S", n, r);
		if(write(fd, s, m) != m){
			warning(nil, "can't write file %s: %r\n", name);
			goto Rescue2;
		}
	}
	if(runeeq(namer, nname, f->name, f->nname)){
		if(q0!=0 || q1!=f->nc){
			f->mod = TRUE;
			w->dirty = TRUE;
			f->unread = TRUE;
		}else{
			d1 = dirfstat(fd);
			if(d1 != nil){
				free(d);
				d = d1;
			}
			f->qidpath = d->qid.path;
			f->dev = d->dev;
			f->mtime = d->mtime;
			f->mod = FALSE;
			w->dirty = FALSE;
			f->unread = FALSE;
		}
		for(i=0; i<f->ntext; i++){
			f->text[i]->w->putseq = f->seq;
			f->text[i]->w->dirty = w->dirty;
		}
	}
	if(plumbsendfd >= 0){
		pm = emalloc(sizeof(Plumbmsg));
		pm->src = estrdup("acme");
		pm->dst = estrdup("put");
		pm->wdir = estrdup(name);
		if(p = strrchr(pm->wdir, '/'))
			*p = '\0';
		pm->type = estrdup("text");
		pm->attr = nil;
		pm->data = estrdup(name);
		pm->ndata = strlen(pm->data);
		if(pm->ndata < messagesize-1024)
			plumbsend(plumbsendfd, pm);
		else
			plumbfree(pm);
	}
	fbuffree(s);
	fbuffree(r);
	free(d);
	free(namer);
	free(name);
	close(fd);
	winsettag(w);
	return;

    Rescue2:
	fbuffree(s);
	fbuffree(r);
	close(fd);
	/* fall through */

    Rescue1:
	free(d);
	free(namer);
	free(name);
}

void
put(Text *et, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	int nname;
	Rune  *namer;
	Window *w;
	File *f;
	char *name;

	if(et==nil || et->w==nil || et->w->isdir)
		return;
	w = et->w;
	f = w->body.file;
	name = getname(&w->body, argt, arg, narg, TRUE);
	if(name == nil){
		warning(nil, "no file name\n");
		return;
	}
	namer = bytetorune(name, &nname);
	putfile(f, 0, f->nc, namer, nname);
	free(name);
}

void
dump(Text *, Text *, Text *argt, int isdump, int, Rune *arg, int narg)
{
	char *name;

	if(narg)
		name = runetobyte(arg, narg);
	else
		getbytearg(argt, FALSE, TRUE, &name);
	if(isdump)
		rowdump(&row, name);
	else
		rowload(&row, name, FALSE);
	free(name);
}

void
cut(Text *et, Text *t, Text*, int dosnarf, int docut, Rune*, int)
{
	uint q0, q1, n, locked, c;
	Rune *r;

	/*
	 * if not executing a mouse chord (et != t) and snarfing (dosnarf)
	 * and executed Cut or Snarf in window tag (et->w != nil),
	 * then use the window body selection or the tag selection
	 * or do nothing at all.
	 */
	if(et!=t && dosnarf && et->w!=nil){
		if(et->w->body.q1>et->w->body.q0){
			t = &et->w->body;
			if(docut)
				filemark(t->file);	/* seq has been incremented by execute */
		}else if(et->w->tag.q1>et->w->tag.q0)
			t = &et->w->tag;
		else
			t = nil;
	}
	if(t == nil)	/* no selection */
		return;

	locked = FALSE;
	if(t->w!=nil && et->w!=t->w){
		locked = TRUE;
		c = 'M';
		if(et->w)
			c = et->w->owner;
		winlock(t->w, c);
	}
	if(t->q0 == t->q1){
		if(locked)
			winunlock(t->w);
		return;
	}
	if(dosnarf){
		q0 = t->q0;
		q1 = t->q1;
		bufdelete(&snarfbuf, 0, snarfbuf.nc);
		r = fbufalloc();
		while(q0 < q1){
			n = q1 - q0;
			if(n > RBUFSIZE)
				n = RBUFSIZE;
			bufread(t->file, q0, r, n);
			bufinsert(&snarfbuf, snarfbuf.nc, r, n);
			q0 += n;
		}
		fbuffree(r);
		putsnarf();
	}
	if(docut){
		textdelete(t, t->q0, t->q1, TRUE);
		textsetselect(t, t->q0, t->q0);
		if(t->w){
			textscrdraw(t);
			winsettag(t->w);
		}
	}else if(dosnarf)	/* Snarf command */
		argtext = t;
	if(locked)
		winunlock(t->w);
}

void
paste(Text *et, Text *t, Text*, int selectall, int tobody, Rune*, int)
{
	int c;
	uint q, q0, q1, n;
	Rune *r;

	/* if(tobody), use body of executing window  (Paste or Send command) */
	if(tobody && et!=nil && et->w!=nil){
		t = &et->w->body;
		filemark(t->file);	/* seq has been incremented by execute */
	}
	if(t == nil)
		return;

	getsnarf();
	if(t==nil || snarfbuf.nc==0)
		return;
	if(t->w!=nil && et->w!=t->w){
		c = 'M';
		if(et->w)
			c = et->w->owner;
		winlock(t->w, c);
	}
	cut(t, t, nil, FALSE, TRUE, nil, 0);
	q = 0;
	q0 = t->q0;
	q1 = t->q0+snarfbuf.nc;
	r = fbufalloc();
	while(q0 < q1){
		n = q1 - q0;
		if(n > RBUFSIZE)
			n = RBUFSIZE;
		if(r == nil)
			r = runemalloc(n);
		bufread(&snarfbuf, q, r, n);
		textinsert(t, q0, r, n, TRUE);
		q += n;
		q0 += n;
	}
	fbuffree(r);
	if(selectall)
		textsetselect(t, t->q0, q1);
	else
		textsetselect(t, q1, q1);
	if(t->w){
		textscrdraw(t);
		winsettag(t->w);
	}
	if(t->w!=nil && et->w!=t->w)
		winunlock(t->w);
}

void
look(Text *et, Text *t, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *r;
	int n;

	if(et && et->w){
		t = &et->w->body;
		if(narg > 0){
			search(t, arg, narg);
			return;
		}
		getarg(argt, FALSE, FALSE, &r, &n);
		if(r == nil){
			n = t->q1-t->q0;
			r = runemalloc(n);
			bufread(t->file, t->q0, r, n);
		}
		search(t, r, n);
		free(r);
	}
}

void
sendx(Text *et, Text *t, Text*, int, int, Rune*, int)
{
	if(et->w==nil)
		return;
	t = &et->w->body;
	if(t->q0 != t->q1)
		cut(t, t, nil, TRUE, FALSE, nil, 0);
	textsetselect(t, t->file->nc, t->file->nc);
	paste(t, t, nil, TRUE, TRUE, nil, 0);
	if(textreadc(t, t->file->nc-1) != '\n'){
		textinsert(t, t->file->nc, L"\n", 1, TRUE);
		textsetselect(t, t->file->nc, t->file->nc);
	}
}

void
edit(Text *et, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *r;
	int len;

	if(et == nil)
		return;
	getarg(argt, FALSE, TRUE, &r, &len);
	seq++;
	if(r != nil){
		editcmd(et, r, len);
		free(r);
	}else
		editcmd(et, arg, narg);
}

void
exit(Text*, Text*, Text*, int, int, Rune*, int)
{
	if(rowclean(&row)){
		sendul(cexit, 0);
		threadexits(nil);
	}
}

void
putall(Text*, Text*, Text*, int, int, Rune*, int)
{
	int i, j, e;
	Window *w;
	Column *c;
	char *a;

	for(i=0; i<row.ncol; i++){
		c = row.col[i];
		for(j=0; j<c->nw; j++){
			w = c->w[j];
			if(w->isscratch || w->isdir || w->body.file->nname==0)
				continue;
			if(w->nopen[QWevent] > 0)
				continue;
			a = runetobyte(w->body.file->name, w->body.file->nname);
			e = access(a, 0);
			if(w->body.file->mod || w->body.ncache)
				if(e < 0)
					warning(nil, "no auto-Put of %s: %r\n", a);
				else{
					wincommit(w, &w->body);
					put(&w->body, nil, nil, XXX, XXX, nil, 0);
				}
			free(a);
		}
	}
}


void
id(Text *et, Text*, Text*, int, int, Rune*, int)
{
	if(et && et->w)
		warning(nil, "/mnt/acme/%d/\n", et->w->id);
}

void
local(Text *et, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	char *a, *aa;
	Runestr dir;

	aa = getbytearg(argt, TRUE, TRUE, &a);

	dir = dirname(et, nil, 0);
	if(dir.nr==1 && dir.r[0]=='.'){	/* sigh */
		free(dir.r);
		dir.r = nil;
		dir.nr = 0;
	}
	run(nil, runetobyte(arg, narg), dir.r, dir.nr, FALSE, aa, a, FALSE);
}

void
kill(Text*, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *a, *cmd, *r;
	int na;

	getarg(argt, FALSE, FALSE, &r, &na);
	if(r)
		kill(nil, nil, nil, 0, 0, r, na);
	/* loop condition: *arg is not a blank */
	for(;;){
		a = findbl(arg, narg, &na);
		if(a == arg)
			break;
		cmd = runemalloc(narg-na+1);
		runemove(cmd, arg, narg-na);
		sendp(ckill, cmd);
		arg = skipbl(a, na, &narg);
	}
}

void
fontx(Text *et, Text *t, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *a, *r, *flag, *file;
	int na, nf;
	char *aa;
	Reffont *newfont;
	Dirlist *dp;
	int i, fix;

	if(et==nil || et->w==nil)
		return;
	t = &et->w->body;
	flag = nil;
	file = nil;
	/* loop condition: *arg is not a blank */
	nf = 0;
	for(;;){
		a = findbl(arg, narg, &na);
		if(a == arg)
			break;
		r = runemalloc(narg-na+1);
		runemove(r, arg, narg-na);
		if(runeeq(r, narg-na, L"fix", 3) || runeeq(r, narg-na, L"var", 3)){
			free(flag);
			flag = r;
		}else{
			free(file);
			file = r;
			nf = narg-na;
		}
		arg = skipbl(a, na, &narg);
	}
	getarg(argt, FALSE, TRUE, &r, &na);
	if(r)
		if(runeeq(r, na, L"fix", 3) || runeeq(r, na, L"var", 3)){
			free(flag);
			flag = r;
		}else{
			free(file);
			file = r;
			nf = na;
		}
	fix = 1;
	if(flag)
		fix = runeeq(flag, runestrlen(flag), L"fix", 3);
	else if(file == nil){
		newfont = rfget(FALSE, FALSE, FALSE, nil);
		if(newfont)
			fix = strcmp(newfont->f->name, t->font->name)==0;
	}
	if(file){
		aa = runetobyte(file, nf);
		newfont = rfget(fix, flag!=nil, FALSE, aa);
		free(aa);
	}else
		newfont = rfget(fix, FALSE, FALSE, nil);
	if(newfont){
		draw(screen, t->w->r, textcols[BACK], nil, ZP);
		rfclose(t->reffont);
		t->reffont = newfont;
		t->font = newfont->f;
		frinittick(t);
		if(t->w->isdir){
			t->all.min.x++;	/* force recolumnation; disgusting! */
			for(i=0; i<t->w->ndl; i++){
				dp = t->w->dlp[i];
				aa = runetobyte(dp->r, dp->nr);
				dp->wid = stringwidth(newfont->f, aa);
				free(aa);
			}
		}
		/* avoid shrinking of window due to quantization */
		colgrow(t->w->col, t->w, -1);
	}
	free(file);
	free(flag);
}

void
incl(Text *et, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *a, *r;
	Window *w;
	int na, n, len;

	if(et==nil || et->w==nil)
		return;
	w = et->w;
	n = 0;
	getarg(argt, FALSE, TRUE, &r, &len);
	if(r){
		n++;
		winaddincl(w, r, len);
	}
	/* loop condition: *arg is not a blank */
	for(;;){
		a = findbl(arg, narg, &na);
		if(a == arg)
			break;
		r = runemalloc(narg-na+1);
		runemove(r, arg, narg-na);
		n++;
		winaddincl(w, r, narg-na);
		arg = skipbl(a, na, &narg);
	}
	if(n==0 && w->nincl){
		for(n=w->nincl; --n>=0; )
			warning(nil, "%S ", w->incl[n]);
		warning(nil, "\n");
	}
}

enum {
	IGlobal = -2,
	IError = -1,
	Ion = 0,
	Ioff = 1,
};

static int
indentval(Rune *s, int n)
{
	if(n < 2)
		return IError;
	if(runestrncmp(s, L"ON", n) == 0){
		globalautoindent = TRUE;
		warning(nil, "Indent ON\n");
		return IGlobal;
	}
	if(runestrncmp(s, L"OFF", n) == 0){
		globalautoindent = FALSE;
		warning(nil, "Indent OFF\n");
		return IGlobal;
	}
	return runestrncmp(s, L"on", n) == 0;
}

static void
fixindent(Window *w, void*)
{
	w->autoindent = globalautoindent;
}

void
indent(Text *et, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *a, *r;
	Window *w;
	int na, len, autoindent;

	w = nil;
	if(et!=nil && et->w!=nil)
		w = et->w;
	autoindent = IError;
	getarg(argt, FALSE, TRUE, &r, &len);
	if(r!=nil && len>0)
		autoindent = indentval(r, len);
	else{
		a = findbl(arg, narg, &na);
		if(a != arg)
			autoindent = indentval(arg, narg-na);
	}
	if(autoindent == IGlobal)
		allwindows(fixindent, nil);
	else if(w != nil && autoindent >= 0)
		w->autoindent = autoindent;
}

void
tab(Text *et, Text*, Text *argt, int, int, Rune *arg, int narg)
{
	Rune *a, *r;
	Window *w;
	int na, len, tab;
	char *p;

	if(et==nil || et->w==nil)
		return;
	w = et->w;
	getarg(argt, FALSE, TRUE, &r, &len);
	tab = 0;
	if(r!=nil && len>0){
		p = runetobyte(r, len);
		if('0'<=p[0] && p[0]<='9')
			tab = atoi(p);
		free(p);
	}else{
		a = findbl(arg, narg, &na);
		if(a != arg){
			p = runetobyte(arg, narg-na);
			if('0'<=p[0] && p[0]<='9')
				tab = atoi(p);
			free(p);
		}
	}
	if(tab > 0){
		if(w->body.tabstop != tab){
			w->body.tabstop = tab;
			winresize(w, w->r, FALSE, TRUE);
		}
	}else
		warning(nil, "%.*S: Tab %d\n", w->body.file->nname, w->body.file->name, w->body.tabstop);
}

void
runproc(void *argvp)
{
	/* args: */
		Window *win;
		char *s;
		Rune *rdir;
		int ndir;
		int newns;
		char *argaddr;
		char *arg;
		Command *c;
		Channel *cpid;
		int iseditcmd;
	/* end of args */
	char *e, *t, *name, *filename, *dir, **av, *news;
	Rune r, **incl;
	int ac, w, inarg, i, n, fd, nincl, winid;
	int pipechar;
	char buf[512];
	static void *parg[2];
	void **argv;

	argv = argvp;
	win = argv[0];
	s = argv[1];
	rdir = argv[2];
	ndir = (uintptr)argv[3];
	newns = (uintptr)argv[4];
	argaddr = argv[5];
	arg = argv[6];
	c = argv[7];
	cpid = argv[8];
	iseditcmd = (uintptr)argv[9];
	free(argv);

	t = s;
	while(*t==' ' || *t=='\n' || *t=='\t')
		t++;
	for(e=t; *e; e++)
		if(*e==' ' || *e=='\n' || *e=='\t' )
			break;
	name = emalloc((e-t)+2);
	memmove(name, t, e-t);
	name[e-t] = 0;
	e = utfrrune(name, '/');
	if(e)
		memmove(name, e+1, strlen(e+1)+1);	/* strcpy but overlaps */
	strcat(name, " ");	/* add blank here for ease in waittask */
	c->name = bytetorune(name, &c->nname);
	free(name);
	pipechar = 0;
	if(*t=='<' || *t=='|' || *t=='>')
		pipechar = *t++;
	c->iseditcmd = iseditcmd;
	c->text = s;
	if(rdir != nil){
		dir = runetobyte(rdir, ndir);
		chdir(dir);	/* ignore error: probably app. window */
		free(dir);
	}
	if(newns){
		nincl = 0;
		incl = nil;
		if(win){
			filename = smprint("%.*S", win->body.file->nname, win->body.file->name);
			nincl = win->nincl;
			if(nincl > 0){
				incl = emalloc(nincl*sizeof(Rune*));
				for(i=0; i<nincl; i++){
					n = runestrlen(win->incl[i]);
					incl[i] = runemalloc(n+1);
					runemove(incl[i], win->incl[i], n);
				}
			}
			winid = win->id;
		}else{
			filename = nil;
			winid = 0;
			if(activewin)
				winid = activewin->id;
		}
		rfork(RFNAMEG|RFENVG|RFFDG|RFNOTEG);
		sprint(buf, "%d", winid);
		putenv("winid", buf);

		if(filename){
			putenv("%", filename);
			free(filename);
		}
		c->md = fsysmount(rdir, ndir, incl, nincl);
		if(c->md == nil){
			fprint(2, "child: can't mount /dev/cons: %r\n");
			threadexits("mount");
		}
		close(0);
		if(winid>0 && (pipechar=='|' || pipechar=='>')){
			sprint(buf, "/mnt/acme/%d/rdsel", winid);
			open(buf, OREAD);
		}else
			open("/dev/null", OREAD);
		close(1);
		if((winid>0 || iseditcmd) && (pipechar=='|' || pipechar=='<')){
			if(iseditcmd){
				if(winid > 0)
					sprint(buf, "/mnt/acme/%d/editout", winid);
				else
					sprint(buf, "/mnt/acme/editout");
			}else
				sprint(buf, "/mnt/acme/%d/wrsel", winid);
			open(buf, OWRITE);
			close(2);
			open("/dev/cons", OWRITE);
		}else{
			open("/dev/cons", OWRITE);
			dup(1, 2);
		}
	}else{
		rfork(RFFDG|RFNOTEG);
		fsysclose();
		close(0);
		open("/dev/null", OREAD);
		close(1);
		open(acmeerrorfile, OWRITE);
		dup(1, 2);
	}

	if(win)
		winclose(win);

	if(argaddr)
		putenv("acmeaddr", argaddr);
	if(strlen(t) > sizeof buf-10)	/* may need to print into stack */
		goto Hard;
	inarg = FALSE;
	for(e=t; *e; e+=w){
		w = chartorune(&r, e);
		if(r==' ' || r=='\t')
			continue;
		if(r < ' ')
			goto Hard;
		if(utfrune("#;&|^$=`'{}()<>[]*?^~`", r))
			goto Hard;
		inarg = TRUE;
	}
	if(!inarg)
		goto Fail;

	ac = 0;
	av = nil;
	inarg = FALSE;
	for(e=t; *e; e+=w){
		w = chartorune(&r, e);
		if(r==' ' || r=='\t'){
			inarg = FALSE;
			*e = 0;
			continue;
		}
		if(!inarg){
			inarg = TRUE;
			av = realloc(av, (ac+1)*sizeof(char**));
			av[ac++] = e;
		}
	}
	av = realloc(av, (ac+2)*sizeof(char**));
	av[ac++] = arg;
	av[ac] = nil;
	c->av = av;
	procexec(cpid, av[0], av);
	e = av[0];
	if(e[0]=='/' || (e[0]=='.' && e[1]=='/'))
		goto Fail;
	if(cputype){
		sprint(buf, "%s/%s", cputype, av[0]);
		procexec(cpid, buf, av);
	}
	sprint(buf, "/bin/%s", av[0]);
	procexec(cpid, buf, av);
	goto Fail;

Hard:

	/*
	 * ugly: set path = (. $cputype /bin)
	 * should honor $path if unusual.
	 */
	if(cputype){
		n = 0;
		memmove(buf+n, ".", 2);
		n += 2;
		i = strlen(cputype)+1;
		memmove(buf+n, cputype, i);
		n += i;
		memmove(buf+n, "/bin", 5);
		n += 5;
		fd = create("/env/path", OWRITE, 0666);
		write(fd, buf, n);
		close(fd);
	}

	if(arg){
		news = emalloc(strlen(t) + 1 + 1 + strlen(arg) + 1 + 1);
		if(news){
			sprint(news, "%s '%s'", t, arg);	/* BUG: what if quote in arg? */
			free(s);
			t = news;
			c->text = news;
		}
	}
	procexecl(cpid, "/bin/rc", "rc", "-c", t, nil);

   Fail:
	/* procexec hasn't happened, so send a zero */
	sendul(cpid, 0);
	threadexits(nil);
}

void
runwaittask(void *v)
{
	Command *c;
	Channel *cpid;
	void **a;

	threadsetname("runwaittask");
	a = v;
	c = a[0];
	cpid = a[1];
	free(a);
	do
		c->pid = recvul(cpid);
	while(c->pid == ~0);
	free(c->av);
	if(c->pid != 0)	/* successful exec */
		sendp(ccommand, c);
	else{
		if(c->iseditcmd)
			sendul(cedit, 0);
		free(c->name);
		free(c->text);
		free(c);
	}
	chanfree(cpid);
}

void
run(Window *win, char *s, Rune *rdir, int ndir, int newns, char *argaddr, char *xarg, int iseditcmd)
{
	void **arg;
	Command *c;
	Channel *cpid;

	if(s == nil)
		return;

	arg = emalloc(10*sizeof(void*));
	c = emalloc(sizeof *c);
	cpid = chancreate(sizeof(ulong), 0);
	arg[0] = win;
	arg[1] = s;
	arg[2] = rdir;
	arg[3] = (void*)ndir;
	arg[4] = (void*)newns;
	arg[5] = argaddr;
	arg[6] = xarg;
	arg[7] = c;
	arg[8] = cpid;
	arg[9] = (void*)iseditcmd;
	proccreate(runproc, arg, STACK);
	/* mustn't block here because must be ready to answer mount() call in run() */
	arg = emalloc(2*sizeof(void*));
	arg[0] = c;
	arg[1] = cpid;
	threadcreate(runwaittask, arg, STACK);
}
