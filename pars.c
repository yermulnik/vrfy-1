/*
 * Copyright (c) 1983 Eric P. Allman
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static char Version[] = "@(#)pars.c	e07@nikhef.nl (Eric Wassenaar) 921013";
#endif

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>

typedef int	bool;
#define TRUE	1
#define FALSE	0

#define NOT_DOTTED_QUAD	((u_long)-1)

#define sameword(a,b)	(strcasecmp(a,b) == 0)

extern char *index();
extern char *rindex();
extern char *strcpy();
extern u_long inet_addr();

/* pars.c */
bool invalidaddr();
char *find_delim();
char *parselist();
static char *cataddr();
char *parseaddr();
bool invalidhost();
char *parsehost();
char *parsespec();
bool invalidloop();

/* main.c */
void usrerr();

/* conn.c */
u_long numeric_addr();

/*
** INVALIDADDR -- check an address for invalid control characters
** --------------------------------------------------------------
**
**	Returns:
**		TRUE if address string could cause problems.
**		FALSE otherwise.
**
**	Side Effects:
**		An error message is generated if invalid.
**
**	Called before any parsing is attempted.
**	All control characters are rejected, not only the sendmail
**	meta-characters, even within comments or quoted strings.
*/

bool
invalidaddr(addrspec)
char *addrspec;				/* address specification */
{
	register char *p;

	for (p = addrspec; *p != '\0'; p++)
	{
		/* reject all control characters unless harmless */
		if (iscntrl(*p) && !isspace(*p))
		{
			usrerr("invalid control character in address");
			return(TRUE);
		}
	}

	return(FALSE);
}

/*
** FIND_DELIM -- Find the position of a delimiter in an address
** ------------------------------------------------------------
**
**	Returns:
**		Position of delimiter in address if found.
**		Position of null byte after address if not found.
**		NULL in case of elementary syntax errors.
**
**	Side Effects:
**		An error message is generated if invalid.
**
**	Used by parselist() to locate address delimiters in address lists.
**	Used by parseaddr() to locate brackets and parens in addresses.
**	Used by parsehost() to locate domain separators in domain names.
**
**	It will search for an unquoted delimiter outside comments
**	in complicated full address specifications like
**	"comment \"comment\" comment" <address (comment \comment)>
*/

char *
find_delim(addrspec, delimiter)
char *addrspec;				/* full address specification */
char delimiter;				/* delimiter char to search for */
{
	bool escaping = FALSE;		/* set if need to escape next char */
	bool quoting = FALSE;		/* set if within quoted string */
	int comment = 0;		/* level of parenthesized comments */
	int bracket = 0;		/* level of bracketed addresses */
	register char *p;

/*
 * Scan address list, and break when delimiter found.
 */
	for (p = addrspec; p != NULL && *p != '\0'; p++)
	{
		if (escaping)
			escaping = FALSE;
		else if (*p == '\\')
			escaping = TRUE;

		else if (*p == '"')
			quoting = !quoting;
		else if (quoting)
			continue;

		else if (*p == delimiter && bracket == 0 && comment == 0)
			break;

		else if (*p == '(')
			comment++;
		else if (*p == ')')
		{
			comment--;
			if (comment < 0)
				break;
		}
		else if (comment > 0)
			continue;

		else if (*p == '<')
			bracket++;
		else if (*p == '>')
		{
			bracket--;
			if (bracket < 0)
				break;
		}
	}

/*
 * Check for elementary syntax errors.
 * If ok, p points to the delimiter, or to a null byte.
 */
	if (quoting)
		usrerr("Unbalanced '\"'");
	else if (comment > 0)
		usrerr("Unbalanced '('");
	else if (comment < 0)
		usrerr("Unbalanced ')'");
	else if (bracket > 0)
		usrerr("Unbalanced '<'");
	else if (bracket < 0)
		usrerr("Unbalanced '>'");
	else
		return(p);

	return(NULL);
}

/*
** PARSELIST -- Isolate a single address in an address list
** --------------------------------------------------------
**
**	Returns:
**		Beginning of first address in the list.
**		NULL in case of elementary syntax errors.
**
**	Side effects:
**		The address is terminated with a null byte,
**		clobbering the original address list.
**		An error message is generated if invalid.
**
**	DelimAddr will point to the beginning of the next address
**	in the list, or is set to NULL if this was the last address.
*/

char *DelimAddr = NULL;		/* position of next address in list */

