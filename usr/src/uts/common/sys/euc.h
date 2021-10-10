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


#ifndef _SYS_EUC_H
#define	_SYS_EUC_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	NOTASCII
#define	SS2	0x008e
#define	SS3	0x008f

	/* NOTE: c of following macros must be the 1st byte of characters */
#define	ISASCII(c)	(!((c) & ~0177))
#define	NOTASCII(c)	((c) & ~0177)
#define	ISSET2(c)	(((c) & 0x00ff) == SS2)
#define	ISSET3(c)	(((c) & 0x00ff) == SS3)
#define	ISPRINT(c, wp)	(wp._multibyte && !ISASCII(c) || isprint(c))
			/* eucwidth_t wp; */

#ifndef _EUCWIDTH_T
#define	_EUCWIDTH_T
typedef struct {
	short int _eucw1, _eucw2, _eucw3;	/*	EUC width	*/
	short int _scrw1, _scrw2, _scrw3;	/*	screen width	*/
	short int _pcw;		/*	WIDE_CHAR width	*/
	char _multibyte;	/*	1=multi-byte, 0=single-byte	*/
} eucwidth_t;
#endif	/* ! _EUCWIDTH_T */
#endif	/* ! NOTASCII */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EUC_H */