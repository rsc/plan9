#include <u.h>
#include <libc.h>

int touch(int, char *);
ulong now;

void
usage(void)
{
	fprint(2, "usage: touch [-c] [-t time] files\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *t, *s;
	int nocreate = 0;
	int status = 0;

	now = time(0);
	ARGBEGIN{
	case 't':
		t = EARGF(usage());
		now = strtoul(t, &s, 0);
		if(s == t || *s != '\0')
			usage();
		break;
	case 'c':
		nocreate = 1;
		break;
	default:
		usage();
	}ARGEND

	if(!*argv)
		usage();
	while(*argv)
		status += touch(nocreate, *argv++);
	if(status)
		exits("touch");
	exits(0);
}

touch(int nocreate, char *name)
{
	Dir stbuff;
	int fd;

	nulldir(&stbuff);
	stbuff.mtime = now;
	if(dirwstat(name, &stbuff) >= 0)
		return 0;
	if(nocreate){
		fprint(2, "touch: %s: cannot wstat: %r\n", name);
		return 1;
	}
	if((fd = create(name, OREAD|OEXCL, 0666)) < 0){
		fprint(2, "touch: %s: cannot create: %r\n", name);
		return 1;
	}
	dirfwstat(fd, &stbuff);
	close(fd);
	return 0;
}
