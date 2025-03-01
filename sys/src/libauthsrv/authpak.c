#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <authsrv.h>

#include "msqrt.mpc"
#include "decaf.mpc"
#include "edwards.mpc"
#include "elligator2.mpc"
#include "spake2ee.mpc"
#include "ed448.mpc"

typedef struct PAKcurve PAKcurve;
struct PAKcurve
{
	Lock;
	mpint	*P;
	mpint	*A;
	mpint	*D;
	mpint	*X;
	mpint	*Y;
};

void
mptober(mpint *b, uchar *buf, int len)
{
	int n;

	n = mptobe(b, buf, len, nil);
	assert(n >= 0);
	if(n < len){
		len -= n;
		memmove(buf+len, buf, n);
		memset(buf, 0, len);
	}
}

/* return uniform random [0..n-1] */
mpint*
mpnrand(mpint *n, void (*gen)(uchar*, int), mpint *b)
{
	int bits;

	bits = mpsignif(n);
	if(bits == 0)
		abort();
	if(b == nil){
		b = mpnew(bits);
		setmalloctag(b, getcallerpc(&n));
	}
	do {
		mprand(bits, gen, b);
	} while(mpmagcmp(b, n) >= 0);

	return b;
}

/* operands need to have m->top+1 digits of space and satisfy 0 ≤ a ≤ m-1 */
static mpint*
modarg(mpint *a, mpint *m)
{
	if(a->size <= m->top || a->sign < 0 || mpmagcmp(a, m) >= 0){
		a = mpcopy(a);
		mpmod(a, m, a);
		mpbits(a, Dbits*(m->top+1));
		a->top = m->top;
	} else if(a->top < m->top){
		memset(&a->p[a->top], 0, (m->top - a->top)*Dbytes);
	}
	return a;
}

void
mpmodadd(mpint *b1, mpint *b2, mpint *m, mpint *sum)
{
	mpint *a, *b;
	mpdigit d;
	int i, j;

	a = modarg(b1, m);
	b = modarg(b2, m);

	mpbits(sum, Dbits*2*(m->top+1));

	mpvecadd(a->p, m->top, b->p, m->top, sum->p);
	mpvecsub(sum->p, m->top+1, m->p, m->top, sum->p+m->top+1);

	d = sum->p[2*m->top+1];
	for(i = 0, j = m->top+1; i < m->top; i++, j++)
		sum->p[i] = (sum->p[i] & d) | (sum->p[j] & ~d);

	sum->top = m->top;
	sum->sign = 1;
	mpnorm(sum);

	if(a != b1)
		mpfree(a);
	if(b != b2)
		mpfree(b);
}

void
mpmodsub(mpint *b1, mpint *b2, mpint *m, mpint *diff)
{
	mpint *a, *b;
	mpdigit d;
	int i, j;

	a = modarg(b1, m);
	b = modarg(b2, m);

	mpbits(diff, Dbits*2*(m->top+1));

	a->p[m->top] = 0;
	mpvecsub(a->p, m->top+1, b->p, m->top, diff->p);
	mpvecadd(diff->p, m->top, m->p, m->top, diff->p+m->top+1);

	d = ~diff->p[m->top];
	for(i = 0, j = m->top+1; i < m->top; i++, j++)
		diff->p[i] = (diff->p[i] & d) | (diff->p[j] & ~d);

	diff->top = m->top;
	diff->sign = 1;
	mpnorm(diff);

	if(a != b1)
		mpfree(a);
	if(b != b2)
		mpfree(b);
}

void
mpmodmul(mpint *b1, mpint *b2, mpint *m, mpint *prod)
{
	mpint *a, *b;

	a = modarg(b1, m);
	b = modarg(b2, m);

	mpmul(a, b, prod);
	mpmod(prod, m, prod);

	if(a != b1)
		mpfree(a);
	if(b != b2)
		mpfree(b);
}

static PAKcurve*
authpak_curve(void)
{
	static PAKcurve a;

	lock(&a);
	if(a.P == nil){
		a.P = mpnew(0);
		a.A = mpnew(0);
		a.D = mpnew(0);
		a.X = mpnew(0);
		a.Y = mpnew(0);
		ed448_curve(a.P, a.A, a.D, a.X, a.Y);
//		a.P = mpfield(a.P);
	}
	unlock(&a);
	return &a;
}

