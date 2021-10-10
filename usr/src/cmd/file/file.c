/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#define	_LARGEFILE64_SOURCE

#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <libelf.h>
#include <stdlib.h>
#include <limits.h>
#include <locale.h>
#include <wctype.h>
#include <string.h>
#include <errno.h>
#include <door.h>
#include <sys/types.h>
#include <sys/mkdev.h>
#include <sys/stat.h>
#include <procfs.h>
#include <sys/core.h>
#include <sys/dumphdr.h>
#include <elf_file.h>
#include <netinet/in.h>
#include <inttypes.h>
#include "file.h"

/*
 *	Misc
 */

#define	FBSZ		512
#define	MLIST_SZ	12

#if ELF_FILE_FBUF_MIN > FBSZ
#error "FBSZ too small for _elf_file()"
#endif

/*
 * The 0x8FCA0102 magic string was used in crash dumps generated by releases
 * prior to Solaris 7.
 */
#define	OLD_DUMP_MAGIC	0x8FCA0102

#define	SNOOP_MAGIC	(0x736e6f6f70000000ULL)

#if defined(__sparc)
#define	NATIVE_ISA	"SPARC"
#define	OTHER_ISA	"Intel"
#else
#define	NATIVE_ISA	"Intel"
#define	OTHER_ISA	"SPARC"
#endif

/* Assembly language comment char */
#ifdef pdp11
#define	ASCOMCHAR '/'
#else
#define	ASCOMCHAR '!'
#endif

#pragma	align	16(fbuf)
static char	fbuf[FBSZ];

/*
 * Magic file variables
 */
static intmax_t maxmagicoffset;
static intmax_t tmpmax;
static char	*magicbuf;

static char	*dfile;
static char	*troff[] = {	/* new troff intermediate lang */
		"x", "T", "res", "init", "font", "202", "V0", "p1", 0};

static char	*fort[] = {			/* FORTRAN */
		"function", "subroutine", "common", "dimension", "block",
		"integer", "real", "data", "double",
		"FUNCTION", "SUBROUTINE", "COMMON", "DIMENSION", "BLOCK",
		"INTEGER", "REAL", "DATA", "DOUBLE", 0};

static char	*asc[] = {		/* Assembler Commands */
		"sys", "mov", "tst", "clr", "jmp", "cmp", "set", "inc",
		"dec", 0};

static char	*c[] = {			/* C Language */
		"int", "char", "float", "double", "short", "long", "unsigned",
		"register", "static", "struct", "extern", 0};

static char	*as[] = {	/* Assembler Pseudo Ops, prepended with '.' */
		"globl", "global", "ident", "file", "byte", "even",
		"text", "data", "bss", "comm", 0};

/* start for MB env */
static wchar_t	wchar;
static int	length;
static int	IS_ascii;
static int	Max;
/* end for MB env */
static int	i;	/* global index into first 'fbsz' bytes of file */
static int	fbsz;
static int	ifd = -1;
static int	tret;
static int	hflg;
static int	dflg;
static int	mflg;
static int	M_flg;
static int	iflg;
static struct stat64	mbuf;

static char	**mlist1;	/* 1st ordered list of magic files */
static char	**mlist2;	/* 2nd ordered list of magic files */
static size_t	mlist1_sz;	/* number of ptrs allocated for mlist1 */
static size_t	mlist2_sz;	/* number of ptrs allocated for mlist2 */
static char	**mlist1p;	/* next entry in mlist1 */
static char	**mlist2p;	/* next entry in mlist2 */

static ssize_t	mread;

static int type(char *file);
static int def_position_tests(char *file);
static void def_context_tests(void);
static int troffint(char *bp, int n);
static int lookup(char **tab);
static int ccom(void);
static int ascom(void);
static int sccs(void);
static int english(char *bp, int n);
static int shellscript(char buf[], struct stat64 *sb);
static int get_door_target(char *, char *, size_t);
static int zipfile(char *, int);
static int is_crash_dump(const char *, int);
static int is_snoop_file(const char *buf);
static void print_dumphdr(const int, const dumphdr_t *, uint32_t (*)(uint32_t),
    const char *);
static uint32_t swap_uint32(uint32_t);
static uint32_t return_uint32(uint32_t);
static void usage(void);
static void default_magic(void);
static void add_to_mlist(char *, int);
static void fd_cleanup(void);

#ifdef XPG4
	/* SUSv3 requires a single <space> after the colon */
#define	prf(x)	(void) printf("%s: ", x);
#define	IS_XPG4 1
#else	/* !XPG4 */
#define	prf(x)	(void) printf("%s:%s", x, (int)strlen(x) > 6 ? "\t" : "\t\t");
#define	IS_XPG4 0
#endif	/* XPG4 */

/*
 * Static program identifier - used to prevent localization of the name "file"
 * within individual error messages.
 */
const char *File = "file";

