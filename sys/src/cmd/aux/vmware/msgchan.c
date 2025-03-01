#include "all.h"
#include <fcall.h>
#include <thread.h>
#include <9p.h>	/* for emalloc9p */

enum
{
	/* high bits of u.cx */
	CmdOpen		= 0x00,
	CmdSendsize	= 0x01,
	CmdSenddata	= 0x02,
	CmdRecvsize	= 0x03,
	CmdRecvdata	= 0x04,
	CmdRecvstatus	= 0x05,
	CmdClose		= 0x06,

	StatSuccess	= 0x0001,		/* request succeeded */
	StatHavedata	= 0x0002,		/* vmware has message available */
	StatClosed	= 0x0004,		/* channel got closed */
	StatUnsent	= 0x0008,		/* vmware removed message before it got delivered */
	StatChkpt		= 0x0010,		/* checkpoint occurred */
	StatPoweroff	= 0x0020,		/* underlying device is powering off */
};


Msgchan*
openmsg(ulong proto)
{
	Msgchan *c;
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = (CmdOpen<<16) | BackMessage;
	u.bx = proto;

	backdoor(&u, 0);

fprint(2, "msgopen %.8lux\n", u.cx);
	if(!(u.cx & (StatSuccess<<16))){
		fprint(2, "message %.8lux\n", u.cx);
		werrstr("unable to open message channel (%.8lux)", u.cx);
		return nil;
	}

	c = emalloc9p(sizeof(*c));
	c->id = u.dx>>16;
	return c;
}

enum
{
	Ok = 0,
	ErrBad = -1,
	ErrChkpt = -2,
};

static int
sendpiece(Msgchan *c, int ty, ulong a)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = (ty<<16) | BackMessage;
	u.dx = c->id<<16;
	u.bx = a;

	backdoor(&u, 0);

	if(!(u.cx & (StatSuccess<<16))){
fprint(2, "cmd %x failed\n", ty);
		if(u.cx & (StatChkpt<<16))
			return ErrChkpt;
		return ErrBad;
	}
	return 0;
}

int
sendmsg(Msgchan *c, void *a, int n)
{
	int i, m, e, left;
	uchar *p;
	ulong v;

Again:
	v = n;
	if(sendpiece(c, CmdSendsize, v) < 0)
		return -1;

	p = a;
	left = n;
	while(left > 0){
		m = left;
		if(m > 4)
			m = 4;
		v = 0;
		for(i=0; i<m; i++)
			v |= *p++ << (8*i);
		if((e=sendpiece(c, CmdSenddata, v)) < 0){
			if(e == -2)
				goto Again;
			return -1;
		}
		left -= m;
	}
	return 0;
}

static int
recvpiece(Msgchan *c, int ty, ulong *stat, ulong *a)
{
	Ureg u;

fprint(2, "recvpiece %d %d\n", c->id, ty);
	memset(&u, 0, sizeof u);
	u.cx = (ty<<16) | BackMessage;
	u.bx = StatSuccess;
	u.dx = c->id<<16;

	backdoor(&u, 0);

	if(!(u.cx & (StatSuccess<<16))){
fprint(2, "no success %lux\n", u.cx);
		if(u.cx & (StatChkpt<<16))
			return ErrChkpt;
		return ErrBad;
	}
	*stat = u.cx;
	if(ty == CmdRecvsize && !(u.cx&(StatHavedata<<16))){
fprint(2, "poll got no data\n");
		return 0;
	}

	if(ty != CmdRecvstatus && (u.dx>>16) != ty-2){
fprint(2, "got wrong answer! %lux\n", u.dx);
		werrstr("protocol error");
		return ErrBad;
	}
	*a = u.bx;
fprint(2, "got %lux\n", *a);
	return 0;
}

static void
signalerror(Msgchan *c, int ty)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = (ty<<16) | BackMessage;
	u.dx = c->id<<16;
	u.bx = 0;
	backdoor(&u, 0);
}

int
recvmsg(Msgchan *c, void **pp)
{
	int left;
	ulong v, stat, tot;
	uchar *p;

Top:
	if(recvpiece(c, CmdRecvsize, &stat, &tot) < 0)
		return -1;

	if(!(stat & (StatHavedata<<16)))
		return 0;

	free(c->a);
	c->a = emalloc9p(tot+5);
	c->na = tot;
	p = c->a;		/* NUL terminate for callers */

	left = tot;
	while(left > 0){
		switch(recvpiece(c, CmdRecvdata, &stat, &v)){
		case ErrChkpt:
			goto Top;
		case ErrBad:
			signalerror(c, CmdRecvdata);
			return -1;
		}
		*(uint*)p = v;
		p += 4;
		left -= 4;
	}
	((char*)c->a)[tot] = '\0';

	switch(recvpiece(c, CmdRecvstatus, &stat, &v)){
	case ErrChkpt:
		goto Top;
	case ErrBad:
		/* BUG: signal receipt of error */
		signalerror(c, CmdRecvstatus);
		return -1;
	}

	*pp = c->a;
	return tot;
}

int
closemsg(Msgchan *c)
{
	Ureg u;

	memset(&u, 0, sizeof u);

	u.cx = (CmdClose<<16) | BackMessage;
	u.dx = c->id;

	backdoor(&u, 0);

	if(!(u.cx & (StatSuccess<<16))){
		werrstr("unable to close message channel");
		return -1;
	}

	free(c->a);
	c->a = nil;
	free(c);
	return 0;
}
