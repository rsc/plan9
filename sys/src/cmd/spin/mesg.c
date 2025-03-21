/***** spin: mesg.c *****/

/*
 * This file is part of the public release of Spin. It is subject to the
 * terms in the LICENSE file that is included in this source directory.
 * Tool documentation is available at http://spinroot.com
 */

#include <stdlib.h>
#include <assert.h>
#include "spin.h"
#include "y.tab.h"

#ifndef MAXQ
#define MAXQ	2500		/* default max # queues  */
#endif

extern RunList	*X_lst;
extern Symbol	*Fname;
extern int	verbose, TstOnly, s_trail, analyze, columns;
extern int	lineno, depth, xspin, m_loss, jumpsteps;
extern int	nproc, nstop;
extern short	Have_claim;

QH	*qh_lst;
Queue	*qtab = (Queue *) 0;	/* linked list of queues */
Queue	*ltab[MAXQ];		/* linear list of queues */
int	nrqs = 0, firstrow = 1, has_stdin = 0;
char	GBuf[4096];

static Lextok	*n_rem = (Lextok *) 0;
static Queue	*q_rem = (Queue  *) 0;

static int	a_rcv(Queue *, Lextok *, int);
static int	a_snd(Queue *, Lextok *);
static int	sa_snd(Queue *, Lextok *);
static int	s_snd(Queue *, Lextok *);
extern Lextok	**find_mtype_list(const char *);
extern char	*which_mtype(const char *);
extern void	sr_buf(int, int, const char *);
extern void	sr_mesg(FILE *, int, int, const char *);
extern void	putarrow(int, int);
static void	sr_talk(Lextok *, int, char *, char *, int, Queue *);

int
cnt_mpars(Lextok *n)
{	Lextok *m;
	int i=0;

	for (m = n; m; m = m->rgt)
		i += Cnt_flds(m);
	return i;
}

int
qmake(Symbol *s)
{	Lextok *m;
	Queue *q;
	int i, j;

	if (!s->ini)
		return 0;

	if (nrqs >= MAXQ)
	{	lineno = s->ini->ln;
		Fname  = s->ini->fn;
		fatal("too many queues (%s)", s->name);
	}
	if (analyze && nrqs >= 255)
	{	fatal("too many channel types", (char *)0);
	}

	if (s->ini->ntyp != CHAN)
		return eval(s->ini);

	q = (Queue *) emalloc(sizeof(Queue));
	q->qid    = (short) ++nrqs;
	q->nslots = s->ini->val;
	q->nflds  = cnt_mpars(s->ini->rgt);
	q->setat  = depth;

	i = max(1, q->nslots);	/* 0-slot qs get 1 slot minimum */
	j = q->nflds * i;

	q->contents  = (int *) emalloc(j*sizeof(int));
	q->fld_width = (int *) emalloc(q->nflds*sizeof(int));
	q->mtp       = (char **) emalloc(q->nflds*sizeof(char *));
	q->stepnr    = (int *) emalloc(i*sizeof(int));

	for (m = s->ini->rgt, i = 0; m; m = m->rgt)
	{	if (m->sym && m->ntyp == STRUCT)
		{	i = Width_set(q->fld_width, i, getuname(m->sym));
		} else
		{	if (m->sym)
			{	q->mtp[i] = m->sym->name;
			}
			q->fld_width[i++] = m->ntyp;
	}	}
	q->nxt = qtab;
	qtab = q;
	ltab[q->qid-1] = q;

	return q->qid;
}

int
qfull(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
		return (ltab[whichq]->qlen >= ltab[whichq]->nslots);
	return 0;
}

int
qlen(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
		return ltab[whichq]->qlen;
	return 0;
}

int
q_is_sync(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
		return (ltab[whichq]->nslots == 0);
	return 0;
}

int
qsend(Lextok *n)
{	int whichq = eval(n->lft)-1;

	if (whichq == -1)
	{	printf("Error: sending to an uninitialized chan\n");
		/* whichq = 0; */
		return 0;
	}
	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
	{	ltab[whichq]->setat = depth;
		if (ltab[whichq]->nslots > 0)
		{	return a_snd(ltab[whichq], n);;
		} else
		{	return s_snd(ltab[whichq], n);
	}	}
	return 0;
}

