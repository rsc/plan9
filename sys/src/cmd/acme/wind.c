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

int	winid;

void
wininit(Window *w, Window *clone, Rectangle r)
{
	Rectangle r1, br;
	File *f;
	Reffont *rf;
	Rune *rp;
	int nc;

	w->tag.w = w;
	w->taglines = 1;
	w->tagexpand = TRUE;
	w->body.w = w;
	w->id = ++winid;
	incref(w);
	if(globalincref)
		incref(w);
	w->ctlfid = ~0;
	w->utflastqid = -1;
	r1 = r;

	w->tagtop = r;
	w->tagtop.max.y = r.min.y + font->height;
	r1.max.y = r1.min.y + w->taglines*font->height;

	incref(&reffont);
	f = fileaddtext(nil, &w->tag);
	textinit(&w->tag, f, r1, &reffont, tagcols);
	w->tag.what = Tag;
	/* tag is a copy of the contents, not a tracked image */
	if(clone){
		textdelete(&w->tag, 0, w->tag.file->nc, TRUE);
		nc = clone->tag.file->nc;
		rp = runemalloc(nc);
		bufread(clone->tag.file, 0, rp, nc);
		textinsert(&w->tag, 0, rp, nc, TRUE);
		free(rp);
		filereset(w->tag.file);
		textsetselect(&w->tag, nc, nc);
	}
	r1 = r;
	r1.min.y += w->taglines*font->height + 1;
	if(r1.max.y < r1.min.y)
		r1.max.y = r1.min.y;
	f = nil;
	if(clone){
		f = clone->body.file;
		w->body.org = clone->body.org;
		w->isscratch = clone->isscratch;
		rf = rfget(FALSE, FALSE, FALSE, clone->body.reffont->f->name);
	}else
		rf = rfget(FALSE, FALSE, FALSE, nil);
	f = fileaddtext(f, &w->body);
	w->body.what = Body;
	textinit(&w->body, f, r1, rf, textcols);
	r1.min.y -= 1;
	r1.max.y = r1.min.y+1;
	draw(screen, r1, tagcols[BORD], nil, ZP);
	textscrdraw(&w->body);
	w->r = r;
	br.min = w->tag.scrollr.min;
	br.max.x = br.min.x + Dx(button->r);
	br.max.y = br.min.y + Dy(button->r);
	draw(screen, br, button, nil, button->r.min);
	w->filemenu = TRUE;
	w->maxlines = w->body.maxlines;
	w->autoindent = globalautoindent;
	if(clone){
		w->dirty = clone->dirty;
		textsetselect(&w->body, clone->body.q0, clone->body.q1);
		winsettag(w);
		w->autoindent = clone->autoindent;
	}
}

/*
 * Draw the appropriate button.
 */
void
windrawbutton(Window *w)
{
	Image *b;
	Rectangle br;

	b = button;
	if(!w->isdir && !w->isscratch && (w->body.file->mod || w->body.ncache))
		b = modbutton;
	br.min = w->tag.scrollr.min;
	br.max.x = br.min.x + Dx(b->r);
	br.max.y = br.min.y + Dy(b->r);
	draw(screen, br, b, nil, b->r.min);
}

int
delrunepos(Window *w)
{
	int n;
	Rune rune;

	for(n=0; n<w->tag.file->nc; n++) {
		bufread(w->tag.file, n, &rune, 1);
		if(rune == ' ')
			break;
	}
	n += 2;
	if(n >= w->tag.file->nc)
		return -1;
	return n;
}

void
movetodel(Window *w)
{
	int n;

	n = delrunepos(w);
	if(n < 0)
		return;
	moveto(mousectl, addpt(frptofchar(&w->tag, n), Pt(4, w->tag.font->height-4)));
}

/*
 * Compute number of tag lines required
 * to display entire tag text.
 */
