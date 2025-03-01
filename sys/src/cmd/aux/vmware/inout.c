#include "all.h"

void
setmousepoint(Point p)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackSetptrloc;
	u.bx = (p.x<<16) | p.y;
	backdoor(&u, 1);
}

Point
getmousepoint(void)
{
	Point p;
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackGetptrloc;
	backdoor(&u, 0);
	p.x = (signed)((u.ax>>16)&0xFFFF);
	p.y = (signed)(u.ax & 0xFFFF);
	return p;
}

int
getsnarflength(void)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackGetsellength;
	backdoor(&u, 0);
	return u.ax;
}

uint
getsnarfpiece(void)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackGetnextpiece;
	backdoor(&u, 0);
	return u.ax;
}

void
setsnarflength(uint len)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackSetsellength;
	u.bx = len;
	backdoor(&u, 1);
}

void
setsnarfpiece(uint p)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackSetnextpiece;
	u.bx = p;
	backdoor(&u, 1);
}

int
getversion(void)
{
	Ureg u;
	jmp_buf jb;

	memset(&u, 0, sizeof u);
	u.cx = BackGetversion;
	memmove(jb, backdoorjmp, sizeof jb);
	if(setjmp(backdoorjmp)){
		memmove(backdoorjmp, jb, sizeof jb);
		return -1;
	}
	backdoor(&u, 0);
	memmove(backdoorjmp, jb, sizeof jb);
	if(u.ax != VersionMagic || u.bx != BackMagic)
		return -1;
	return 0;
}

int
setdevicestate(uint id, int enable)
{
	Ureg u;

	if(enable)
		id |= 0x80000000;
	memset(&u, 0, sizeof u);
	u.cx = BackToggledev;
	u.bx = id;
	backdoor(&u, 0);
	return u.ax;
}

int
getdeviceinfo(uint id, uint offset, uint *p)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.bx = (id<<16) | offset;
	u.cx = BackGetdevlistel;
	backdoor(&u, 0);
	*p = u.bx;
	return u.ax;
}

void
setguistate(uint state)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.bx = state;
	u.cx = BackSetguiopt;
	backdoor(&u, 1);
}

uint
getguistate(void)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackGetguiopt;
	backdoor(&u, 0);
	return u.ax;
}

uint
copystep(uint x)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackHostcopy;
	u.bx = x;
	backdoor(&u, 0);
	return u.ax;
}

void
gettime(uint *sec, uint *micro, uint *lag)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackGettime;
	backdoor(&u, 0);

	*sec = u.ax;
	*micro = u.bx;
	*lag = u.cx;
}

void
stopcatchup(void)
{
	Ureg u;

	memset(&u, 0, sizeof u);
	u.cx = BackStopcatchup;
	backdoor(&u, 0);
}