#ifndef PC
 #include <termios.h>
 static struct termios initial_settings, new_settings;

 void
 peek_ch_init(void)
 {
	tcgetattr(0,&initial_settings);

	new_settings = initial_settings;
	new_settings.c_lflag &= ~ICANON;
	new_settings.c_lflag &= ~ECHO;
	new_settings.c_lflag &= ~ISIG;
	new_settings.c_cc[VMIN] = 0;
	new_settings.c_cc[VTIME] = 0;
 }

 int
 peek_ch(void)
 {	int n;

	has_stdin = 1;

	tcsetattr(0, TCSANOW, &new_settings);
	n = getchar();
	tcsetattr(0, TCSANOW, &initial_settings);

	return n;
 }
#endif

int
qrecv(Lextok *n, int full)
{	int whichq = eval(n->lft)-1;

	if (whichq == -1)
	{	if (n->sym && !strcmp(n->sym->name, "STDIN"))
		{	Lextok *m;
#ifndef PC
			static int did_once = 0;
			if (!did_once) /* 6.2.4 */
			{	peek_ch_init();
				did_once = 1;
			}
#endif
			if (TstOnly) return 1;

			for (m = n->rgt; m; m = m->rgt)
			if (m->lft->ntyp != CONST && m->lft->ntyp != EVAL)
			{
#ifdef PC
				int c = getchar();
#else
				int c = peek_ch();	/* 6.2.4, was getchar(); */
#endif
				if (c == 27 || c == 3)	/* escape or control-c */
				{	printf("quit\n");
					exit(0);
				} /* else: non-blocking */
				if (c == EOF) return 0;	/* no char available */
				(void) setval(m->lft, c);
			} else
			{	fatal("invalid use of STDIN", (char *)0);
			}
			return 1;
		}
		printf("Error: receiving from an uninitialized chan %s\n",
			n->sym?n->sym->name:"");
		/* whichq = 0; */
		return 0;
	}
	if (whichq < MAXQ && whichq >= 0 && ltab[whichq])
	{	ltab[whichq]->setat = depth;
		return a_rcv(ltab[whichq], n, full);
	}
	return 0;
}

static int
sa_snd(Queue *q, Lextok *n)	/* sorted asynchronous */
{	Lextok *m;
	int i, j, k;
	int New, Old;

	for (i = 0; i < q->qlen; i++)
	for (j = 0, m = n->rgt; m && j < q->nflds; m = m->rgt, j++)
	{	New = cast_val(q->fld_width[j], eval(m->lft), 0);
		Old = q->contents[i*q->nflds+j];
		if (New == Old)
			continue;
		if (New >  Old)
			break;	/* inner loop */
		goto found;	/* New < Old */
	}
found:
	for (j = q->qlen-1; j >= i; j--)
	for (k = 0; k < q->nflds; k++)
	{	q->contents[(j+1)*q->nflds+k] =
			q->contents[j*q->nflds+k];	/* shift up */
		if (k == 0)
			q->stepnr[j+1] = q->stepnr[j];
	}
	return i*q->nflds;				/* new q offset */
}

void
typ_ck(int ft, int at, char *s)
{
	if ((verbose&32) && ft != at
	&& (ft == CHAN || at == CHAN)
	&& (at != PREDEF || strcmp(s, "recv") != 0))
	{	char buf[256], tag1[64], tag2[64];
		(void) sputtype(tag1, ft);
		(void) sputtype(tag2, at);
		sprintf(buf, "type-clash in %s, (%s<-> %s)", s, tag1, tag2);
		non_fatal("%s", buf);
	}
}

static void
mtype_ck(char *p, Lextok *arg)
{	char *t, *s = p?p:"_unnamed_";

	if (!arg
	||  !arg->sym)
	{	return;
	}

	switch (arg->ntyp) {
	case NAME:
		if (arg->sym->mtype_name)
		{	t = arg->sym->mtype_name->name;
		} else
		{	t = "_unnamed_";
		}
		break;
	case CONST:
		t = which_mtype(arg->sym->name);
		break;
	default:
		t = "expression";
		break;
	}

	if (strcmp(s, t) != 0)
	{	printf("spin: %s:%d, Error: '%s' is type '%s', but ",
			arg->fn?arg->fn->name:"", arg->ln,
			arg->sym->name, t);
		printf("should be type '%s'\n", s);
		non_fatal("incorrect type of '%s'", arg->sym->name);
	}
}