int
wintaglines(Window *w, Rectangle r)
{
	int n;
	Rune rune;
	Point p;

	if(!w->tagexpand && !w->showdel)
		return 1;
	w->showdel = FALSE;
	w->tag.noredraw = 1;
	textresize(&w->tag, r, TRUE);
	w->tag.noredraw = 0;
	w->tagsafe = FALSE;

	if(!w->tagexpand) {
		/* use just as many lines as needed to show the Del */
		n = delrunepos(w);
		if(n < 0)
			return 1;
		p = subpt(frptofchar(&w->tag, n), w->tag.r.min);
		return 1 + p.y / w->tag.font->height;
	}

	/* can't use more than we have */
	if(w->tag.nlines >= w->tag.maxlines)
		return w->tag.maxlines;

	/* if tag ends with \n, include empty line at end for typing */
	n = w->tag.nlines;
	if(w->tag.file->nc > 0){
		bufread(w->tag.file, w->tag.file->nc-1, &rune, 1);
		if(rune == '\n')
			n++;
	}
	if(n == 0)
		n = 1;
	return n;
}

int
winresize(Window *w, Rectangle r, int safe, int keepextra)
{
	int oy, y, mouseintag, mouseinbody;
	Point p;
	Rectangle r1;

	mouseintag = ptinrect(mouse->xy, w->tag.all);
	mouseinbody = ptinrect(mouse->xy, w->body.all);

	/* tagtop is first line of tag */
	w->tagtop = r;
	w->tagtop.max.y = r.min.y+font->height;

	r1 = r;
	r1.max.y = min(r.max.y, r1.min.y + w->taglines*font->height);

	/* If needed, recompute number of lines in tag. */
	if(!safe || !w->tagsafe || !eqrect(w->tag.all, r1)){
		w->taglines = wintaglines(w, r);
		r1.max.y = min(r.max.y, r1.min.y + w->taglines*font->height);
	}

	/* If needed, resize & redraw tag. */
	y = r1.max.y;
	if(!safe || !w->tagsafe || !eqrect(w->tag.all, r1)){
		textresize(&w->tag, r1, TRUE);
		y = w->tag.r.max.y;
		windrawbutton(w);
		w->tagsafe = TRUE;

		/* If mouse is in tag, pull up as tag closes. */
		if(mouseintag && !ptinrect(mouse->xy, w->tag.all)){
			p = mouse->xy;
			p.y = w->tag.all.max.y-3;
			moveto(mousectl, p);
		}

		/* If mouse is in body, push down as tag expands. */
		if(mouseinbody && ptinrect(mouse->xy, w->tag.all)){
			p = mouse->xy;
			p.y = w->tag.all.max.y+3;
			moveto(mousectl, p);
		}
	}

	/* If needed, resize & redraw body. */
	r1 = r;
	r1.min.y = y;
	if(!safe || !eqrect(w->body.all, r1)){
		oy = y;
		if(y+1+w->body.font->height <= r.max.y){	/* room for one line */
			r1.min.y = y;
			r1.max.y = y+1;
			draw(screen, r1, tagcols[BORD], nil, ZP);
			y++;
			r1.min.y = min(y, r.max.y);
			r1.max.y = r.max.y;
		}else{
			r1.min.y = y;
			r1.max.y = y;
		}
		y = textresize(&w->body, r1, keepextra);
		w->r = r;
		w->r.max.y = y;
		textscrdraw(&w->body);
		w->body.all.min.y = oy;
	}
	w->maxlines = min(w->body.nlines, max(w->maxlines, w->body.maxlines));
	return w->r.max.y;
}

void
winlock1(Window *w, int owner)
{
	incref(w);
	qlock(w);
	w->owner = owner;
}

void
winlock(Window *w, int owner)
{
	int i;
	File *f;

	f = w->body.file;
	for(i=0; i<f->ntext; i++)
		winlock1(f->text[i]->w, owner);
}