char *
parselist(addrspec)
char *addrspec;
{
	char delimiter;			/* delimiter between addresses */

/*
 * Always search for comma as address delimiter.
 */
#ifdef notdef
	delimiter = ' ';
	if (index(addrspec, ',') != NULL || index(addrspec, ';') != NULL ||
	    index(addrspec, '<') != NULL || index(addrspec, '(') != NULL)
#endif
		delimiter = ',';

/*
 * Move to the beginning of the first address in the list.
 */
	while (isspace(*addrspec) || *addrspec == ',')
		addrspec++;

/*
 * Scan address list for delimiter. Abort on syntax errors.
 */
	DelimAddr = find_delim(addrspec, delimiter);
	if (DelimAddr == NULL)
		return(NULL);

/*
 * Move to the beginning of the next following address.
 */
	while (isspace(*DelimAddr) || *DelimAddr == ',')
		*DelimAddr++ = '\0';

/*
 * Return the first address and the position of the next address.
 */
	if (*DelimAddr == '\0')
		DelimAddr = NULL;

	return(addrspec);
}

/*
** CATADDR -- Append an address part to an address buffer
** ------------------------------------------------------
**
**	Returns:
**		Next free position in address buffer.
**
**	Used by parseaddr() to construct an address without comments.
**	Address parts are concatenated without embedded blanks.
**	Trailing ';' characters used in group syntax are skipped.
*/

static char *
cataddr(buf, address, addrspec)
char *buf;				/* start of address buffer */
register char *address;			/* buf position to append to */
register char *addrspec;		/* address spec to fetch from */
{
	/* skip leading whitespace */
	while (isspace(*addrspec))
		addrspec++;

	/* copy address part */
	while (*addrspec != '\0')
		*address++ = *addrspec++;

	/* remove trailing whitespace and trailing ';' */
	while (address > buf && (isspace(address[-1]) || address[-1] == ';'))
		address--;

	/* return next free position */
	return(address);
}

/*
** PARSEADDR -- Construct a plain address without comments
** -------------------------------------------------------
**
**	Returns:
**		The plain address as saved in static storage.
**		NULL in case of elementary syntax errors.
**
**	Side Effects:
**		An error message is generated if invalid.
**
**	We assume the parsed address will fit into the local storage.
**
**	Comments outside brackets, and the brackets, are eliminated.
**	Comments within parentheses, and the parens, are eliminated.
**	The remaining address parts are concatenated without blanks.
**
**	Note for the insiders: this is not 100% conforming to sendmail
**	since we disregard the type of individual tokens, and do not
**	insert SpaceSub characters between atoms. We don't care here.
*/

char *
parseaddr(addrspec)
char *addrspec;				/* full address specification */
{
	register char *address;		/* plain address without comment */
	static char buf[BUFSIZ];	/* saved plain address */
	register char *p, *q;

/*
 * Check if we have anything in angle brackets. If so, reprocess
 * the part between the brackets. Abort in case of syntax errors.
 */
	p = find_delim(addrspec, '<');
	if (p != NULL && *p == '<')
	{
		q = find_delim(p+1, '>');
		if (q != NULL && *q == '>')
		{
			*q = '\0';
			address = parseaddr(p+1);
			*q = '>';
			return(address);
		}
	}

	if (p == NULL || *p != '\0')
		return(NULL);

/*
 * Strip out comments between parentheses.
 */
	address = buf;
rescan:
	p = find_delim(addrspec, '(');
	if (p != NULL && *p == '(')
	{
		q = find_delim(p+1, ')');
		if (q != NULL && *q == ')')
		{
			*p = '\0';
			address = cataddr(buf, address, addrspec);
			*p = '(';
			addrspec = q+1;
			goto rescan;
		}
	}

	if (p == NULL || *p != '\0')
		return(NULL);

	address = cataddr(buf, address, addrspec);
	*address = '\0';
	return(buf);
}

/*
** INVALIDHOST -- check for invalid domain name specification
** ----------------------------------------------------------
**
**	Returns:
**		TRUE if the domain name is (probably) invalid.
**		FALSE otherwise.
**
**	Side Effects:
**		An error message is generated if invalid.
*/

