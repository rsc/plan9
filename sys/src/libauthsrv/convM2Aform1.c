#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <libsec.h>

#define	CHAR(x)		f->x = *p++
#define	SHORT(x)	f->x = (p[0] | (p[1]<<8)); p += 2
#define	VLONG(q)	q = (p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); p += 4
#define	LONG(x)		VLONG(f->x)
#define	STRING(x,n)	memmove(f->x, p, n); p += n

void
convM2Aform1(char *ap, Authenticator *f, char *key)
{
	uchar *p;
	Chachastate s;

	if(key) {
		setupChachastate(&s, (uchar *)key, ChachaKeylen, (uchar*)ap, ChachaIVlen, 0);
		ap += FORM1NONCELEN;
		ccpoly_decrypt((uchar *)ap, CHALLEN+NONCELEN, nil, 0, (uchar *)ap+CHALLEN+NONCELEN, &s);
	}
	p = (uchar*)ap;
//	CHAR(num);
	f->num = AuthAc; //todo: actually parse;
	STRING(chal, CHALLEN);
	STRING(rand, NONCELEN);
	LONG(id);

	USED(p);
}
