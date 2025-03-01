#include "all.h"

int statusonly;

void
usage(void)
{
	fprint(2, "usage: aux/isvmware [-s]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	ARGBEGIN{
	case 's':
		statusonly = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	atnotify(backdoorbell, 1);
	if(getversion() < 0){
		if(!statusonly)
			print("no vmware\n");
		exits("no vmware");
	}
	exits(nil);
}