bool
invalidhost(domain)
char *domain;				/* domain name to be checked */
{
	register char *p;

	/* must not be of zero length */
	if (strlen(domain) < 1)
	{
		usrerr("null domain");
		return(TRUE);
	}

	/* must not end with a dot */
	if (domain[strlen(domain)-1] == '.')
	{
		usrerr("illegal trailing dot");
		return(TRUE);
	}

	/* must not be a plain dotted quad */
	if (inet_addr(domain) != NOT_DOTTED_QUAD)
	{
		usrerr("invalid dotted quad");
		return(TRUE);
	}

	/* but may be a dotted quad between brackets */
	if (numeric_addr(domain) != NOT_DOTTED_QUAD)
		return(FALSE);

	/* check for invalid embedded characters */
	for (p = domain; *p != '\0'; p++)
	{
		/* only alphanumeric plus dot and dash allowed */
		if (!isalnum(*p) && *p != '.' && *p != '-')
		{
			usrerr("invalid domain name");
			return(TRUE);
		}
	}

	/* looks like a valid domain name */
	return(FALSE);
}

/*
** PARSEHOST --  Extract the domain part from a plain address
** ----------------------------------------------------------
**
**	Returns:
**		Pointer to the domain part, if present, or
**		"localhost" in case there is no domain part.
**		NULL in case of elementary syntax errors.
**
**	Side effects:
**		Domain part may be terminated with a null
**		byte, thereby clobbering the address.
**		An error message is generated if invalid.
**
**	In addresses with both '!' and '@' precedence is
**	given to the '@' part: 'foo!user@bar' goes to bar.
**	Source routes have the highest priority.
*/

char *
parsehost(address)
char *address;				/* plain address without comment */
{
	register char *delim;

/*
 * RFC822 source route.
 * Note that it should have been specified between brackets.
 */
	if (*address == '@')
	{
		delim = find_delim(address, ',');
		if (delim == NULL || *delim == '\0')
			delim = find_delim(address, ':');
		if (delim == NULL || *delim == '\0')
		{
			usrerr("invalid source route");
			return(NULL);
		}
		*delim = '\0';
		return(address+1);
	}

/*
 * Ordinary RFC822 internet address.
 * Note that we scan for the first '@' and not for the last.
 */
	delim = find_delim(address, '@');
	if (delim != NULL && *delim != '\0')
		return(delim+1);

/*
 * Old fashioned uucp path.
 */
	delim = find_delim(address, '!');
	if (delim != NULL && *delim != '\0')
	{
		*delim = '\0';
		return(address);
	}

/*
 * Everything else is local.
 */
	return("localhost");
}

/*
** PARSESPEC --  Extract and validate plain address and domain part
** ----------------------------------------------------------------
**
**	Returns:
**		Pointer to the domain part, if present, or
**		"localhost" in case there is no domain part.
**		NULL in case of elementary syntax errors.
**		NULL in case of probably invalid domain.
**
**	Side effects:
**		An error message is generated if invalid.
**
**	Outputs:
**		A copy of the parsed components is saved, if requested.
**
**	We assume the parsed components fit into the provided storage.
*/

char *
parsespec(addrspec, copya, copyd)
char *addrspec;				/* full address specification */
char *copya;				/* buffer to store plain address */
char *copyd;				/* buffer to store domain part */
{
	char *address;			/* plain address without comment */
	char *domain;			/* domain part of address */

/*
 * Extract plain address from full address specification.
 * Abort if address could not be parsed properly.
 */
	address = parseaddr(addrspec);
	if (address == NULL)
		return(NULL);

	/* save plain address if requested */
	if (copya != NULL)
		(void) strcpy(copya, address);

/*
 * Extract the domain part from the plain address.
 * Abort if domain could not be parsed properly.
 */
	domain = parsehost(address);
	if (domain == NULL)
		return(NULL);

	/* save domain part if requested */
	if (copyd != NULL)
		(void) strcpy(copyd, domain);

/*
 * Validate the domain part. Make some basic checks.
 * Abort if this is an invalid domain name.
 */
	if (invalidhost(domain))
		return(NULL);

/*
 * Looks like a valid address specification.
 */
	return(domain);
}

/*
** INVALIDLOOP -- Check whether an address is present in chain
** -----------------------------------------------------------
**
**	Returns:
**		TRUE if address is found in the chain.
**		FALSE otherwise.
**
**	Side effects:
**		An error message is generated if invalid.
*/

bool
invalidloop(address)
char *address;
{
	extern char *AddrChain[];	/* addresses in chain */
	extern int recursion_level;	/* current limit */
	register int j;

	for (j = 0; j < recursion_level; j++)
	{
		if (sameword(address, AddrChain[j]))
		{
			usrerr("mail forwarding loop");
			return(TRUE);
		}
	}

	return(FALSE);
}