int
main(int argc, char **argv)
{
	char	*p;
	int	ch;
	FILE	*fl;
	int	cflg = 0;
	int	eflg = 0;
	int	fflg = 0;
	char	*ap = NULL;
	int	pathlen;
	char	**filep;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((ch = getopt(argc, argv, "M:cdf:him:")) != EOF) {
		switch (ch) {

		case 'M':
			add_to_mlist(optarg, !dflg);
			M_flg++;
			break;

		case 'c':
			cflg++;
			break;

		case 'd':
			if (!dflg) {
				default_magic();
				add_to_mlist(dfile, 0);
				dflg++;
			}
			break;

		case 'f':
			fflg++;
			errno = 0;
			if ((fl = fopen(optarg, "r")) == NULL) {
				int err = errno;
				(void) fprintf(stderr, gettext("%s: cannot "
				    "open file %s: %s\n"), File, optarg,
				    err ? strerror(err) : "");
				usage();
			}
			pathlen = pathconf("/", _PC_PATH_MAX);
			if (pathlen == -1) {
				int err = errno;
				(void) fprintf(stderr, gettext("%s: cannot "
				    "determine maximum path length: %s\n"),
				    File, strerror(err));
				exit(1);
			}
			pathlen += 2; /* for null and newline in fgets */
			if ((ap = malloc(pathlen * sizeof (char))) == NULL) {
				int err = errno;
				(void) fprintf(stderr, gettext("%s: malloc "
				    "failed: %s\n"), File, strerror(err));
				exit(2);
			}
			break;

		case 'h':
			hflg++;
			break;

		case 'i':
			iflg++;
			break;

		case 'm':
			add_to_mlist(optarg, !dflg);
			mflg++;
			break;

		case '?':
			eflg++;
			break;
		}
	}
	if (!cflg && !fflg && (eflg || optind == argc))
		usage();
	if (iflg && (dflg || mflg || M_flg)) {
		usage();
	}
	if (iflg && cflg) {
		usage();
	}

	if (!dflg && !mflg && !M_flg && !iflg) {
	/* no -d, -m, nor -M option; also -i option doesn't need magic  */
		default_magic();
		if (f_mkmtab(dfile, cflg, 0) == -1) {
			exit(2);
		}
	}

	else if (mflg && !M_flg && !dflg) {
	/* -m specified without -d nor -M */

#ifdef XPG4	/* For SUSv3 only */

		/*
		 * The default position-dependent magic file tests
		 * in /etc/magic will follow all the -m magic tests.
		 */

		for (filep = mlist1; filep < mlist1p; filep++) {
			if (f_mkmtab(*filep, cflg, 1) == -1) {
				exit(2);
			}
		}
		default_magic();
		if (f_mkmtab(dfile, cflg, 0) == -1) {
			exit(2);
		}
#else	/* !XPG4 */
		/*
		 * Retain Solaris file behavior for -m before SUSv3,
		 * when the new -d and -M options are not specified.
		 * Use the -m file specified in place of the default
		 * /etc/magic file.  Solaris file will
		 * now allow more than one magic file to be specified
		 * with multiple -m options, for consistency with
		 * other behavior.
		 *
		 * Put the magic table(s) specified by -m into
		 * the second magic table instead of the first
		 * (as indicated by the last argument to f_mkmtab()),
		 * since they replace the /etc/magic tests and
		 * must be executed alongside the default
		 * position-sensitive tests.
		 */

		for (filep = mlist1; filep < mlist1p; filep++) {
			if (f_mkmtab(*filep, cflg, 0) == -1) {
				exit(2);
			}
		}
#endif /* XPG4 */
	} else {
		/*
		 * For any other combination of -d, -m, and -M,
		 * use the magic files in command-line order.
		 * Store the entries from the two separate lists of magic
		 * files, if any, into two separate magic file tables.
		 * mlist1: magic tests executed before default magic tests
		 * mlist2: default magic tests and after
		 */
		for (filep = mlist1; filep && (filep < mlist1p); filep++) {
			if (f_mkmtab(*filep, cflg, 1) == -1) {
				exit(2);
			}
		}
		for (filep = mlist2; filep && (filep < mlist2p); filep++) {
			if (f_mkmtab(*filep, cflg, 0) == -1) {
				exit(2);
			}
		}
	}

	/* Initialize the magic file variables; check both magic tables */
	tmpmax = f_getmaxoffset(1);
	maxmagicoffset = f_getmaxoffset(0);
	if (maxmagicoffset < tmpmax) {
		maxmagicoffset = tmpmax;
	}
	if (maxmagicoffset < (intmax_t)FBSZ)
		maxmagicoffset = (intmax_t)FBSZ;
	if ((magicbuf = malloc(maxmagicoffset)) == NULL) {
		int err = errno;
		(void) fprintf(stderr, gettext("%s: malloc failed: %s\n"),
		    File, strerror(err));
		exit(2);
	}

	if (cflg) {
		f_prtmtab();
		if (ferror(stdout) != 0) {
			(void) fprintf(stderr, gettext("%s: error writing to "
			    "stdout\n"), File);
			exit(1);
		}
		if (fclose(stdout) != 0) {
			int err = errno;
			(void) fprintf(stderr, gettext("%s: fclose "
			    "failed: %s\n"), File, strerror(err));
			exit(1);
		}
		exit(0);
	}

	for (; fflg || optind < argc; optind += !fflg) {
		register int	l;

		if (fflg) {
			if ((p = fgets(ap, pathlen, fl)) == NULL) {
				fflg = 0;
				optind--;
				continue;
			}
			l = strlen(p);
			if (l > 0)
				p[l - 1] = '\0';
		} else
			p = argv[optind];
		prf(p);				/* print "file_name:<tab>" */

		if (type(p))
			tret = 1;
	}
	if (ap != NULL)
		free(ap);
	if (tret != 0)
		exit(tret);

	if (ferror(stdout) != 0) {
		(void) fprintf(stderr, gettext("%s: error writing to "
		    "stdout\n"), File);
		exit(1);
	}
	if (fclose(stdout) != 0) {
		int err = errno;
		(void) fprintf(stderr, gettext("%s: fclose failed: %s\n"),
		    File, strerror(err));
		exit(1);
	}
	return (0);
}

