#include <u.h>
#include <libc.h>

ulong
time(long *tp)
{
	vlong t;

	t = nsec()/1000000000LL;
	if(tp != nil)
		*tp = t;
	return t;
}