void
authpak_hash(Authkey *k, char *u)
{
	static char info[] = "Plan 9 AuthPAK hash";
	uchar *bp, salt[SHA2_256dlen], h[2*PAKSLEN];
	mpint *H, *PX,*PY,*PZ,*PT;
	PAKcurve *c;

	H = mpnew(0);
	PX = mpnew(0);
	PY = mpnew(0);
	PZ = mpnew(0);
	PT = mpnew(0);

	sha2_256((uchar*)u, strlen(u), salt, nil);

	hkdf_x(	salt, SHA2_256dlen,
		(uchar*)info, sizeof(info)-1,
		k->aes, AESKEYLEN,
		h, sizeof(h),
		hmac_sha2_256, SHA2_256dlen);

	c = authpak_curve();

	betomp(h + 0*PAKSLEN, PAKSLEN, H);		/* HM */
	spake2ee_h2P(c->P,c->A,c->D, H, PX,PY,PZ,PT);	/* PM */

	bp = k->pakhash;
	mptober(PX, bp, PAKSLEN), bp += PAKSLEN;
	mptober(PY, bp, PAKSLEN), bp += PAKSLEN;
	mptober(PZ, bp, PAKSLEN), bp += PAKSLEN;
	mptober(PT, bp, PAKSLEN), bp += PAKSLEN;

	betomp(h + 1*PAKSLEN, PAKSLEN, H);		/* HN */
	spake2ee_h2P(c->P,c->A,c->D, H, PX,PY,PZ,PT);	/* PN */

	mptober(PX, bp, PAKSLEN), bp += PAKSLEN;
	mptober(PY, bp, PAKSLEN), bp += PAKSLEN;
	mptober(PZ, bp, PAKSLEN), bp += PAKSLEN;
	mptober(PT, bp, PAKSLEN);

	mpfree(PX);
	mpfree(PY);
	mpfree(PZ);
	mpfree(PT);
	mpfree(H);
}

void
authpak_new(PAKpriv *p, Authkey *k, uchar y[PAKYLEN], int isclient)
{
	mpint *PX,*PY,*PZ,*PT, *X, *Y;
	PAKcurve *c;
	uchar *bp;
	int bits;

	memset(p, 0, sizeof(PAKpriv));
	p->isclient = isclient != 0;

	X = mpnew(0);
	Y = mpnew(0);

	PX = mpnew(0);
	PY = mpnew(0);
	PZ = mpnew(0);
	PT = mpnew(0);

//	PX->flags |= MPtimesafe;
//	PY->flags |= MPtimesafe;
//	PZ->flags |= MPtimesafe;
//	PT->flags |= MPtimesafe;

	bp = k->pakhash + PAKPLEN*(p->isclient == 0);
	betomp(bp, PAKSLEN, PX), bp += PAKSLEN;
	betomp(bp, PAKSLEN, PY), bp += PAKSLEN;
	betomp(bp, PAKSLEN, PZ), bp += PAKSLEN;
	betomp(bp, PAKSLEN, PT);

	c = authpak_curve();

//	X->flags |= MPtimesafe;
	mpnrand(c->P, genrandom, X);

	spake2ee_1(c->P,c->A,c->D, X, c->X,c->Y, PX,PY,PZ,PT, Y);

	mptober(X, p->x, PAKXLEN);
	mptober(Y, p->y, PAKYLEN);

	memmove(y, p->y, PAKYLEN);

	mpfree(PX);
	mpfree(PY);
	mpfree(PZ);
	mpfree(PT);

	mpfree(X);
	mpfree(Y);
}

int
authpak_finish(PAKpriv *p, Authkey *k, uchar y[PAKYLEN])
{
	static char info[] = "Plan 9 AuthPAK key";
	uchar *bp, z[PAKSLEN], salt[SHA2_256dlen];
	mpint *PX,*PY,*PZ,*PT, *X, *Y, *Z, *ok;
	DigestState *s;
	PAKcurve *c;
	int ret;

	X = mpnew(0);
	Y = mpnew(0);
	Z = mpnew(0);
	ok = mpnew(0);

	PX = mpnew(0);
	PY = mpnew(0);
	PZ = mpnew(0);
	PT = mpnew(0);

//	PX->flags |= MPtimesafe;
//	PY->flags |= MPtimesafe;
//	PZ->flags |= MPtimesafe;
//	PT->flags |= MPtimesafe;

	bp = k->pakhash + PAKPLEN*(p->isclient != 0);
	betomp(bp, PAKSLEN, PX), bp += PAKSLEN;
	betomp(bp, PAKSLEN, PY), bp += PAKSLEN;
	betomp(bp, PAKSLEN, PZ), bp += PAKSLEN;
	betomp(bp, PAKSLEN, PT);

//	Z->flags |= MPtimesafe;
//	X->flags |= MPtimesafe;
	betomp(p->x, PAKXLEN, X);

	betomp(y, PAKYLEN, Y);

	c = authpak_curve();
	spake2ee_2(c->P,c->A,c->D, PX,PY,PZ,PT, X, Y, ok, Z);

	if(mpcmp(ok, mpzero) == 0){
		ret = -1;
		goto out;
	}

	mptober(Z, z, sizeof(z));

	s = sha2_256(p->isclient ? p->y : y, PAKYLEN, nil, nil);
	sha2_256(p->isclient ? y : p->y, PAKYLEN, salt, s);

	hkdf_x(	salt, SHA2_256dlen,
		(uchar*)info, sizeof(info)-1,
		z, sizeof(z),
		k->pakkey, PAKKEYLEN,
		hmac_sha2_256, SHA2_256dlen);

	ret = 0;
out:
	memset(z, 0, sizeof(z));
	memset(p, 0, sizeof(PAKpriv));

	mpfree(PX);
	mpfree(PY);
	mpfree(PZ);
	mpfree(PT);

	mpfree(X);
	mpfree(Y);
	mpfree(Z);
	mpfree(ok);

	return ret;
}