static int
type(char *file)
{
	int	cc;
	char	buf[BUFSIZ];
	int	(*statf)() = hflg ? lstat64 : stat64;

	i = 0;		/* reset index to beginning of file */
	ifd = -1;
	if ((*statf)(file, &mbuf) < 0) {
		if (statf == lstat64 || lstat64(file, &mbuf) < 0) {
			int err = errno;
			(void) printf(gettext("cannot open: %s\n"),
			    strerror(err));
			return (0);		/* POSIX.2 */
		}
	}
	switch (mbuf.st_mode & S_IFMT) {
	case S_IFREG:
		if (iflg) {
			(void) printf(gettext("regular file\n"));
			return (0);
		}
		break;
	case S_IFCHR:
		(void) printf(gettext("character"));
		goto spcl;

	case S_IFDIR:
		(void) printf(gettext("directory\n"));
		return (0);

	case S_IFIFO:
		(void) printf(gettext("fifo\n"));
		return (0);

	case S_IFLNK:
		if ((cc = readlink(file, buf, BUFSIZ)) < 0) {
			int err = errno;
			(void) printf(gettext("readlink error: %s\n"),
			    strerror(err));
			return (1);
		}
		buf[cc] = '\0';
		(void) printf(gettext("symbolic link to %s\n"), buf);
		return (0);

	case S_IFBLK:
		(void) printf(gettext("block"));
					/* major and minor, see sys/mkdev.h */
spcl:
		(void) printf(gettext(" special (%d/%d)\n"),
		    major(mbuf.st_rdev), minor(mbuf.st_rdev));
		return (0);

	case S_IFSOCK:
		(void) printf("socket\n");
		/* FIXME, should open and try to getsockname. */
		return (0);

	case S_IFDOOR:
		if (get_door_target(file, buf, sizeof (buf)) == 0)
			(void) printf(gettext("door to %s\n"), buf);
		else
			(void) printf(gettext("door\n"));
		return (0);

	}

	if (elf_version(EV_CURRENT) == EV_NONE) {
		(void) printf(gettext("libelf is out of date\n"));
		return (1);
	}

	ifd = open64(file, O_RDONLY);
	if (ifd < 0) {
		int err = errno;
		(void) printf(gettext("cannot open: %s\n"), strerror(err));
		return (0);			/* POSIX.2 */
	}

	if ((fbsz = pread(ifd, fbuf, FBSZ, 0)) == -1) {
		int err = errno;
		(void) printf(gettext("cannot read: %s\n"), strerror(err));
		(void) close(ifd);
		ifd = -1;
		return (0);			/* POSIX.2 */
	}
	if (fbsz == 0) {
		(void) printf(gettext("empty file\n"));
		fd_cleanup();
		return (0);
	}

	/*
	 * First try user-specified position-dependent magic tests, if any,
	 * which need to execute before the default tests.
	 */
	if ((mread = pread(ifd, (void*)magicbuf, (size_t)maxmagicoffset,
	    0)) == -1) {
		int err = errno;
		(void) printf(gettext("cannot read: %s\n"), strerror(err));
		fd_cleanup();
		return (0);
	}

	/*
	 * ChecK against Magic Table entries.
	 * Check first magic table for magic tests to be applied
	 * before default tests.
	 * If no default tests are to be applied, all magic tests
	 * should occur in this magic table.
	 */
	switch (f_ckmtab(magicbuf, mread, 1)) {
		case -1:	/* Error */
			exit(2);
			break;
		case 0:		/* Not magic */
			break;
		default:	/* Switch is magic index */
			(void) putchar('\n');
			fd_cleanup();
			return (0);
			/* NOTREACHED */
			break;
	}

	if (dflg || !M_flg) {
		/*
		 * default position-dependent tests,
		 * plus non-default magic tests, if any
		 */
		switch (def_position_tests(file)) {
			case -1:	/* error */
				fd_cleanup();
				return (1);
			case 1:	/* matching type found */
				fd_cleanup();
				return (0);
				/* NOTREACHED */
				break;
			case 0:		/* no matching type found */
				break;
		}
		/* default context-sensitive tests */
		def_context_tests();
	} else {
		/* no more tests to apply; no match was found */
		(void) printf(gettext("data\n"));
	}
	fd_cleanup();
	return (0);
}

