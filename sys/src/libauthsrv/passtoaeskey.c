#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <libsec.h>

int
passtoaeskey(char *key, char *p)
{
	static char salt[] = "Plan 9 key derivation";
	pbkdf2_x((uchar*)p, strlen(p), (uchar*)salt, sizeof(salt)-1, 9001, (uchar *)key, AESKEYLEN, hmac_sha1, SHA1dlen);
	return 0;
}
