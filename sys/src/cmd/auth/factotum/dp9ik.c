/*
 * dp9ik uses AuthPAK diffie hellman key exchange with the
 * auth server to protect the password derived key from offline
 * dictionary attacks.
 *
 * Client protocol:
 *	write challenge[challen]
 *	 read pakreq[ticketreqlen + pakylen]
 *	  write paky[pakylen]
 *	write ticket + authenticator
 *	read authenticator
 *
 * Server protocol:
 * 	read challenge[challen]
 *	 write pakreq[ticketreqlen + pakylen]
 *	  read paky[pakylen]
 *	read ticket + authenticator
 *	write authenticator
 */

#include "dat.h"

struct State
{
	int vers;
	Key *key;
	Ticket t;
	Ticketreq tr;
	Authkey k;
	PAKpriv p;
	char cchal[CHALLEN];
	char tbuf[TICKETLENFORM1+AUTHENTLENFORM1];
	uchar rand[2*NONCELEN];
	char authkey[DESKEYLEN];
	uchar *secret;
	int speakfor;
};

enum
{
	/* client phases */
	CHaveChal,
	CNeedPAKreq,
	CHavePAKy,
	CHaveTicket,
	CNeedAuth,

	/* server phases */
	SNeedChal,
	SHavePAKreq,
	SNeedPAKy,
	SNeedTicket,
	SHaveAuth,

	Maxphase,
};

static char *phasenames[Maxphase] =
{
[CHaveChal]		"CHaveChal",
[CHaveTicket]		"CHaveTicket",
[CNeedAuth]		"CNeedAuth",

[SNeedChal]		"SNeedChal",
[SHavePAKreq]		"SHavePAKreq",
[SNeedPAKy]		"SNeedPAKy",
[SNeedTicket]		"SNeedTicket",
[SHaveAuth]		"SHaveAuth",
};

static int gettickets(State*, char*, char*);

static int
dp9ikinit(Proto *p, Fsstate *fss)
{
	State *s;
	int iscli, ret;
	Key *k;
	Keyinfo ki;
	Attr *attr;

	if((iscli = isclient(_strfindattr(fss->attr, "role"))) < 0)
		return failure(fss, nil);

	s = emalloc(sizeof *s);
	fss = fss;
	fss->phasename = phasenames;
	fss->maxphase = Maxphase;
	if(iscli){
		fss->phase = CHaveChal;
		memrandom(s->cchal, CHALLEN);
	}else{
		s->tr.type = AuthTreq;
		attr = setattr(_copyattr(fss->attr), "proto=dp9ik");
		mkkeyinfo(&ki, fss, attr);
		ki.user = nil;
		ret = findkey(&k, &ki, "user? dom?");
		_freeattr(attr);
		if(ret != RpcOk){
			free(s);
			return ret;
		}
		safecpy(s->tr.authid, _strfindattr(k->attr, "user"), sizeof(s->tr.authid));
		safecpy(s->tr.authdom, _strfindattr(k->attr, "dom"), sizeof(s->tr.authdom));
		s->key = k;
		memcpy(s->k.aes, k->priv, AESKEYLEN);	
//		memmove(&s->k, k->priv, sizeof(Authkey)); // todo, is an AuthKey struct required?
		authpak_hash(&s->k, s->tr.authid);
		memrandom(s->tr.chal, sizeof s->tr.chal);
		fss->phase = SNeedChal;
	}
	fss->ps = s;
	return RpcOk;
}

static int
dp9ikread(Fsstate *fss, void *a, uint *n)
{
	int m;
	State *s;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "read");

	case SHavePAKreq:
		m = TICKREQLEN + PAKYLEN;
		if(*n < m)
			return toosmall(fss, m);
		s->tr.type = AuthPAK;
		*n = convTR2M(&s->tr, a);
		authpak_new(&s->p, &s->k, (uchar*)a + *n, 1);
		*n += PAKYLEN;
		fss->phase = SNeedPAKy;
		return RpcOk;

	case CHaveTicket:
		m = TICKETLEN+AUTHENTLEN;
		if(*n < m)
			return toosmall(fss, m);
		*n = m;
		memmove(a, s->tbuf, m);
		fss->phase = CNeedAuth;
		return RpcOk;

	case SHaveAuth:
		m = AUTHENTLENFORM1;
		if(*n < m)
			return toosmall(fss, m);
		*n = m;
		memmove(a, s->tbuf+TICKETLENFORM1, m);
		static char info[] = "Plan 9 session secret";
		fss->ai.cuid = s->t.cuid;
		fss->ai.suid = s->t.suid;
		s->secret = emalloc(256);
		print("%s\n", s->rand);
		hkdf_x(	s->rand, 2*NONCELEN,
			(uchar*)info, sizeof(info)-1,
			(uchar *)s->t.key, NONCELEN,
			s->secret, 256,
			hmac_sha2_256, SHA2_256dlen);
		fss->ai.secret = s->secret;
		fss->ai.nsecret = 256;
		fss->haveai = 1;
		fss->phase = Established;
		return RpcOk;
	}
}