/*
 * def_position_tests() - applies default position-sensitive tests,
 *	looking for values in specific positions in the file.
 *	These are followed by default (followed by possibly some
 *	non-default) magic file tests.
 *
 *	All position-sensitive tests, default or otherwise, must
 *	be applied before context-sensitive tests, to avoid
 *	false context-sensitive matches.
 *
 * 	Returns -1 on error which should result in error (non-zero)
 *	exit status for the file utility.
 *	Returns 0 if no matching file type found.
 *	Returns 1 if matching file type found.
 */

static int
def_position_tests(char *file)
{
	if (sccs()) {	/* look for "1hddddd" where d is a digit */
		(void) printf("sccs \n");
		return (1);
	}
	if (fbuf[0] == '#' && fbuf[1] == '!' && shellscript(fbuf+2, &mbuf))
		return (1);

	switch (_elf_file(File, file, IS_XPG4, ELF_FILE_AR_BASIC, ifd, fbuf,
	    fbsz)) {
	case ELF_FILE_SUCCESS:
		(void) putchar('\n');
		return (1);
	case ELF_FILE_FATAL:
		return (-1);
	}

	/* LINTED: pointer cast may result in improper alignment */
	if (*(int *)fbuf == CORE_MAGIC) {
		/* LINTED: pointer cast may result in improper alignment */
		struct core *corep = (struct core *)fbuf;

		(void) printf("a.out core file");

		if (*(corep->c_cmdname) != '\0')
			(void) printf(" from '%s'", corep->c_cmdname);
		(void) putchar('\n');
		return (1);
	}

	/*
	 * ZIP files, JAR files, and Java executables
	 */
	if (zipfile(fbuf, ifd))
		return (1);

	if (is_crash_dump(fbuf, ifd))
		return (1);
	if (fbsz >= (sizeof (long long) + sizeof (int)) && is_snoop_file(fbuf))
		return (1);

	/*
	 * ChecK against Magic Table entries.
	 * The magic entries checked here always start with default
	 * magic tests and may be followed by other, non-default magic
	 * tests.  If no default tests are to be executed, all the
	 * magic tests should have been in the first magic table.
	 */
	switch (f_ckmtab(magicbuf, mread, 0)) {
		case -1:	/* Error */
			exit(2);
			break;
		case 0:		/* Not magic */
			return (0);
			/* NOTREACHED */
			break;
		default:	/* Switch is magic index */

			(void) putchar('\n');
			return (1);
			/* NOTREACHED */
			break;
	}

	return (0);	/* file was not identified */
}

/*
 * def_context_tests() - default context-sensitive tests.
 *	These are the last tests to be applied.
 *	If no match is found, prints out "data".
 */