static int
a_snd(Queue *q, Lextok *n)
{	Lextok *m;
	int i = q->qlen*q->nflds;	/* q offset */
	int j = 0;			/* q field# */

	if (q->nslots > 0 && q->qlen >= q->nslots)
	{	return m_loss;	/* q is full */
	}

	if (TstOnly)
	{	return 1;
	}
	if (n->val) i = sa_snd(q, n);	/* sorted insert */

	q->stepnr[i/q->nflds] = depth;

	for (m = n->rgt; m && j < q->nflds; m = m->rgt, j++)
	{	int New = eval(m->lft);
		q->contents[i+j] = cast_val(q->fld_width[j], New, 0);

		if (q->fld_width[i+j] == MTYPE)
		{	mtype_ck(q->mtp[i+j], m->lft);	/* 6.4.8 */
		}
		if ((verbose&16) && depth >= jumpsteps)
		{	sr_talk(n, New, "Send ", "->", j, q); /* XXX j was i+j in 6.4.8 */
		}
		typ_ck(q->fld_width[i+j], Sym_typ(m->lft), "send");
	}

	if ((verbose&16) && depth >= jumpsteps)
	{	for (i = j; i < q->nflds; i++)
		{	sr_talk(n, 0, "Send ", "->", i, q);
		}
		if (j < q->nflds)
		{	printf("%3d: warning: missing params in send\n",
				depth);
		}
		if (m)
		{	printf("%3d: warning: too many params in send\n",
				depth);
	}	}
	q->qlen++;
	return 1;
}

static int
a_rcv(Queue *q, Lextok *n, int full)
{	Lextok *m;
	int i=0, oi, j, k;
	extern int Rvous;

	if (q->qlen == 0)
	{	return 0;	/* q is empty */
	}
try_slot:
	/* test executability */
	for (m = n->rgt, j=0; m && j < q->nflds; m = m->rgt, j++)
	{
		if (q->fld_width[i*q->nflds+j] == MTYPE)
		{	mtype_ck(q->mtp[i*q->nflds+j], m->lft);	/* 6.4.8 */
		}

		if (m->lft->ntyp == CONST
		&&  q->contents[i*q->nflds+j] != m->lft->val)
		{
			if (n->val == 0		/* fifo recv */
			||  n->val == 2		/* fifo poll */
			|| ++i >= q->qlen)	/* last slot */
			{	return 0;	/* no match  */
			}
			goto try_slot;		/* random recv */
		}

		if (m->lft->ntyp == EVAL)
		{	Lextok *fix = m->lft->lft;

			if (fix->ntyp == ',')	/* new, usertype7 */
			{	do {
					assert(j < q->nflds);
					if (q->contents[i*q->nflds+j] != eval(fix->lft))
					{	if (n->val == 0
						||  n->val == 2
						||  ++i >= q->qlen)
						{	return 0;
						}
						goto try_slot;	/* random recv */
					}
					j++;
					fix = fix->rgt;
				} while (fix && fix->ntyp == ',');
				j--;
			} else
			{	if (q->contents[i*q->nflds+j] != eval(fix))
				{	if (n->val == 0		/* fifo recv */
					||  n->val == 2		/* fifo poll */
					|| ++i >= q->qlen)	/* last slot */
					{	return 0;	/* no match  */
					}
					goto try_slot;		/* random recv */
		}	}	}
	}

	if (TstOnly) return 1;

	if (verbose&8)
	{	if (j < q->nflds)
		{	printf("%3d: warning: missing params in next recv\n",
				depth);
		} else if (m)
		{	printf("%3d: warning: too many params in next recv\n",
				depth);
	}	}

	/* set the fields */
	if (Rvous)
	{	n_rem = n;
		q_rem = q;
	}

	oi = q->stepnr[i];
	for (m = n->rgt, j = 0; m && j < q->nflds; m = m->rgt, j++)
	{	if (columns && !full)	/* was columns == 1 */
			continue;
		if ((verbose&8) && !Rvous && depth >= jumpsteps)
		{	sr_talk(n, q->contents[i*q->nflds+j],
			(full && n->val < 2)?"Recv ":"[Recv] ", "<-", j, q);
		}
		if (!full)
			continue;	/* test */
		if (m && m->lft->ntyp != CONST && m->lft->ntyp != EVAL)
		{	(void) setval(m->lft, q->contents[i*q->nflds+j]);
			typ_ck(q->fld_width[j], Sym_typ(m->lft), "recv");
		}
		if (n->val < 2)		/* not a poll */
		for (k = i; k < q->qlen-1; k++)
		{	q->contents[k*q->nflds+j] =
			  q->contents[(k+1)*q->nflds+j];
			if (j == 0)
			  q->stepnr[k] = q->stepnr[k+1];
		}
	}

	if ((!columns || full)
	&& (verbose&8) && !Rvous && depth >= jumpsteps)
	for (i = j; i < q->nflds; i++)
	{	sr_talk(n, 0,
		(full && n->val < 2)?"Recv ":"[Recv] ", "<-", i, q);
	}
	if (columns == 2 && full && !Rvous && depth >= jumpsteps)
		putarrow(oi, depth);

	if (full && n->val < 2)
		q->qlen--;
	return 1;
}