static int
dp9ikwrite(Fsstate *fss, void *a, uint n)
{
	int m;
	Authenticator auth;
	State *s;

	s = fss->ps;
	switch(fss->phase){
	default:
		return phaseerror(fss, "write");

	case SNeedChal:
		m = CHALLEN;
		if(n < m)
			return toosmall(fss, m);
		memmove(s->cchal, a, m);
		fss->phase = SHavePAKreq;
		return RpcOk;

	case SNeedPAKy:
		m = PAKYLEN;
		if(n < m)
			return toosmall(fss, m);
		if(authpak_finish(&s->p, &s->k, (uchar*)a))
			return failure(fss, Easproto);
		fss->phase = SNeedTicket;
		return RpcOk;

	case SNeedTicket:
		m = TICKETLENFORM1 + AUTHENTLENFORM1;
		if(n < m)
			return toosmall(fss, m);
		convM2Tform1(a, &s->t, (char *)s->k.pakkey);
		if(s->t.num != AuthTs
		|| memcmp(s->t.chal, s->tr.chal, CHALLEN) != 0)
			return failure(fss, Easproto);
		convM2Aform1((char*)a+TICKETLENFORM1, &auth, s->t.key);
		if(auth.num != AuthAc
		|| memcmp(auth.chal, s->tr.chal, CHALLEN) != 0)
			return failure(fss, Easproto);
		memmove(s->rand, auth.rand, NONCELEN);
		memrandom(s->rand + NONCELEN, NONCELEN);
		auth.num = AuthAs;
		memmove(auth.chal, s->cchal, CHALLEN);
		memmove(auth.rand, s->rand + NONCELEN, NONCELEN);
		convA2Mform1(&auth, s->tbuf+TICKETLENFORM1, s->t.key);
		fss->phase = SHaveAuth;
		return RpcOk;

	case CNeedAuth:
		m = AUTHENTLEN;
		if(n < m)
			return toosmall(fss, m);
		convM2A(a, &auth, s->t.key);
		if(auth.num != AuthAs
		|| memcmp(auth.chal, s->cchal, CHALLEN) != 0
		|| auth.id != 0)
			return failure(fss, Easproto);
		fss->ai.cuid = s->t.cuid;
		fss->ai.suid = s->t.suid;
		s->secret = emalloc(8);
		des56to64((uchar*)s->t.key, s->secret);
		fss->ai.secret = s->secret;
		fss->ai.nsecret = 8;
		fss->haveai = 1;
		fss->phase = Established;
		return RpcOk;
	}
}

static void
dp9ikclose(Fsstate *fss)
{
	State *s;

	s = fss->ps;
	if(s->secret != nil){
		free(s->secret);
		s->secret = nil;
	}
	if(s->key != nil){
		closekey(s->key);
		s->key = nil;
	}
	free(s);
}

static int
unhex(char c)
{
	if('0' <= c && c <= '9')
		return c-'0';
	if('a' <= c && c <= 'f')
		return c-'a'+10;
	if('A' <= c && c <= 'F')
		return c-'A'+10;
	abort();
	return -1;
}

static int
hexparse(char *hex, uchar *dat, int ndat)
{
	int i;

	if(strlen(hex) != 2*ndat)
		return -1;
	if(hex[strspn(hex, "0123456789abcdefABCDEF")] != '\0')
		return -1;
	for(i=0; i<ndat; i++)
		dat[i] = (unhex(hex[2*i])<<4)|unhex(hex[2*i+1]);
	return 0;
}

static int
dp9ikaddkey(Key *k, int before)
{
	char *s;

	k->priv = emalloc(AESKEYLEN);
	if(s = _strfindattr(k->privattr, "!hex")){
		if(hexparse(s, k->priv, AESKEYLEN) < 0){
			free(k->priv);
			k->priv = nil;
			werrstr("malformed key data");
			return -1;
		}
	}else if(s = _strfindattr(k->privattr, "!password")){
		passtoaeskey((char*)k->priv, s);
	}else{
		werrstr("no key data");
		free(k->priv);
		k->priv = nil;
		return -1;
	}
	return replacekey(k, before);
}

static void
dp9ikclosekey(Key *k)
{
	free(k->priv);
}

static int
getastickets(State *s, char *trbuf, char *tbuf)
{
	int asfd, rv;
	char *dom;

	if((dom = _strfindattr(s->key->attr, "dom")) == nil){
		werrstr("auth key has no domain");
		return -1;
	}
	asfd = _authdial(nil, dom);
	if(asfd < 0)
		return -1;
	rv = _asgetticket(asfd, trbuf, tbuf);
	close(asfd);
	return rv;
}

static int
mkserverticket(State *s, char *tbuf)
{
	Ticketreq *tr = &s->tr;
	Ticket t;

	if(strcmp(tr->authid, tr->hostid) != 0)
		return -1;
/* this keeps creating accounts on martha from working.  -- presotto
	if(strcmp(tr->uid, "none") == 0)
		return -1;
*/
	memset(&t, 0, sizeof(t));
	memmove(t.chal, tr->chal, CHALLEN);
	strcpy(t.cuid, tr->uid);
	strcpy(t.suid, tr->uid);
	memrandom(t.key, DESKEYLEN);
	t.num = AuthTc;
	convT2M(&t, tbuf, s->key->priv);
	t.num = AuthTs;
	convT2M(&t, tbuf+TICKETLEN, s->key->priv);
	return 0;
}

static int
gettickets(State *s, char *trbuf, char *tbuf)
{
/*
	if(mktickets(s, trbuf, tbuf) >= 0)
		return 0;
*/
	if(getastickets(s, trbuf, tbuf) >= 0)
		return 0;
	return mkserverticket(s, tbuf);
}

Proto dp9ik = {
.name=	"dp9ik",
.init=		dp9ikinit,
.write=	dp9ikwrite,
.read=	dp9ikread,
.close=	dp9ikclose,
.addkey=	dp9ikaddkey,
.closekey=	dp9ikclosekey,
.keyprompt=	"user? !password?"
};