static void
def_context_tests(void)
{
	int	j;
	int	nl;
	char	ch;
	int	len;

	if (ccom() == 0)
		goto notc;
	while (fbuf[i] == '#') {
		j = i;
		while (fbuf[i++] != '\n') {
			if (i - j > 255) {
				(void) printf(gettext("data\n"));
				return;
			}
			if (i >= fbsz)
				goto notc;
		}
		if (ccom() == 0)
			goto notc;
	}
check:
	if (lookup(c) == 1) {
		while ((ch = fbuf[i]) != ';' && ch != '{') {
			if ((len = mblen(&fbuf[i], MB_CUR_MAX)) <= 0)
				len = 1;
			i += len;
			if (i >= fbsz)
				goto notc;
		}
		(void) printf(gettext("c program text"));
		goto outa;
	}
	nl = 0;
	while (fbuf[i] != '(') {
		if (fbuf[i] <= 0)
			goto notas;
		if (fbuf[i] == ';') {
			i++;
			goto check;
		}
		if (fbuf[i++] == '\n')
			if (nl++ > 6)
				goto notc;
		if (i >= fbsz)
			goto notc;
	}
	while (fbuf[i] != ')') {
		if (fbuf[i++] == '\n')
			if (nl++ > 6)
				goto notc;
		if (i >= fbsz)
			goto notc;
	}
	while (fbuf[i] != '{') {
		if ((len = mblen(&fbuf[i], MB_CUR_MAX)) <= 0)
			len = 1;
		if (fbuf[i] == '\n')
			if (nl++ > 6)
				goto notc;
		i += len;
		if (i >= fbsz)
			goto notc;
	}
	(void) printf(gettext("c program text"));
	goto outa;
notc:
	i = 0;			/* reset to begining of file again */
	while (fbuf[i] == 'c' || fbuf[i] == 'C'|| fbuf[i] == '!' ||
	    fbuf[i] == '*' || fbuf[i] == '\n') {
		while (fbuf[i++] != '\n')
			if (i >= fbsz)
				goto notfort;
	}
	if (lookup(fort) == 1) {
		(void) printf(gettext("fortran program text"));
		goto outa;
	}
notfort:			/* looking for assembler program */
	i = 0;			/* reset to beginning of file again */
	if (ccom() == 0)	/* assembler programs may contain */
				/* c-style comments */
		goto notas;
	if (ascom() == 0)
		goto notas;
	j = i - 1;
	if (fbuf[i] == '.') {
		i++;
		if (lookup(as) == 1) {
			(void) printf(gettext("assembler program text"));
			goto outa;
		} else if (j != -1 && fbuf[j] == '\n' && isalpha(fbuf[j + 2])) {
			(void) printf(
			    gettext("[nt]roff, tbl, or eqn input text"));
			goto outa;
		}
	}
	while (lookup(asc) == 0) {
		if (ccom() == 0)
			goto notas;
		if (ascom() == 0)
			goto notas;
		while (fbuf[i] != '\n' && fbuf[i++] != ':') {
			if (i >= fbsz)
				goto notas;
		}
		while (fbuf[i] == '\n' || fbuf[i] == ' ' || fbuf[i] == '\t')
			if (i++ >= fbsz)
				goto notas;
		j = i - 1;
		if (fbuf[i] == '.') {
			i++;
			if (lookup(as) == 1) {
				(void) printf(
				    gettext("assembler program text"));
				goto outa;
			} else if (fbuf[j] == '\n' && isalpha(fbuf[j+2])) {
				(void) printf(
				    gettext("[nt]roff, tbl, or eqn input "
				    "text"));
				goto outa;
			}
		}
	}
	(void) printf(gettext("assembler program text"));
	goto outa;
notas:
	/* start modification for multibyte env */
	IS_ascii = 1;
	if (fbsz < FBSZ)
		Max = fbsz;
	else
		Max = FBSZ - MB_LEN_MAX; /* prevent cut of wchar read */
	/* end modification for multibyte env */

	for (i = 0; i < Max; /* null */)
		if (fbuf[i] & 0200) {
			IS_ascii = 0;
			if (fbuf[0] == '\100' && fbuf[1] == '\357') {
				(void) printf(gettext("troff output\n"));
				return;
			}
		/* start modification for multibyte env */
			if ((length = mbtowc(&wchar, &fbuf[i], MB_CUR_MAX))
			    <= 0 || !iswprint(wchar)) {
				(void) printf(gettext("data\n"));
				return;
			}
			i += length;
		}
		else
			i++;
	i = fbsz;
		/* end modification for multibyte env */
	if (mbuf.st_mode&(S_IXUSR|S_IXGRP|S_IXOTH))
		(void) printf(gettext("commands text"));
	else if (troffint(fbuf, fbsz))
		(void) printf(gettext("troff intermediate output text"));
	else if (english(fbuf, fbsz))
		(void) printf(gettext("English text"));
	else if (IS_ascii)
		(void) printf(gettext("ascii text"));
	else
		(void) printf(gettext("text")); /* for multibyte env */
outa:
	/*
	 * This code is to make sure that no MB char is cut in half
	 * while still being used.
	 */
	fbsz = (fbsz < FBSZ ? fbsz : fbsz - MB_CUR_MAX + 1);
	while (i < fbsz) {
		if (isascii(fbuf[i])) {
			i++;
			continue;
		} else {
			if ((length = mbtowc(&wchar, &fbuf[i], MB_CUR_MAX))
			    <= 0 || !iswprint(wchar)) {
				(void) printf(gettext(" with garbage\n"));
				return;
			}
			i = i + length;
		}
	}
	(void) printf("\n");
}

static int
troffint(char *bp, int n)
{
	int k;

	i = 0;
	for (k = 0; k < 6; k++) {
		if (lookup(troff) == 0)
			return (0);
		if (lookup(troff) == 0)
			return (0);
		while (i < n && bp[i] != '\n')
			i++;
		if (i++ >= n)
			return (0);
	}
	return (1);
}

/*
 * lookup -
 * Attempts to match one of the strings from a list, 'tab',
 * with what is in the file, starting at the current index position 'i'.
 * Looks past any initial whitespace and expects whitespace or other
 * delimiting characters to follow the matched string.
 * A match identifies the file as being 'assembler', 'fortran', 'c', etc.
 * Returns 1 for a successful match, 0 otherwise.
 */
static int
lookup(char **tab)
{
	register char	r;
	register int	k, j, l;

	while (fbuf[i] == ' ' || fbuf[i] == '\t' || fbuf[i] == '\n')
		i++;
	for (j = 0; tab[j] != 0; j++) {
		l = 0;
		for (k = i; ((r = tab[j][l++]) == fbuf[k] && r != '\0'); k++)
			;
		if (r == '\0')
			if (fbuf[k] == ' ' || fbuf[k] == '\n' ||
			    fbuf[k] == '\t' || fbuf[k] == '{' ||
			    fbuf[k] == '/') {
				i = k;
				return (1);
			}
	}
	return (0);
}

