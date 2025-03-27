#include <u.h>
#include <libc.h>
#include <bio.h>
#include <flate.h>

/*
 * gzip header fields
 */
enum
{
	GZMAGIC1	= 0x1f,
	GZMAGIC2	= 0x8b,

	GZDEFLATE	= 8,

	GZFTEXT		= 1 << 0,		/* file is text */
	GZFHCRC		= 1 << 1,		/* crc of header included */
	GZFEXTRA	= 1 << 2,		/* extra header included */
	GZFNAME		= 1 << 3,		/* name of file included */
	GZFCOMMENT	= 1 << 4,		/* header comment included */
	GZFMASK		= (1 << 5) -1,		/* mask of specified bits */

	GZXFAST		= 2,			/* used fast algorithm, little compression */
	GZXBEST		= 4,			/* used maximum compression algorithm */

	GZOSFAT		= 0,			/* FAT file system */
	GZOSAMIGA	= 1,			/* Amiga */
	GZOSVMS		= 2,			/* VMS or OpenVMS */
	GZOSUNIX	= 3,			/* Unix */
	GZOSVMCMS	= 4,			/* VM/CMS */
	GZOSATARI	= 5,			/* Atari TOS */
	GZOSHPFS	= 6,			/* HPFS file system */
	GZOSMAC		= 7,			/* Macintosh */
	GZOSZSYS	= 8,			/* Z-System */
	GZOSCPM		= 9,			/* CP/M */
	GZOSTOPS20	= 10,			/* TOPS-20 */
	GZOSNTFS	= 11,			/* NTFS file system */
	GZOSQDOS	= 12,			/* QDOS */
	GZOSACORN	= 13,			/* Acorn RISCOS */
	GZOSUNK		= 255,

	GZCRCPOLY	= 0xedb88320UL,

	GZOSINFERNO	= GZOSUNIX,
};

typedef struct	GZHead	GZHead;

struct GZHead
{
	ulong	mtime;
	char	*file;
};

static	int	crcwrite(void *bout, void *buf, int n);
static	int	get1(Biobuf *b);
static	ulong	get4(Biobuf *b);
static	int	gunzipf(char *file, int stdout);
static	int	gunzip(int ofd, char *ofile, Biobuf *bin);
static	void	header(Biobuf *bin, GZHead *h);
static	void	trailer(Biobuf *bin, long wlen);
static	void	error(char*, ...);
#pragma	varargck	argpos	error	1

static	Biobuf	bin;
static	ulong	crc;
static	ulong	*crctab;
static	int	debug;
static	char	*delfile;
static	vlong	gzok;
static	char	*infile;
static	int	settimes;
static	int	table;
static	int	verbose;
static	int	wbad;
static	ulong	wlen;
static	jmp_buf	zjmp;

void
_ungzip(int in, int out)
{
	Biobuf bin;
	int ok;

	if(crctab == nil){
		crctab = mkcrctab(GZCRCPOLY);
		ok = inflateinit();
		if(ok != FlateOk)
			sysfatal("inflateinit failed: %s", flateerr(ok));
	}

	Binit(&bin, in, OREAD);
	if(gunzip(out, "gzroot", &bin) != 1) {
		fprint(2, "gunzip failed\n");
		_exits("gunzip");
	}
}

int
ungzip(int in)
{
	int rv, out, p[2];

	if(pipe(p) < 0)
		sysfatal("pipe: %r");

	rv = p[0];
	out = p[1];
	switch(rfork(RFPROC|RFFDG|RFNOTEG|RFMEM)){
	case -1:
		sysfatal("fork: %r");
	case 0:
		close(rv);
		break;
	default:
		close(in);
		close(out);
		return rv;
	}

	_ungzip(in, out);
	_exits(0);
	return -1;	/* not reached */
}

