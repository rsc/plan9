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
convM2Tform1(char *ap, Ticket *f, char *key)
{
	Chachastate s;
	uchar *p;

	if(key) {
		setupChachastate(&s, (uchar *)key, ChachaKeylen, (uchar*)ap, ChachaIVlen, 0);
		ap += FORM1NONCELEN;
		ccpoly_decrypt((uchar *)ap, TICKETLENFORM1-FORM1NONCELEN-FORM1AUTHTAGLEN, nil, 0, (uchar *)ap+TICKETLENFORM1-FORM1NONCELEN-FORM1AUTHTAGLEN, &s);
	}
	p = (uchar*)ap;
//	CHAR(num);
	f->num = AuthTs; // todo: actually parse;
	STRING(chal, CHALLEN);
	STRING(cuid, ANAMELEN);
	f->cuid[ANAMELEN-1] = 0;
	STRING(suid, ANAMELEN);
	f->suid[ANAMELEN-1] = 0;
print(f->suid);
	STRING(key, NONCELEN);
	USED(p);
}