static int
s_snd(Queue *q, Lextok *n)
{	Lextok *m;
	RunList *rX, *sX = X_lst;	/* rX=recvr, sX=sendr */
	int i, j = 0;	/* q field# */

	for (m = n->rgt; m && j < q->nflds; m = m->rgt, j++)
	{	q->contents[j] = cast_val(q->fld_width[j], eval(m->lft), 0);
		typ_ck(q->fld_width[j], Sym_typ(m->lft), "rv-send");

		if (q->fld_width[j] == MTYPE)
		{	mtype_ck(q->mtp[j], m->lft);	/* 6.4.8 */
	}	}

	q->qlen = 1;
	if (!complete_rendez())
	{	q->qlen = 0;
		return 0;
	}
	if (TstOnly)
	{	q->qlen = 0;
		return 1;
	}
	q->stepnr[0] = depth;
	if ((verbose&16) && depth >= jumpsteps)
	{	m = n->rgt;
		rX = X_lst; X_lst = sX;

		for (j = 0; m && j < q->nflds; m = m->rgt, j++)
		{	sr_talk(n, eval(m->lft), "Sent ", "->", j, q);
		}

		for (i = j; i < q->nflds; i++)
		{	sr_talk(n, 0, "Sent ", "->", i, q);
		}

		if (j < q->nflds)
		{	  printf("%3d: warning: missing params in rv-send\n",
				depth);
		} else if (m)
		{	  printf("%3d: warning: too many params in rv-send\n",
				depth);
		}

		X_lst = rX;	/* restore receiver's context */
		if (!s_trail)
		{	if (!n_rem || !q_rem)
				fatal("cannot happen, s_snd", (char *) 0);
			m = n_rem->rgt;
			for (j = 0; m && j < q->nflds; m = m->rgt, j++)
			{
				if (q->fld_width[j] == MTYPE)
				{	mtype_ck(q->mtp[j], m->lft);	/* 6.4.8 */
				}

				if (m->lft->ntyp != NAME
				||  strcmp(m->lft->sym->name, "_") != 0)
				{	i = eval(m->lft);
				} else
				{	i = 0;
				}
				if (verbose&8)
				sr_talk(n_rem,i,"Recv ","<-",j,q_rem);
			}
			if (verbose&8)
			for (i = j; i < q->nflds; i++)
				sr_talk(n_rem, 0, "Recv ", "<-", j, q_rem);
			if (columns == 2)
				putarrow(depth, depth);
		}
		n_rem = (Lextok *) 0;
		q_rem = (Queue *) 0;
	}
	return 1;
}

