#include <u.h>
#include <libc.h>

void
usage(void)
{
	fprint(2, "usage: fp [testname]\n");
	exits("usage");
}

void
nop(void)
{
}

void
float0(char *ctx)
{
	double d;

	d=1.0;
	d /= 2.0;
	if(d != 0.5)
		sysfatal("%s: float1 %g", ctx, d);
}

void
float1(void)
{
	float0("top");
}

void
floatnote(void*, char *msg)
{
	print("floatnote %s\n", msg);
	float0("handler");
	print("floatnote done\n");
	noted(NCONT);
}

void
noteonly(void)
{
	notify(floatnote);
	postnote(PNPROC, getpid(), "mynote");
}

void
beforenote(void)
{
	float0("before");
	noteonly();
}

void
afternote(void)
{
	noteonly();
	float0("after");
}

void
beforeafter(void)
{
	float0("before");
	noteonly();
	float0("after");
}

void
simul(void)
{
	int pid;
	double d;
	vlong v;
	int i;

	notify(floatnote);

	pid = fork();
	if(pid < 0)
		sysfatal("fork: %r");
	if(pid == 0) {
		sleep(10);
		print("postnote\n");
		postnote(PNPROC, getppid(), "mynote");
		print("postnote done\n");
		exits(0);
	}

	print("start counting\n");
	d = 1.0;
	v = 1;
	for(i=0; i<10000000; i++) {
		if(i%100000 == 0) print(".");
		d += (double)i;
		v += i;
		if(d != (vlong)v)
			sysfatal("out of sync %.17g %lld", d, v);
	}
	print("%.17g %lld\n", d, v);

}

struct {
	char *name;
	void (*fn)(void);
} tests[] = {
	{"nop", nop},
	{"float1", float1},
	{"noteonly", noteonly},
	{"beforenote", beforenote},
	{"afternote", afternote},
	{"beforeafter", beforeafter},
	{"simul", simul},
};

void
main(int argc, char **argv)
{
	int i, j, pid;
	char *status;
	Waitmsg *w;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	status = nil;
	for(i=0; i<nelem(tests); i++) {
		if(argc > 0){
			for(j=0; j<argc; j++)
				if(strcmp(argv[j], tests[i].name) == 0)
					goto Found;
			continue;
		}
	Found:
		print("# %s\n", tests[i].name);
		pid = fork();
		if(pid == 0){
			tests[i].fn();
			exits(0);
		}
		if(pid < 0)
			sysfatal("fork: %r");
		w = wait();
		if(w == nil)
			sysfatal("wait: %r");
		if(w->msg && w->msg[0]){
			print("%s: %s\n", tests[i].name, w->msg);
			status = "broken";
		}
	}
	if(status==nil)
		print("PASS\n");
	else
		print("FAIL\n");
	exits(status);
}