/*
 * ccom -
 * Increments the current index 'i' into the file buffer 'fbuf' past any
 * whitespace lines and C-style comments found, starting at the current
 * position of 'i'.  Returns 1 as long as we don't increment i past the
 * size of fbuf (fbsz).  Otherwise, returns 0.
 */

static int
ccom(void)
{
	register char	cc;
	int		len;

	while ((cc = fbuf[i]) == ' ' || cc == '\t' || cc == '\n')
		if (i++ >= fbsz)
			return (0);
	if (fbuf[i] == '/' && fbuf[i+1] == '*') {
		i += 2;
		while (fbuf[i] != '*' || fbuf[i+1] != '/') {
			if (fbuf[i] == '\\')
				i++;
			if ((len = mblen(&fbuf[i], MB_CUR_MAX)) <= 0)
				len = 1;
			i += len;
			if (i >= fbsz)
				return (0);
		}
		if ((i += 2) >= fbsz)
			return (0);
	}
	if (fbuf[i] == '\n')
		if (ccom() == 0)
			return (0);
	return (1);
}

/*
 * ascom -
 * Increments the current index 'i' into the file buffer 'fbuf' past
 * consecutive assembler program comment lines starting with ASCOMCHAR,
 * starting at the current position of 'i'.
 * Returns 1 as long as we don't increment i past the
 * size of fbuf (fbsz).  Otherwise returns 0.
 */

static int
ascom(void)
{
	while (fbuf[i] == ASCOMCHAR) {
		i++;
		while (fbuf[i++] != '\n')
			if (i >= fbsz)
				return (0);
		while (fbuf[i] == '\n')
			if (i++ >= fbsz)
				return (0);
	}
	return (1);
}

static int
sccs(void)
{				/* look for "1hddddd" where d is a digit */
	register int j;

	if (fbuf[0] == 1 && fbuf[1] == 'h') {
		for (j = 2; j <= 6; j++) {
			if (isdigit(fbuf[j]))
				continue;
			else
				return (0);
		}
	} else {
		return (0);
	}
	return (1);
}

static int
english(char *bp, int n)
{
#define	NASC 128		/* number of ascii char ?? */
	register int	j, vow, freq, rare, len;
	register int	badpun = 0, punct = 0;
	int	ct[NASC];

	if (n < 50)
		return (0); /* no point in statistics on squibs */
	for (j = 0; j < NASC; j++)
		ct[j] = 0;
	for (j = 0; j < n; j += len) {
		if ((unsigned char)bp[j] < NASC)
			ct[bp[j]|040]++;
		switch (bp[j]) {
		case '.':
		case ',':
		case ')':
		case '%':
		case ';':
		case ':':
		case '?':
			punct++;
			if (j < n-1 && bp[j+1] != ' ' && bp[j+1] != '\n')
				badpun++;
		}
		if ((len = mblen(&bp[j], MB_CUR_MAX)) <= 0)
			len = 1;
	}
	if (badpun*5 > punct)
		return (0);
	vow = ct['a'] + ct['e'] + ct['i'] + ct['o'] + ct['u'];
	freq = ct['e'] + ct['t'] + ct['a'] + ct['i'] + ct['o'] + ct['n'];
	rare = ct['v'] + ct['j'] + ct['k'] + ct['q'] + ct['x'] + ct['z'];
	if (2*ct[';'] > ct['e'])
		return (0);
	if ((ct['>'] + ct['<'] + ct['/']) > ct['e'])
		return (0);	/* shell file test */
	return (vow * 5 >= n - ct[' '] && freq >= 10 * rare);
}


static int
shellscript(char buf[], struct stat64 *sb)
{
	char *tp, *cp, *xp, *up, *gp;

	cp = strchr(buf, '\n');
	if (cp == NULL || cp - fbuf > fbsz)
		return (0);
	for (tp = buf; tp != cp && isspace((unsigned char)*tp); tp++)
		if (!isascii(*tp))
			return (0);
	for (xp = tp; tp != cp && !isspace((unsigned char)*tp); tp++)
		if (!isascii(*tp))
			return (0);
	if (tp == xp)
		return (0);
	if (sb->st_mode & S_ISUID)
		up = gettext("set-uid ");
	else
		up = "";

	if (sb->st_mode & S_ISGID)
		gp = gettext("set-gid ");
	else
		gp = "";

	if (strncmp(xp, "/bin/sh", tp - xp) == 0)
		xp = gettext("shell");
	else if (strncmp(xp, "/bin/csh", tp - xp) == 0)
		xp = gettext("c-shell");
	else if (strncmp(xp, "/usr/sbin/dtrace", tp - xp) == 0)
		xp = gettext("DTrace");
	else
		*tp = '\0';
	/*
	 * TRANSLATION_NOTE
	 * This message is printed by file command for shell scripts.
	 * The first %s is for the translation for "set-uid " (if the script
	 *   has the set-uid bit set), or is for an empty string (if the
	 *   script does not have the set-uid bit set).
	 * Similarly, the second %s is for the translation for "set-gid ",
	 *   or is for an empty string.
	 * The third %s is for the translation for either: "shell", "c-shell",
	 *   or "DTrace", or is for the pathname of the program the script
	 *   executes.
	 */
	(void) printf(gettext("%s%sexecutable %s script\n"), up, gp, xp);
	return (1);
}