static void
channm(Lextok *n)
{	char lbuf[512];

	if (n->sym->type == CHAN)
		strcat(GBuf, n->sym->name);
	else if (n->sym->type == NAME)
		strcat(GBuf, lookup(n->sym->name)->name);
	else if (n->sym->type == STRUCT)
	{	Symbol *r = n->sym;
		if (r->context)
		{	r = findloc(r);
			if (!r)
			{	strcat(GBuf, "*?*");
				return;
		}	}
		ini_struct(r);
		printf("%s", r->name);
		strcpy(lbuf, "");
		struct_name(n->lft, r, 1, lbuf);
		strcat(GBuf, lbuf);
	} else
		strcat(GBuf, "-");
	if (n->lft->lft)
	{	sprintf(lbuf, "[%d]", eval(n->lft->lft));
		strcat(GBuf, lbuf);
	}
}

static void
difcolumns(Lextok *n, char *tr, int v, int j, Queue *q)
{	extern int prno;

	if (j == 0)
	{	GBuf[0] = '\0';
		channm(n);
		strcat(GBuf, (strncmp(tr, "Sen", 3))?"?":"!");
	} else
		strcat(GBuf, ",");
	if (tr[0] == '[') strcat(GBuf, "[");
	sr_buf(v, q->fld_width[j] == MTYPE, q->mtp[j]);
	if (j == q->nflds - 1)
	{	int cnr;
		if (s_trail)
		{	cnr = prno - Have_claim;
		} else
		{	cnr = X_lst?X_lst->pid - Have_claim:0;
		}
		if (tr[0] == '[') strcat(GBuf, "]");
		pstext(cnr, GBuf);
	}
}

static void
docolumns(Lextok *n, char *tr, int v, int j, Queue *q)
{	int i;

	if (firstrow)
	{	printf("q\\p");
		for (i = 0; i < nproc-nstop - Have_claim; i++)
			printf(" %3d", i);
		printf("\n");
		firstrow = 0;
	}
	if (j == 0)
	{	printf("%3d", q->qid);
		if (X_lst)
		for (i = 0; i < X_lst->pid - Have_claim; i++)
			printf("   .");
		printf("   ");
		GBuf[0] = '\0';
		channm(n);
		printf("%s%c", GBuf, (strncmp(tr, "Sen", 3))?'?':'!');
	} else
		printf(",");
	if (tr[0] == '[') printf("[");
	sr_mesg(stdout, v, q->fld_width[j] == MTYPE, q->mtp[j]);
	if (j == q->nflds - 1)
	{	if (tr[0] == '[') printf("]");
		printf("\n");
	}
}

void
qhide(int q)
{	QH *p = (QH *) emalloc(sizeof(QH));
	p->n = q;
	p->nxt = qh_lst;
	qh_lst = p;
}

int
qishidden(int q)
{	QH *p;
	for (p = qh_lst; p; p = p->nxt)
		if (p->n == q)
			return 1;
	return 0;
}

static void
sr_talk(Lextok *n, int v, char *tr, char *a, int j, Queue *q)
{	char s[128];

	if (qishidden(eval(n->lft)))
		return;

	if (columns)
	{	if (columns == 2)
			difcolumns(n, tr, v, j, q);
		else
			docolumns(n, tr, v, j, q);
		return;
	}
	if (xspin)
	{	if ((verbose&4) && tr[0] != '[')
		sprintf(s, "(state -)\t[values: %d",
			eval(n->lft));
		else
		sprintf(s, "(state -)\t[%d", eval(n->lft));
		if (strncmp(tr, "Sen", 3) == 0)
			strcat(s, "!");
		else
			strcat(s, "?");
	} else
	{	strcpy(s, tr);
	}

	if (j == 0)
	{	char snm[128];
		whoruns(1);
		{	char *ptr = n->fn->name;
			char *qtr = snm;
			while (*ptr != '\0')
			{	if (*ptr != '\"')
				{	*qtr++ = *ptr;
				}
				ptr++;
			}
			*qtr = '\0';
			printf("%s:%d %s",
				snm, n->ln, s);
		}
	} else
	{	printf(",");
	}
	sr_mesg(stdout, v, q->fld_width[j] == MTYPE, q->mtp[j]);

	if (j == q->nflds - 1)
	{	if (xspin)
		{	printf("]\n");
			if (!(verbose&4)) printf("\n");
			return;
		}
		printf("\t%s queue %d (", a, eval(n->lft));
		GBuf[0] = '\0';
		channm(n);
		printf("%s)\n", GBuf);
	}
	fflush(stdout);
}