void
winunlock(Window *w)
{
	int i;
	File *f;

	/*
	 * subtle: loop runs backwards to avoid tripping over
	 * winclose indirectly editing f->text and freeing f
	 * on the last iteration of the loop.
	 */
	f = w->body.file;
	for(i=f->ntext-1; i>=0; i--){
		w = f->text[i]->w;
		w->owner = 0;
		qunlock(w);
		winclose(w);
	}
}

void
winmousebut(Window *w)
{
	moveto(mousectl, addpt(w->tag.scrollr.min,
		divpt(Pt(Dx(w->tag.scrollr), font->height), 2)));
}

void
windirfree(Window *w)
{
	int i;
	Dirlist *dl;

	if(w->isdir){
		for(i=0; i<w->ndl; i++){
			dl = w->dlp[i];
			free(dl->r);
			free(dl);
		}
		free(w->dlp);
	}
	w->dlp = nil;
	w->ndl = 0;
}

void
winclose(Window *w)
{
	int i;

	if(decref(w) == 0){
		windirfree(w);
		textclose(&w->tag);
		textclose(&w->body);
		if(activewin == w)
			activewin = nil;
		for(i=0; i<w->nincl; i++)
			free(w->incl[i]);
		free(w->incl);
		free(w->events);
		free(w);
	}
}

void
windelete(Window *w)
{
	Xfid *x;

	x = w->eventx;
	if(x){
		w->nevents = 0;
		free(w->events);
		w->events = nil;
		w->eventx = nil;
		sendp(x->c, nil);	/* wake him up */
	}
}

void
winundo(Window *w, int isundo)
{
	Text *body;
	int i;
	File *f;
	Window *v;

	w->utflastqid = -1;
	body = &w->body;
	fileundo(body->file, isundo, &body->q0, &body->q1);
	textshow(body, body->q0, body->q1, 1);
	f = body->file;
	for(i=0; i<f->ntext; i++){
		v = f->text[i]->w;
		v->dirty = (f->seq != v->putseq);
		if(v != w){
			v->body.q0 = v->body.p0+v->body.org;
			v->body.q1 = v->body.p1+v->body.org;
		}
	}
	winsettag(w);
}

void
winsetname(Window *w, Rune *name, int n)
{
	Text *t;
	Window *v;
	int i;

	t = &w->body;
	if(runeeq(t->file->name, t->file->nname, name, n) == TRUE)
		return;
	w->isscratch = FALSE;
	if(n>=6 && runeeq(L"/guide", 6, name+(n-6), 6))
		w->isscratch = TRUE;
	else if(n>=7 && runeeq(L"+Errors", 7, name+(n-7), 7))
		w->isscratch = TRUE;
	filesetname(t->file, name, n);
	for(i=0; i<t->file->ntext; i++){
		v = t->file->text[i]->w;
		winsettag(v);
		v->isscratch = w->isscratch;
	}
}

void
wintype(Window *w, Text *t, Rune r)
{
	int i;

	texttype(t, r);
	if(t->what == Body)
		for(i=0; i<t->file->ntext; i++)
			textscrdraw(t->file->text[i]);
	winsettag(w);
}

void
wincleartag(Window *w)
{
	int i, n;
	Rune *r;

	/* w must be committed */
	n = w->tag.file->nc;
	r = runemalloc(n);
	bufread(w->tag.file, 0, r, n);
	for(i=0; i<n; i++)
		if(r[i]==' ' || r[i]=='\t')
			break;
	for(; i<n; i++)
		if(r[i] == '|')
			break;
	if(i == n)
		return;
	i++;
	textdelete(&w->tag, i, n, TRUE);
	free(r);
	w->tag.file->mod = FALSE;
	if(w->tag.q0 > i)
		w->tag.q0 = i;
	if(w->tag.q1 > i)
		w->tag.q1 = i;
	textsetselect(&w->tag, w->tag.q0, w->tag.q1);
}

