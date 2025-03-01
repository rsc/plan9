#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <libsec.h>

#define	CHAR(x)		*p++ = f->x
#define	SHORT(x)	p[0] = f->x; p[1] = f->x>>8; p += 2
#define	VLONG(q)	p[0] = (q); p[1] = (q)>>8; p[2] = (q)>>16; p[3] = (q)>>24; p += 4
#define	LONG(x)		VLONG(f->x)
#define VSTRING(s,n)	memmove(p, s, n); p += n
#define	STRING(x,n)	VSTRING(f->x, n);

int
convA2Mform1(Authenticator *f, char *ap, char *key)
{
	int n;
	uchar *p;
	static u32int counter;
	Chachastate s;

	p = (uchar*)ap;
	VSTRING("form1 As", FORM1SIGLEN);
	VLONG(counter++);
	STRING(chal, CHALLEN);
	STRING(rand, NONCELEN);
	n = p - (uchar*)ap;
	if(key) {
		setupChachastate(&s, (uchar *)key, ChachaKeylen, (uchar*)ap, ChachaIVlen, 0);
		ap += FORM1NONCELEN;
		n -= FORM1NONCELEN;
		ccpoly_encrypt((uchar *)ap, n, nil, 0, (uchar *)ap+n, &s);
	}
	return n;
}