void
sr_buf(int v, int j, const char *s)
{	int cnt = 1; Lextok *n;
	char lbuf[512];
	Lextok *Mtype = ZN;

	if (j)
	{	Mtype = *find_mtype_list(s?s:"_unnamed_");
	}
	for (n = Mtype; n && j; n = n->rgt, cnt++)
	{	if (cnt == v)
		{	if(strlen(n->lft->sym->name) >= sizeof(lbuf))
			{	non_fatal("mtype name %s too long", n->lft->sym->name);
				break;
			}
			sprintf(lbuf, "%s", n->lft->sym->name);
			strcat(GBuf, lbuf);
			return;
	}	}
	sprintf(lbuf, "%d", v);
	strcat(GBuf, lbuf);
}

void
sr_mesg(FILE *fd, int v, int j, const char *s)
{	GBuf[0] ='\0';

	sr_buf(v, j, s);
	fprintf(fd, GBuf, (char *) 0); /* prevent compiler warning */
}

void
doq(Symbol *s, int n, RunList *r)
{	Queue *q;
	int j, k;

	if (!s->val)	/* uninitialized queue */
		return;
	for (q = qtab; q; q = q->nxt)
	if (q->qid == s->val[n])
	{	if (xspin > 0
		&& (verbose&4)
		&& q->setat < depth)
		{	continue;
		}
		if (q->nslots == 0)
		{	continue;	/* rv q always empty */
		}

		printf("\t\tqueue %d (", q->qid);
		if (r)
		{	printf("%s(%d):", r->n->name, r->pid - Have_claim);
		}

		if (s->nel > 1 || s->isarray)
		{	printf("%s[%d]): ", s->name, n);
		} else
		{	printf("%s): ", s->name);
		}

		for (k = 0; k < q->qlen; k++)
		{	printf("[");
			for (j = 0; j < q->nflds; j++)
			{	if (j > 0) printf(",");
				sr_mesg(stdout,
					q->contents[k*q->nflds+j],
					q->fld_width[j] == MTYPE,
					q->mtp[j]);
			}
			printf("]");
		}
		printf("\n");
		break;
	}
}

void
nochan_manip(Lextok *p, Lextok *n, int d)	/* p=lhs n=rhs */
{	int e = 1;

	if (!n
	||  !p
	||  !p->sym
	||   p->sym->type == STRUCT)
	{	/* if a struct, assignments to structure fields arent checked yet */
		return;
	}

	if (d == 0 && p->sym && p->sym->type == CHAN)
	{	setaccess(p->sym, ZS, 0, 'L');

		if (n && n->ntyp == CONST)
			fatal("invalid asgn to chan", (char *) 0);

		if (n && n->sym && n->sym->type == CHAN)
		{	setaccess(n->sym, ZS, 0, 'V');
			return;
		}
	}

	if (!d && n && n->ismtyp)	/* rhs is an mtype value (a constant) */
	{	char *lhs = "_unnamed_", *rhs = "_unnamed_";

		if (p->sym)
		{	lhs = p->sym->mtype_name?p->sym->mtype_name->name:"_unnamed_";
		}
		if (n->sym)
		{	rhs = which_mtype(n->sym->name); /* only for constants */
		}

		if (p->sym && !p->sym->mtype_name && n->sym)
		{	p->sym->mtype_name = (Symbol *) emalloc(sizeof(Symbol));
			p->sym->mtype_name->name = rhs;
		} else if (strcmp(lhs, rhs) != 0)
		{	fprintf(stderr, "spin: %s:%d, Error: '%s' is type '%s' but '%s' is type '%s'\n",
				p->fn->name, p->ln,
				p->sym?p->sym->name:"?", lhs,
				n->sym?n->sym->name:"?", rhs);
			non_fatal("type error", (char *) 0);
	}	}

	/* ok on the rhs of an assignment: */
	if (!n
	||  n->ntyp == LEN   || n->ntyp == RUN
	||  n->ntyp == FULL  || n->ntyp == NFULL
	||  n->ntyp == EMPTY || n->ntyp == NEMPTY
	||  n->ntyp == 'R')
		return;

	if (n->sym && n->sym->type == CHAN)
	{	if (d == 1)
			fatal("invalid use of chan name", (char *) 0);
		else
			setaccess(n->sym, ZS, 0, 'V');
	}

	if (n->ntyp == NAME
	||  n->ntyp == '.')
	{	e = 0;	/* array index or struct element */
	}

	nochan_manip(p, n->lft, e);
	nochan_manip(p, n->rgt, 1);
}

