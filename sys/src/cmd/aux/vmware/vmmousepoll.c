#include <u.h>
#include <libc.h>

void
usage(void)
{
	fprint(2, "usage: aux/vmmousepoll [/mnt/vmware/mouse]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fd;
	char *file;
	char buf[50];

	quotefmtinstall();
	ARGBEGIN{
	default:
		usage();
	}ARGEND

	switch(argc){
	default:
		usage();
	case 0:
		file = "/mnt/vmware/mouse";
		break;
	case 1:
		file = argv[0];
		break;
	}

	if((fd = open(file, OREAD)) < 0)
		sysfatal("open %q: %r", file);
		
	for(;;){
		sleep(250);
		if(pread(fd, buf, sizeof buf, 0) < 0)
			break;
	}
	exits(nil);
}