static int
gunzip(int ofd, char *ofile, Biobuf *bin)
{
	Dir *d;
	GZHead h;
	int err;

	h.file = nil;
	gzok = 0;
	for(;;){
		if(Bgetc(bin) < 0)
			return 1;
		Bungetc(bin);

		if(setjmp(zjmp))
			return 0;
		header(bin, &h);
		gzok = 0;

		wlen = 0;
		crc = 0;

		if(!table && verbose)
			fprint(2, "extracting %s to %s\n", h.file, ofile);

		err = inflate((void*)ofd, crcwrite, bin, (int(*)(void*))Bgetc);
		if(err != FlateOk)
			error("inflate failed: %s", flateerr(err));

		trailer(bin, wlen);

		if(table){
			if(verbose)
				print("%-32s %10ld %s", h.file, wlen, ctime(h.mtime));
			else
				print("%s\n", h.file);
		}else if(settimes && h.mtime && (d=dirfstat(ofd)) != nil){
			d->mtime = h.mtime;
			dirfwstat(ofd, d);
			free(d);
		}

		free(h.file);
		h.file = nil;
		gzok = Boffset(bin);
	}
}

static void
header(Biobuf *bin, GZHead *h)
{
	char *s;
	int i, c, flag, ns, nsa;

	if(get1(bin) != GZMAGIC1 || get1(bin) != GZMAGIC2)
		error("bad gzip file magic");
	if(get1(bin) != GZDEFLATE)
		error("unknown compression type");

	flag = get1(bin);
	if(flag & ~(GZFTEXT|GZFEXTRA|GZFNAME|GZFCOMMENT|GZFHCRC))
		fprint(2, "gunzip: reserved flags set, data may not be decompressed correctly\n");

	/* mod time */
	h->mtime = get4(bin);

	/* extra flags */
	get1(bin);

	/* OS type */
	get1(bin);

	if(flag & GZFEXTRA)
		for(i=get1(bin); i>0; i--)
			get1(bin);

	/* name */
	if(flag & GZFNAME){
		nsa = 32;
		ns = 0;
		s = malloc(nsa);
		if(s == nil)
			error("out of memory");
		while((c = get1(bin)) != 0){
			s[ns++] = c;
			if(ns >= nsa){
				nsa += 32;
				s = realloc(s, nsa);
				if(s == nil)
					error("out of memory");
			}
		}
		s[ns] = '\0';
		h->file = s;
	}else
		h->file = strdup("<unnamed file>");

	/* comment */
	if(flag & GZFCOMMENT)
		while(get1(bin) != 0)
			;

	/* crc16 */
	if(flag & GZFHCRC){
		get1(bin);
		get1(bin);
	}
}

static void
trailer(Biobuf *bin, long wlen)
{
	ulong tcrc;
	long len;

	tcrc = get4(bin);
	if(tcrc != crc)
		error("crc mismatch");

	len = get4(bin);

	if(len != wlen)
		error("bad output length: expected %lud got %lud", wlen, len);
}

static ulong
get4(Biobuf *b)
{
	ulong v;
	int i, c;

	v = 0;
	for(i = 0; i < 4; i++){
		c = Bgetc(b);
		if(c < 0)
			error("unexpected eof reading file information");
		v |= c << (i * 8);
	}
	return v;
}

static int
get1(Biobuf *b)
{
	int c;

	c = Bgetc(b);
	if(c < 0)
		error("unexpected eof reading file information");
	return c;
}

static int
crcwrite(void *out, void *buf, int n)
{
	int fd, nw;

	wlen += n;
	crc = blockcrc(crctab, crc, buf, n);
	fd = (int)(uintptr)out;
	if(fd < 0)
		return n;
	nw = write(fd, buf, n);
	if(nw != n)
		wbad = 1;
	return nw;
}

static void
error(char *fmt, ...)
{
	va_list arg;

	if(gzok)
		fprint(2, "gunzip: %s: corrupted data after byte %lld ignored\n", infile, gzok);
	else{
		fprint(2, "gunzip: ");
		if(infile)
			fprint(2, "%s: ", infile);
		va_start(arg, fmt);
		vfprint(2, fmt, arg);
		va_end(arg);
		fprint(2, "\n");

		if(delfile != nil){
			fprint(2, "gunzip: removing output file %s\n", delfile);
			remove(delfile);
			delfile = nil;
		}
	}

	longjmp(zjmp, 1);
}