typedef struct BaseName {
	char *str;
	int cnt;
	struct BaseName *nxt;
} BaseName;

static BaseName *bsn;

void
newbasename(char *s)
{	BaseName *b;

/*	printf("+++++++++%s\n", s);	*/
	for (b = bsn; b; b = b->nxt)
		if (strcmp(b->str, s) == 0)
		{	b->cnt++;
			return;
		}
	b = (BaseName *) emalloc(sizeof(BaseName));
	b->str = emalloc(strlen(s)+1);
	b->cnt = 1;
	strcpy(b->str, s);
	b->nxt = bsn;
	bsn = b;
}

void
delbasename(char *s)
{	BaseName *b, *prv = (BaseName *) 0;

	for (b = bsn; b; prv = b, b = b->nxt)
	{	if (strcmp(b->str, s) == 0)
		{	b->cnt--;
			if (b->cnt == 0)
			{	if (prv)
				{	prv->nxt = b->nxt;
				} else
				{	bsn = b->nxt;
			}	}
/*	printf("---------%s\n", s);	*/
			break;
	}	}
}

void
checkindex(char *s, char *t)
{	BaseName *b;

/*	printf("xxx Check %s (%s)\n", s, t);	*/
	for (b = bsn; b; b = b->nxt)
	{
/*		printf("	%s\n", b->str);	*/
		if (strcmp(b->str, s) == 0)
		{	non_fatal("do not index an array with itself (%s)", t);
			break;
	}	}
}

void
scan_tree(Lextok *t, char *mn, char *mx)
{	char sv[512];
	char tmp[32];
	int oln = lineno;

	if (!t) return;

	lineno = t->ln;

	if (t->ntyp == NAME)
	{	if (strlen(t->sym->name) + strlen(mn) > 256) // conservative
		{	fatal("name too long", t->sym->name);
		}

		strcat(mn, t->sym->name);
		strcat(mx, t->sym->name);
		if (t->lft)		/* array index */
		{	strcat(mn, "[]");
			newbasename(mn);
				strcpy(sv, mn);		/* save */
				strcpy(mn, "");		/* clear */
				strcat(mx, "[");
				scan_tree(t->lft, mn, mx);	/* index */
				strcat(mx, "]");
				checkindex(mn, mx);	/* match against basenames */
				strcpy(mn, sv);		/* restore */
			delbasename(mn);
		}
		if (t->rgt)	/* structure element */
		{	scan_tree(t->rgt, mn, mx);
		}
	} else if (t->ntyp == CONST)
	{	strcat(mn, "1"); /* really: t->val */
		sprintf(tmp, "%d", t->val);
		strcat(mx, tmp);
	} else if (t->ntyp == '.')
	{	strcat(mn, ".");
		strcat(mx, ".");
		scan_tree(t->lft, mn, mx);
	} else
	{	strcat(mn, "??");
		strcat(mx, "??");
	}
	lineno = oln;
}

void
no_nested_array_refs(Lextok *n)	/* a [ a[1] ] with a[1] = 1, causes trouble in pan.b */
{	char mn[512];
	char mx[512];

/*	printf("==================================ZAP\n");	*/
	bsn = (BaseName *) 0;	/* start new list */
	strcpy(mn, "");
	strcpy(mx, "");

	scan_tree(n, mn, mx);
/*	printf("==> %s\n", mn);	*/
}

void
no_internals(Lextok *n)
{	char *sp;

	if (!n->sym
	||  !n->sym->name)
		return;

	sp = n->sym->name;

	if ((strlen(sp) == strlen("_nr_pr") && strcmp(sp, "_nr_pr") == 0)
	||  (strlen(sp) == strlen("_pid") && strcmp(sp, "_pid") == 0)
	||  (strlen(sp) == strlen("_p") && strcmp(sp, "_p") == 0))
	{	fatal("invalid assignment to %s", sp);
	}

	no_nested_array_refs(n);
}