static int
get_door_target(char *file, char *buf, size_t bufsize)
{
	int fd;
	door_info_t di;
	psinfo_t psinfo;

	if ((fd = open64(file, O_RDONLY)) < 0 ||
	    door_info(fd, &di) != 0) {
		if (fd >= 0)
			(void) close(fd);
		return (-1);
	}
	(void) close(fd);

	(void) sprintf(buf, "/proc/%ld/psinfo", di.di_target);
	if ((fd = open64(buf, O_RDONLY)) < 0 ||
	    read(fd, &psinfo, sizeof (psinfo)) != sizeof (psinfo)) {
		if (fd >= 0)
			(void) close(fd);
		return (-1);
	}
	(void) close(fd);

	(void) snprintf(buf, bufsize, "%s[%ld]", psinfo.pr_fname, di.di_target);
	return (0);
}

/*
 * ZIP file header information
 */
#define	SIGSIZ		4
#define	LOCSIG		"PK\003\004"
#define	LOCHDRSIZ	30

#define	CH(b, n)	(((unsigned char *)(b))[n])
#define	SH(b, n)	(CH(b, n) | (CH(b, n+1) << 8))
#define	LG(b, n)	(SH(b, n) | (SH(b, n+2) << 16))

#define	LOCNAM(b)	(SH(b, 26))	/* filename size */
#define	LOCEXT(b)	(SH(b, 28))	/* extra field size */

#define	XFHSIZ		4		/* header id, data size */
#define	XFHID(b)	(SH(b, 0))	/* extract field header id */
#define	XFDATASIZ(b)	(SH(b, 2))	/* extract field data size */
#define	XFJAVASIG	0xcafe		/* java executables */

static int
zipfile(char *fbuf, int fd)
{
	off_t xoff, xoff_end;

	if (strncmp(fbuf, LOCSIG, SIGSIZ) != 0)
		return (0);

	xoff = LOCHDRSIZ + LOCNAM(fbuf);
	xoff_end = xoff + LOCEXT(fbuf);

	while (xoff < xoff_end) {
		char xfhdr[XFHSIZ];

		if (pread(fd, xfhdr, XFHSIZ, xoff) != XFHSIZ)
			break;

		if (XFHID(xfhdr) == XFJAVASIG) {
			(void) printf("%s\n", gettext("java archive file"));
			return (1);
		}
		xoff += sizeof (xfhdr) + XFDATASIZ(xfhdr);
	}

	/*
	 * We could just print "ZIP archive" here.
	 *
	 * However, customers may be using their own entries in
	 * /etc/magic to distinguish one kind of ZIP file from another, so
	 * let's defer the printing of "ZIP archive" to there.
	 */
	return (0);
}

static int
is_snoop_file(const char *buf)
{
	unsigned long long t;
	unsigned int v;
	char *vp = (char *)((char *)buf + sizeof (unsigned long long));

	/* Do memcpy to avoid any alignment issues */
	(void) memcpy(&t, buf, sizeof (t));
	t = htonll(t);
	(void) memcpy(&v, vp, sizeof (v));
	if (t == SNOOP_MAGIC) {
		(void) printf(gettext("Snoop capture file -"
		    " version %ld \n"), htonl(v));
		return (1);
	}
	return (0);
}

static int
is_crash_dump(const char *buf, int fd)
{
	/* LINTED: pointer cast may result in improper alignment */
	const dumphdr_t *dhp = (const dumphdr_t *)buf;

	/*
	 * The current DUMP_MAGIC string covers Solaris 7 and later releases.
	 * The utsname struct is only present in dumphdr_t's with dump_version
	 * greater than or equal to 9.
	 */
	if (dhp->dump_magic == DUMP_MAGIC) {
		print_dumphdr(fd, dhp, return_uint32, NATIVE_ISA);

	} else if (dhp->dump_magic == swap_uint32(DUMP_MAGIC)) {
		print_dumphdr(fd, dhp, swap_uint32, OTHER_ISA);

	} else if (dhp->dump_magic == OLD_DUMP_MAGIC ||
	    dhp->dump_magic == swap_uint32(OLD_DUMP_MAGIC)) {
		char *isa = (dhp->dump_magic == OLD_DUMP_MAGIC ?
		    NATIVE_ISA : OTHER_ISA);
		(void) printf(gettext("SunOS 32-bit %s crash dump\n"), isa);

	} else {
		return (0);
	}

	return (1);
}

