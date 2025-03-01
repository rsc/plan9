#include <u.h>
#include <libc.h>
#include <ureg.h>
#include <draw.h>

typedef struct Msgchan Msgchan;
typedef struct Ureg Ureg;

typedef ulong uint32;
typedef int       int32;
typedef short     int16;
typedef char      int8;
typedef uvlong	uint64;
typedef vlong 	int64;
typedef char	Bool;

struct Msgchan
{
	ushort id;
	uchar *a;
	int na;
};

enum
{
	/* low bits of u.cx */
	BackGetmhz		= 1,
	BackApm			= 2,
	BackGetdiskgeo	= 3,
	BackGetptrloc		= 4,
	BackSetptrloc		= 5,
	BackGetsellength	= 6,
	BackGetnextpiece	= 7,
	BackSetsellength	= 8,
	BackSetnextpiece	= 9,
	BackGetversion		= 10,
	BackGetdevlistel	= 11,
	BackToggledev		= 12,
	BackGetguiopt		= 13,
	BackSetguiopt		= 14,
	BackGetscreensize	= 15,
	BackGetpcisvgaen	= 16,
	BackSetpcisvgaen	= 17,
	/* 18-20 not used */
	BackHostcopy		= 21,
	BackGetos2intcurs	= 22,
	BackGettime		= 23,
	BackStopcatchup	= 24,
	BackPutchr		= 25,
	BackEnablemsg	= 26,
	BackGototcl		= 27,
	BackInitscsiprom	= 28,
	BackInt13			= 29,
	BackMessage		= 30,

	BackMagic		= 0x564D5868,
	VersionMagic		= 6,
	BackPort			= 0x5658,

};

void		asmbackdoor(Ureg*);
void		backdoor(Ureg*, int);
int		backdoorbell(void*, char*);
int		closemsg(Msgchan*);
int		getdeviceinfo(uint, uint, uint*);
Point		getmousepoint(void);
int		getsnarflength(void);
uint		getsnarfpiece(void);
Msgchan*	openmsg(ulong);
int		recvmsg(Msgchan*, void**);
int		sendmsg(Msgchan*, void*, int);
int		setdevicestate(uint, int);
void		setmousepoint(Point);
void		setsnarflength(uint);
void		setsnarfpiece(uint);
int		getversion(void);
void		setguistate(uint);
uint		getguistate(void);
uint		copystep(uint);
void		gettime(uint*, uint*, uint*);
void		stopcatchup(void);

extern	jmp_buf	backdoorjmp;
