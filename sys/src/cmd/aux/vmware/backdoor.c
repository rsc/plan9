#include "all.h"

jmp_buf backdoorjmp;
static int doorerror;

int
backdoorbell(void *v, char *msg)
{
	uchar *p;
	Ureg *u;

	u = v;
	p = (uchar*)u->pc;
	/* ED is INL and EF is OUTL */
	if((*p==0xED || *p==0xEF) && strstr(msg, "sys: trap: general protection violation")){
		u->pc++;	/* hop over INL/OUTL */
		doorerror = 1;
		return 1;
	}
	return 0;
}

void
backdoor(Ureg *u, int isout)
{
	u->ax = BackMagic;
	u->dx &= ~0xFFFF;
	u->dx |= BackPort;
	u->di = isout;
	doorerror = 0;
	asmbackdoor(u);
	if(doorerror)
		longjmp(backdoorjmp, 1);
}