static void
print_dumphdr(const int fd, const dumphdr_t *dhp, uint32_t (*swap)(uint32_t),
    const char *isa)
{
	dumphdr_t dh;

	/*
	 * A dumphdr_t is bigger than FBSZ, so we have to manually read the
	 * rest of it.
	 */
	if (swap(dhp->dump_version) > 8 && pread(fd, &dh, sizeof (dumphdr_t),
	    0) == sizeof (dumphdr_t)) {
		const char *c = swap(dh.dump_flags) & DF_COMPRESSED ?
		    "compressed " : "";
		const char *l = swap(dh.dump_flags) & DF_LIVE ?
		    "live" : "crash";

		(void) printf(gettext(
		    "%s %s %s %u-bit %s %s%s dump from '%s'\n"),
		    dh.dump_utsname.sysname, dh.dump_utsname.release,
		    dh.dump_utsname.version, swap(dh.dump_wordsize), isa,
		    c, l, dh.dump_utsname.nodename);
	} else {
		(void) printf(gettext("SunOS %u-bit %s crash dump\n"),
		    swap(dhp->dump_wordsize), isa);
	}
}

static void
usage(void)
{
	(void) fprintf(stderr, gettext(
	    "usage: file [-dh] [-M mfile] [-m mfile] [-f ffile] file ...\n"
	    "       file [-dh] [-M mfile] [-m mfile] -f ffile\n"
	    "       file -i [-h] [-f ffile] file ...\n"
	    "       file -i [-h] -f ffile\n"
	    "       file -c [-d] [-M mfile] [-m mfile]\n"));
	exit(2);
}

static uint32_t
swap_uint32(uint32_t in)
{
	uint32_t out;

	out = (in & 0x000000ff) << 24;
	out |= (in & 0x0000ff00) << 8; /* >> 8 << 16 */
	out |= (in & 0x00ff0000) >> 8; /* >> 16 << 8 */
	out |= (in & 0xff000000) >> 24;

	return (out);
}

static uint32_t
return_uint32(uint32_t in)
{
	return (in);
}

/*
 * default_magic -
 *	allocate space for and create the default magic file
 *	name string.
 */

static void
default_magic(void)
{
	const char *msg_locale = setlocale(LC_MESSAGES, NULL);
	struct stat	statbuf;

	if ((dfile = malloc(strlen(msg_locale) + 35)) == NULL) {
		int err = errno;
		(void) fprintf(stderr, gettext("%s: malloc failed: %s\n"),
		    File, strerror(err));
		exit(2);
	}
	(void) snprintf(dfile, strlen(msg_locale) + 35,
	    "/usr/lib/locale/%s/LC_MESSAGES/magic", msg_locale);
	if (stat(dfile, &statbuf) != 0) {
		(void) strcpy(dfile, "/etc/magic");
	}
}

/*
 * add_to_mlist -
 *	Add the given magic_file filename string to the list of magic
 *	files (mlist).  This list of files will later be examined, and
 *	each magic file's entries will be added in order to
 *	the mtab table.
 *
 *	The first flag is set to 1 to add to the first list, mlist1.
 *	The first flag is set to 0 to add to the second list, mlist2.
 */

static void
add_to_mlist(char *magic_file, int first)
{
	char	**mlist;	/* ordered list of magic files */
	size_t	mlist_sz;	/* number of pointers allocated  for mlist */
	char	**mlistp;	/* next entry in mlist */
	size_t mlistp_off;

	if (first) {
		mlist = mlist1;
		mlist_sz = mlist1_sz;
		mlistp = mlist1p;
	} else {
		mlist = mlist2;
		mlist_sz = mlist2_sz;
		mlistp = mlist2p;
	}

	if (mlist == NULL) {	/* initial mlist allocation */
		if ((mlist = calloc(MLIST_SZ, sizeof (char *))) == NULL) {
			int err = errno;
			(void) fprintf(stderr, gettext("%s: malloc "
			    "failed: %s\n"), File, strerror(err));
			exit(2);
		}
		mlist_sz = MLIST_SZ;
		mlistp = mlist;
	}
	if ((mlistp - mlist) >= mlist_sz) {
		mlistp_off = mlistp - mlist;
		mlist_sz *= 2;
		if ((mlist = realloc(mlist,
		    mlist_sz * sizeof (char *))) == NULL) {
			int err = errno;
			(void) fprintf(stderr, gettext("%s: malloc "
			    "failed: %s\n"), File, strerror(err));
			exit(2);
		}
		mlistp = mlist + mlistp_off;
	}
	/*
	 * now allocate memory for and copy the
	 * magic file name string
	 */
	if ((*mlistp = malloc(strlen(magic_file) + 1)) == NULL) {
		int err = errno;
		(void) fprintf(stderr, gettext("%s: malloc failed: %s\n"),
		    File, strerror(err));
		exit(2);
	}
	(void) strlcpy(*mlistp, magic_file, strlen(magic_file) + 1);
	mlistp++;

	if (first) {
		mlist1 = mlist;
		mlist1_sz = mlist_sz;
		mlist1p = mlistp;
	} else {
		mlist2 = mlist;
		mlist2_sz = mlist_sz;
		mlist2p = mlistp;
	}
}

static void
fd_cleanup(void)
{
	if (ifd != -1) {
		(void) close(ifd);
		ifd = -1;
	}
}