void
winsettag1(Window *w)
{
	int i, j, k, n, bar, dirty, resize;
	Rune *new, *old, *r;
	uint q0, q1;

	/* there are races that get us here with stuff in the tag cache, so we take extra care to sync it */
	if(w->tag.ncache!=0 || w->tag.file->mod)
		wincommit(w, &w->tag);	/* check file name; also guarantees we can modify tag contents */
	old = runemalloc(w->tag.file->nc+1);
	bufread(w->tag.file, 0, old, w->tag.file->nc);
	old[w->tag.file->nc] = '\0';
	for(i=0; i<w->tag.file->nc; i++)
		if(old[i]==' ' || old[i]=='\t')
			break;
	if(runeeq(old, i, w->body.file->name, w->body.file->nname) == FALSE){
		textdelete(&w->tag, 0, i, TRUE);
		textinsert(&w->tag, 0, w->body.file->name, w->body.file->nname, TRUE);
		free(old);
		old = runemalloc(w->tag.file->nc+1);
		bufread(w->tag.file, 0, old, w->tag.file->nc);
		old[w->tag.file->nc] = '\0';
	}
	new = runemalloc(w->body.file->nname+100);
	i = 0;
	runemove(new+i, w->body.file->name, w->body.file->nname);
	i += w->body.file->nname;
	runemove(new+i, L" Del Snarf", 10);
	i += 10;
	if(w->filemenu){
		if(w->body.file->delta.nc>0 || w->body.ncache){
			runemove(new+i, L" Undo", 5);
			i += 5;
		}
		if(w->body.file->epsilon.nc > 0){
			runemove(new+i, L" Redo", 5);
			i += 5;
		}
		dirty = w->body.file->nname && (w->body.ncache || w->body.file->seq!=w->putseq);
		if(!w->isdir && dirty){
			runemove(new+i, L" Put", 4);
			i += 4;
		}
	}
	if(w->isdir){
		runemove(new+i, L" Get", 4);
		i += 4;
	}
	runemove(new+i, L" |", 2);
	i += 2;
	r = runestrchr(old, '|');
	if(r)
		k = r-old+1;
	else{
		k = w->tag.file->nc;
		if(w->body.file->seq == 0){
			runemove(new+i, L" Look ", 6);
			i += 6;
		}
	}
	new[i] = 0;

	/* replace tag if the new one is different */
	resize = 0;
	if(runeeq(new, i, old, k) == FALSE){
		resize = 1;
		n = k;
		if(n > i)
			n = i;
		for(j=0; j<n; j++)
			if(old[j] != new[j])
				break;
		q0 = w->tag.q0;
		q1 = w->tag.q1;
		textdelete(&w->tag, j, k, TRUE);
		textinsert(&w->tag, j, new+j, i-j, TRUE);
		/* try to preserve user selection */
		r = runestrchr(old, '|');
		if(r){
			bar = r-old;
			if(q0 > bar){
				bar = (runestrchr(new, '|')-new)-bar;
				w->tag.q0 = q0+bar;
				w->tag.q1 = q1+bar;
			}
		}
	}
	free(old);
	free(new);
	w->tag.file->mod = FALSE;
	n = w->tag.file->nc+w->tag.ncache;
	if(w->tag.q0 > n)
		w->tag.q0 = n;
	if(w->tag.q1 > n)
		w->tag.q1 = n;
	textsetselect(&w->tag, w->tag.q0, w->tag.q1);
	windrawbutton(w);
	if(resize){
		w->tagsafe = 0;
		winresize(w, w->r, TRUE, TRUE);
	}
}

void
winsettag(Window *w)
{
	int i;
	File *f;
	Window *v;

	f = w->body.file;
	for(i=0; i<f->ntext; i++){
		v = f->text[i]->w;
		if(v->col->safe || v->body.maxlines>0)
			winsettag1(v);
	}
}

void
wincommit(Window *w, Text *t)
{
	Rune *r;
	int i;
	File *f;

	textcommit(t, TRUE);
	f = t->file;
	if(f->ntext > 1)
		for(i=0; i<f->ntext; i++)
			textcommit(f->text[i], FALSE);	/* no-op for t */
	if(t->what == Body)
		return;
	r = runemalloc(w->tag.file->nc);
	bufread(w->tag.file, 0, r, w->tag.file->nc);
	for(i=0; i<w->tag.file->nc; i++)
		if(r[i]==' ' || r[i]=='\t')
			break;
	if(runeeq(r, i, w->body.file->name, w->body.file->nname) == FALSE){
		seq++;
		filemark(w->body.file);
		w->body.file->mod = TRUE;
		w->dirty = TRUE;
		winsetname(w, r, i);
		winsettag(w);
	}
	free(r);
}

void
winaddincl(Window *w, Rune *r, int n)
{
	char *a;
	Dir *d;
	Runestr rs;

	a = runetobyte(r, n);
	d = dirstat(a);
	if(d == nil){
		if(a[0] == '/')
			goto Rescue;
		rs = dirname(&w->body, r, n);
		r = rs.r;
		n = rs.nr;
		free(a);
		a = runetobyte(r, n);
		d = dirstat(a);
		if(d == nil)
			goto Rescue;
		r = runerealloc(r, n+1);
		r[n] = 0;
	}
	free(a);
	if((d->qid.type&QTDIR) == 0){
		free(d);
		warning(nil, "%s: not a directory\n", a);
		free(r);
		return;
	}
	free(d);
	w->nincl++;
	w->incl = realloc(w->incl, w->nincl*sizeof(Rune*));
	memmove(w->incl+1, w->incl, (w->nincl-1)*sizeof(Rune*));
	w->incl[0] = runemalloc(n+1);
	runemove(w->incl[0], r, n);
	free(r);
	return;

Rescue:
	warning(nil, "%s: %r\n", a);
	free(r);
	free(a);
	return;
}

int
winclean(Window *w, int conservative)	/* as it stands, conservative is always TRUE */
{
	if(w->isscratch || w->isdir)	/* don't whine if it's a guide file, error window, etc. */
		return TRUE;
	if(!conservative && w->nopen[QWevent]>0)
		return TRUE;
	if(w->dirty){
		if(w->body.file->nname)
			warning(nil, "%.*S modified\n", w->body.file->nname, w->body.file->name);
		else{
			if(w->body.file->nc < 100)	/* don't whine if it's too small */
				return TRUE;
			warning(nil, "unnamed file modified\n");
		}
		w->dirty = FALSE;
		return FALSE;
	}
	return TRUE;
}

char*
winctlprint(Window *w, char *buf, int fonts)
{
	sprint(buf, "%11d %11d %11d %11d %11d ", w->id, w->tag.file->nc,
		w->body.file->nc, w->isdir, w->dirty);
	if(fonts)
		return smprint("%s%11d %q %11d " , buf, Dx(w->body.r),
			w->body.reffont->f->name, w->body.maxtab);
	return buf;
}

void
winevent(Window *w, char *fmt, ...)
{
	int n;
	char *b;
	Xfid *x;
	va_list arg;

	if(w->nopen[QWevent] == 0)
		return;
	if(w->owner == 0)
		error("no window owner");
	va_start(arg, fmt);
	b = vsmprint(fmt, arg);
	va_end(arg);
	if(b == nil)
		error("vsmprint failed");
	n = strlen(b);
	w->events = realloc(w->events, w->nevents+1+n);
	w->events[w->nevents++] = w->owner;
	memmove(w->events+w->nevents, b, n);
	free(b);
	w->nevents += n;
	x = w->eventx;
	if(x){
		w->eventx = nil;
		sendp(x->c, nil);
	}
}
