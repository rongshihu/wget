/* Command line parsing.
   Copyright (C) 1995, 1996, 1997, 1998, 2000, 2001, 2002
   Free Software Foundation, Inc.

This file is part of GNU Wget.

GNU Wget is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GNU Wget is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wget; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

In addition, as a special exception, the Free Software Foundation
gives permission to link the code of its release of Wget with the
OpenSSL project's "OpenSSL" library (or with modified versions of it
that use the same license as the "OpenSSL" library), and distribute
the linked executables.  You must obey the GNU General Public License
in all respects for all of the code used other than "OpenSSL".  If you
modify this file, you may extend this exception to your version of the
file, but you are not obligated to do so.  If you do not wish to do
so, delete this exception statement from your version.  */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/types.h>
#ifdef HAVE_STRING_H
# include <string.h>
#else
# include <strings.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif
#ifdef HAVE_NLS
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif /* HAVE_LOCALE_H */
#endif /* HAVE_NLS */
#include <assert.h>

#include <errno.h>
#ifndef errno
extern int errno;
#endif

#include "wget.h"
#include "utils.h"
#include "init.h"
#include "retr.h"
#include "recur.h"
#include "host.h"
#include "cookies.h"
#include "url.h"
#include "progress.h"		/* for progress_handle_sigwinch */
#include "convert.h"

/* On GNU system this will include system-wide getopt.h. */
#include "getopt.h"

#ifndef PATH_SEPARATOR
# define PATH_SEPARATOR '/'
#endif

struct options opt;

extern LARGE_INT total_downloaded_bytes;
extern char *version_string;

extern struct cookie_jar *wget_cookie_jar;

/* From log.c.  */
void log_init PARAMS ((const char *, int));
void log_close PARAMS ((void));
void log_request_redirect_output PARAMS ((const char *));

static RETSIGTYPE redirect_output_signal PARAMS ((int));

const char *exec_name;

/* Initialize I18N.  The initialization amounts to invoking
   setlocale(), bindtextdomain() and textdomain().
   Does nothing if NLS is disabled or missing.  */
static void
i18n_initialize (void)
{
  /* If HAVE_NLS is defined, assume the existence of the three
     functions invoked here.  */
#ifdef HAVE_NLS
  /* Set the current locale.  */
  /* Here we use LC_MESSAGES instead of LC_ALL, for two reasons.
     First, message catalogs are all of I18N Wget uses anyway.
     Second, setting LC_ALL has a dangerous potential of messing
     things up.  For example, when in a foreign locale, Solaris
     strptime() fails to handle international dates correctly, which
     makes http_atotm() malfunction.  */
#ifdef LC_MESSAGES
  setlocale (LC_MESSAGES, "");
  setlocale (LC_CTYPE, "");
#else
  setlocale (LC_ALL, "");
#endif
  /* Set the text message domain.  */
  bindtextdomain ("wget", LOCALEDIR);
  textdomain ("wget");
#endif /* HAVE_NLS */
}

/* Definition of command-line options. */

#ifdef HAVE_SSL
# define IF_SSL(x) x
#else
# define IF_SSL(x) NULL
#endif

#ifdef ENABLE_DEBUG
# define IF_DEBUG(x) x
#else
# define IF_DEBUG(x) NULL
#endif

struct cmdline_option {
  const char *long_name;
  char short_name;
  enum {
    OPT_VALUE,
    OPT_BOOLEAN,
    /* Non-standard options that have to be handled specially in
       main().  */
    OPT__APPEND_OUTPUT,
    OPT__CLOBBER,
    OPT__EXECUTE,
    OPT__HELP,
    OPT__NO,
    OPT__PARENT,
    OPT__VERSION
  } type;
  const char *handle_cmd;	/* for standard options */
  int argtype;			/* for non-standard options */
};

struct cmdline_option option_data[] =
  {
    { "accept", 'A', OPT_VALUE, "accept", -1 },
    { "append-output", 'a', OPT__APPEND_OUTPUT, NULL, required_argument },
    { "background", 'b', OPT_BOOLEAN, "background", -1 },
    { "backup-converted", 'K', OPT_BOOLEAN, "backupconverted", -1 },
    { "backups", 0, OPT_BOOLEAN, "backups", -1 },
    { "base", 'B', OPT_VALUE, "base", -1 },
    { "bind-address", 0, OPT_VALUE, "bindaddress", -1 },
    { "cache", 'C', OPT_BOOLEAN, "cache", -1 },
    { "clobber", 0, OPT__CLOBBER, NULL, optional_argument },
    { "connect-timeout", 0, OPT_VALUE, "connecttimeout", -1 },
    { "continue", 'c', OPT_BOOLEAN, "continue", -1 },
    { "convert-links", 'k', OPT_BOOLEAN, "convertlinks", -1 },
    { "cookies", 0, OPT_BOOLEAN, "cookies", -1 },
    { "cut-dirs", 0, OPT_VALUE, "cutdirs", -1 },
    { IF_DEBUG ("debug"), 'd', OPT_BOOLEAN, "debug", -1 },
    { "delete-after", 0, OPT_BOOLEAN, "deleteafter", -1 },
    { "directories", 0, OPT_BOOLEAN, "dirstruct", -1 },
    { "directory-prefix", 'P', OPT_VALUE, "dirprefix", -1 },
    { "dns-cache", 0, OPT_BOOLEAN, "dnscache", -1 },
    { "dns-timeout", 0, OPT_VALUE, "dnstimeout", -1 },
    { "domains", 'D', OPT_VALUE, "domains", -1 },
    { "dot-style", 0, OPT_VALUE, "dotstyle", -1 },
    { "egd-file", 0, OPT_VALUE, "egdfile", -1 },
    { "exclude-directories", 'X', OPT_VALUE, "excludedirectories", -1 },
    { "exclude-domains", 0, OPT_VALUE, "excludedomains", -1 },
    { "execute", 'e', OPT__EXECUTE, NULL, required_argument },
    { "follow-ftp", 0, OPT_BOOLEAN, "followftp", -1 },
    { "follow-tags", 0, OPT_VALUE, "followtags", -1 },
    { "force-directories", 'x', OPT_BOOLEAN, "dirstruct", -1 },
    { "force-html", 'F', OPT_BOOLEAN, "forcehtml", -1 },
    { "glob", 'g', OPT_BOOLEAN, "glob", -1 },
    { "header", 0, OPT_VALUE, "header", -1 },
    { "help", 'h', OPT__HELP, NULL, no_argument },
    { "host-directories", 0, OPT_BOOLEAN, "addhostdir", -1 },
    { "html-extension", 'E', OPT_BOOLEAN, "htmlextension", -1 },
    { "htmlify", 0, OPT_BOOLEAN, "htmlify", -1 },
    { "http-keep-alive", 0, OPT_BOOLEAN, "httpkeepalive", -1 },
    { "http-passwd", 0, OPT_VALUE, "httppasswd", -1 },
    { "http-user", 0, OPT_VALUE, "httpuser", -1 },
    { "ignore-length", 0, OPT_BOOLEAN, "ignorelength", -1 },
    { "ignore-tags", 'G', OPT_VALUE, "ignoretags", -1 },
    { "include-directories", 'I', OPT_VALUE, "includedirectories", -1 },
    { "input-file", 'i', OPT_VALUE, "input", -1 },
    { "keep-session-cookies", 0, OPT_BOOLEAN, "keepsessioncookies", -1 },
    { "level", 'l', OPT_VALUE, "reclevel", -1 },
    { "limit-rate", 0, OPT_VALUE, "limitrate", -1 },
    { "load-cookies", 0, OPT_VALUE, "loadcookies", -1 },
    { "mirror", 'm', OPT_BOOLEAN, NULL, -1 },
    { "no", 'n', OPT__NO, NULL, required_argument },
    { "no-clobber", 0, OPT_BOOLEAN, "noclobber", -1 },
    { "no-parent", 0, OPT_BOOLEAN, "noparent", -1 },
    { "output-document", 'O', OPT_VALUE, "outputdocument", -1 },
    { "output-file", 'o', OPT_VALUE, "logfile", -1 },
    { "page-requisites", 'p', OPT_BOOLEAN, "pagerequisites", -1 },
    { "parent", 0, OPT__PARENT, NULL, optional_argument },
    { "passive-ftp", 0, OPT_BOOLEAN, "passiveftp", -1 },
    { "post-data", 0, OPT_VALUE, "postdata", -1 },
    { "post-file", 0, OPT_VALUE, "postfile", -1 },
    { "progress", 0, OPT_VALUE, "progress", -1 },
    { "proxy", 'Y', OPT_BOOLEAN, "useproxy", -1 },
    { "proxy-passwd", 0, OPT_VALUE, "proxypasswd", -1 },
    { "proxy-user", 0, OPT_VALUE, "proxyuser", -1 },
    { "quiet", 'q', OPT_BOOLEAN, "quiet", -1 },
    { "quota", 'Q', OPT_VALUE, "quota", -1 },
    { "random-wait", 0, OPT_BOOLEAN, "randomwait", -1 },
    { "read-timeout", 0, OPT_VALUE, "readtimeout", -1 },
    { "recursive", 'r', OPT_BOOLEAN, "recursive", -1 },
    { "referer", 0, OPT_VALUE, "referer", -1 },
    { "reject", 'R', OPT_VALUE, "reject", -1 },
    { "relative", 'L', OPT_BOOLEAN, "relativeonly", -1 },
    { "remove-listing", 0, OPT_BOOLEAN, "removelisting", -1 },
    { "restrict-file-names", 0, OPT_BOOLEAN, "restrictfilenames", -1 },
    { "retr-symlinks", 0, OPT_BOOLEAN, "retrsymlinks", -1 },
    { "retry-connrefused", 0, OPT_BOOLEAN, "retryconnrefused", -1 },
    { "save-cookies", 0, OPT_VALUE, "savecookies", -1 },
    { "save-headers", 0, OPT_BOOLEAN, "saveheaders", -1 },
    { "server-response", 'S', OPT_BOOLEAN, "serverresponse", -1 },
    { "span-hosts", 'H', OPT_BOOLEAN, "spanhosts", -1 },
    { "spider", 0, OPT_BOOLEAN, "spider", -1 },
    { IF_SSL ("sslcadir"), 0, OPT_VALUE, "sslcadir", -1 },
    { IF_SSL ("sslcafile"), 0, OPT_VALUE, "sslcafile", -1 },
    { IF_SSL ("sslcertfile"), 0, OPT_VALUE, "sslcertfile", -1 },
    { IF_SSL ("sslcertkey"), 0, OPT_VALUE, "sslcertkey", -1 },
    { IF_SSL ("sslcerttype"), 0, OPT_VALUE, "sslcerttype", -1 },
    { IF_SSL ("sslcheckcert"), 0, OPT_VALUE, "sslcheckcert", -1 },
    { IF_SSL ("sslprotocol"), 0, OPT_VALUE, "sslprotocol", -1 },
    { "strict-comments", 0, OPT_BOOLEAN, "strictcomments", -1 },
    { "timeout", 'T', OPT_VALUE, "timeout", -1 },
    { "timestamping", 'N', OPT_BOOLEAN, "timestamping", -1 },
    { "tries", 't', OPT_VALUE, "tries", -1 },
    { "use-proxy", 'Y', OPT_BOOLEAN, "useproxy", -1 },
    { "user-agent", 'U', OPT_VALUE, "useragent", -1 },
    { "verbose", 'v', OPT_BOOLEAN, "verbose", -1 },
    { "verbose", 0, OPT_BOOLEAN, "verbose", -1 },
    { "version", 'V', OPT__VERSION, "version", no_argument },
    { "wait", 'w', OPT_VALUE, "wait", -1 },
    { "waitretry", 0, OPT_VALUE, "waitretry", -1 },
  };

#undef IF_DEBUG
#undef IF_SSL

static char *
no_prefix (const char *s)
{
  static char buffer[1024];
  static char *p = buffer;

  char *cp = p;
  int size = 3 + strlen (s) + 1;  /* "no-STRING\0" */

  if (p + size >= buffer + sizeof (buffer))
    abort ();

  cp[0] = 'n';
  cp[1] = 'o';
  cp[2] = '-';
  strcpy (cp + 3, s);
  p += size;
  return cp;
}

/* The arguments that that main passes to getopt_long. */
static struct option long_options[2 * countof (option_data) + 1];
static char short_options[128];

/* Mapping between short option chars and option_data indices. */
static unsigned char optmap[96];

/* Marker for `--no-FOO' values in long_options.  */
#define BOOLEAN_NEG_MARKER 1024

static void
init_switches (void)
{
  char *p = short_options;
  int i, o = 0;
  for (i = 0; i < countof (option_data); i++)
    {
      struct cmdline_option *opt = &option_data[i];
      struct option *longopt;

      if (!opt->long_name)
	/* The option is disabled. */
	continue;

      longopt = &long_options[o++];
      longopt->name = opt->long_name;
      longopt->val = i;
      if (opt->short_name)
	{
	  *p++ = opt->short_name;
	  optmap[opt->short_name - 32] = longopt - long_options;
	}
      switch (opt->type)
	{
	case OPT_VALUE:
	  longopt->has_arg = required_argument;
          if (opt->short_name)
	    *p++ = ':';
	  break;
	case OPT_BOOLEAN:
	  /* Don't specify optional arguments for boolean short
	     options.  They are evil because they prevent combining of
	     short options.  */
	  longopt->has_arg = optional_argument;
	  /* For Boolean options, add the "--no-FOO" variant, which is
	     identical to "--foo", except it has opposite meaning and
	     it doesn't allow an argument.  */
	  longopt = &long_options[o++];
	  longopt->name = no_prefix (opt->long_name);
	  longopt->has_arg = no_argument;
	  /* Mask the value so we'll be able to recognize that we're
	     dealing with the false value.  */
	  longopt->val = i | BOOLEAN_NEG_MARKER;
	  break;
	default:
	  assert (opt->argtype != -1);
	  longopt->has_arg = opt->argtype;
	  if (opt->short_name)
	    {
	      if (longopt->has_arg == required_argument)
		*p++ = ':';
	      /* Don't handle optional_argument */
	    }
	}
    }
  xzero (long_options[o]);
  *p = '\0';
}

/* Print the usage message.  */
static void
print_usage (void)
{
  printf (_("Usage: %s [OPTION]... [URL]...\n"), exec_name);
}

/* Print the help message, describing all the available options.  If
   you add an option, be sure to update this list.  */
static void
print_help (void)
{
  printf (_("GNU Wget %s, a non-interactive network retriever.\n"),
	  version_string);
  print_usage ();
  /* Had to split this in parts, so the #@@#%# Ultrix compiler and cpp
     don't bitch.  Also, it makes translation much easier.  */
  fputs (_("\
\n\
Mandatory arguments to long options are mandatory for short options too.\n\
\n"), stdout);
  fputs (_("\
Startup:\n\
  -V,  --version           display the version of Wget and exit.\n\
  -h,  --help              print this help.\n\
  -b,  --background        go to background after startup.\n\
  -e,  --execute=COMMAND   execute a `.wgetrc\'-style command.\n\
\n"), stdout);
  fputs (_("\
Logging and input file:\n\
  -o,  --output-file=FILE     log messages to FILE.\n\
  -a,  --append-output=FILE   append messages to FILE.\n\
  -d,  --debug                print debug output.\n\
  -q,  --quiet                quiet (no output).\n\
  -v,  --verbose              be verbose (this is the default).\n\
  -nv, --non-verbose          turn off verboseness, without being quiet.\n\
  -i,  --input-file=FILE      download URLs found in FILE.\n\
  -F,  --force-html           treat input file as HTML.\n\
  -B,  --base=URL             prepends URL to relative links in -F -i file.\n\
\n"),stdout);
  fputs (_("\
Download:\n\
  -t,  --tries=NUMBER           set number of retries to NUMBER (0 unlimits).\n\
       --retry-connrefused      retry even if connection is refused.\n\
  -O   --output-document=FILE   write documents to FILE.\n\
  -nc, --no-clobber             don\'t clobber existing files or use .# suffixes.\n\
  -c,  --continue               resume getting a partially-downloaded file.\n\
       --progress=TYPE          select progress gauge type.\n\
  -N,  --timestamping           don\'t re-retrieve files unless newer than local.\n\
  -S,  --server-response        print server response.\n\
       --spider                 don\'t download anything.\n\
  -T,  --timeout=SECONDS        set all timeout values to SECONDS.\n\
       --dns-timeout=SECS       set the DNS lookup timeout to SECS.\n\
       --connect-timeout=SECS   set the connect timeout to SECS.\n\
       --read-timeout=SECS      set the read timeout to SECS.\n\
  -w,  --wait=SECONDS           wait SECONDS between retrievals.\n\
       --waitretry=SECONDS      wait 1...SECONDS between retries of a retrieval.\n\
       --random-wait            wait from 0...2*WAIT secs between retrievals.\n\
  -Y,  --proxy=on/off           turn proxy on or off.\n\
  -Q,  --quota=NUMBER           set retrieval quota to NUMBER.\n\
       --bind-address=ADDRESS   bind to ADDRESS (hostname or IP) on local host.\n\
       --limit-rate=RATE        limit download rate to RATE.\n\
       --dns-cache=off          disable caching DNS lookups.\n\
       --restrict-file-names=OS restrict chars in file names to ones OS allows.\n\
\n"), stdout);
  fputs (_("\
Directories:\n\
  -nd, --no-directories            don\'t create directories.\n\
  -x,  --force-directories         force creation of directories.\n\
  -nH, --no-host-directories       don\'t create host directories.\n\
  -P,  --directory-prefix=PREFIX   save files to PREFIX/...\n\
       --cut-dirs=NUMBER           ignore NUMBER remote directory components.\n\
\n"), stdout);
  fputs (_("\
HTTP options:\n\
       --http-user=USER      set http user to USER.\n\
       --http-passwd=PASS    set http password to PASS.\n\
  -C,  --cache=on/off        (dis)allow server-cached data (normally allowed).\n\
  -E,  --html-extension      save all text/html documents with .html extension.\n\
       --ignore-length       ignore `Content-Length\' header field.\n\
       --header=STRING       insert STRING among the headers.\n\
       --proxy-user=USER     set USER as proxy username.\n\
       --proxy-passwd=PASS   set PASS as proxy password.\n\
       --referer=URL         include `Referer: URL\' header in HTTP request.\n\
  -s,  --save-headers        save the HTTP headers to file.\n\
  -U,  --user-agent=AGENT    identify as AGENT instead of Wget/VERSION.\n\
       --no-http-keep-alive  disable HTTP keep-alive (persistent connections).\n\
       --cookies=off         don't use cookies.\n\
       --load-cookies=FILE   load cookies from FILE before session.\n\
       --save-cookies=FILE   save cookies to FILE after session.\n\
       --keep-session-cookies  load and save session (non-permanent) cookies.\n\
       --post-data=STRING    use the POST method; send STRING as the data.\n\
       --post-file=FILE      use the POST method; send contents of FILE.\n\
\n"), stdout);
#ifdef HAVE_SSL
  fputs (_("\
HTTPS (SSL) options:\n\
       --sslcertfile=FILE     optional client certificate.\n\
       --sslcertkey=KEYFILE   optional keyfile for this certificate.\n\
       --egd-file=FILE        file name of the EGD socket.\n\
       --sslcadir=DIR         dir where hash list of CA's are stored.\n\
       --sslcafile=FILE       file with bundle of CA's\n\
       --sslcerttype=0/1      Client-Cert type 0=PEM (default) / 1=ASN1 (DER)\n\
       --sslcheckcert=0/1     Check the server cert agenst given CA\n\
       --sslprotocol=0-3      choose SSL protocol; 0=automatic,\n\
                              1=SSLv2 2=SSLv3 3=TLSv1\n\
\n"), stdout);
#endif
  fputs (_("\
FTP options:\n\
  -nr, --dont-remove-listing   don\'t remove `.listing\' files.\n\
  -g,  --glob=on/off           turn file name globbing on or off.\n\
       --passive-ftp           use the \"passive\" transfer mode.\n\
       --retr-symlinks         when recursing, get linked-to files (not dirs).\n\
\n"), stdout);
  fputs (_("\
Recursive retrieval:\n\
  -r,  --recursive          recursive download.\n\
  -l,  --level=NUMBER       maximum recursion depth (inf or 0 for infinite).\n\
       --delete-after       delete files locally after downloading them.\n\
  -k,  --convert-links      convert non-relative links to relative.\n\
  -K,  --backup-converted   before converting file X, back up as X.orig.\n\
  -m,  --mirror             shortcut option equivalent to -r -N -l inf -nr.\n\
  -p,  --page-requisites    get all images, etc. needed to display HTML page.\n\
       --strict-comments    turn on strict (SGML) handling of HTML comments.\n\
\n"), stdout);
  fputs (_("\
Recursive accept/reject:\n\
  -A,  --accept=LIST                comma-separated list of accepted extensions.\n\
  -R,  --reject=LIST                comma-separated list of rejected extensions.\n\
  -D,  --domains=LIST               comma-separated list of accepted domains.\n\
       --exclude-domains=LIST       comma-separated list of rejected domains.\n\
       --follow-ftp                 follow FTP links from HTML documents.\n\
       --follow-tags=LIST           comma-separated list of followed HTML tags.\n\
  -G,  --ignore-tags=LIST           comma-separated list of ignored HTML tags.\n\
  -H,  --span-hosts                 go to foreign hosts when recursive.\n\
  -L,  --relative                   follow relative links only.\n\
  -I,  --include-directories=LIST   list of allowed directories.\n\
  -X,  --exclude-directories=LIST   list of excluded directories.\n\
  -np, --no-parent                  don\'t ascend to the parent directory.\n\
\n"), stdout);
  fputs (_("Mail bug reports and suggestions to <bug-wget@gnu.org>.\n"),
	 stdout);
}

int
main (int argc, char *const *argv)
{
  char **url, **t;
  int i, ret, longindex;
  int nurl, status;
  int append_to_log = 0;

  i18n_initialize ();

  /* Construct the name of the executable, without the directory part.  */
  exec_name = strrchr (argv[0], PATH_SEPARATOR);
  if (!exec_name)
    exec_name = argv[0];
  else
    ++exec_name;

#ifdef WINDOWS
  windows_main_junk (&argc, (char **) argv, (char **) &exec_name);
#endif

  /* Set option defaults; read the system wgetrc and ~/.wgetrc.  */
  initialize ();

  init_switches ();
  longindex = -1;
  while ((ret = getopt_long (argc, argv,
			     short_options, long_options, &longindex)) != -1)
    {
      int val;
      struct cmdline_option *opt;
      if (ret == '?')
	{
	  print_usage ();
	  printf ("\n");
	  printf (_("Try `%s --help' for more options.\n"), exec_name);
	  exit (2);
	}

      /* If LONGINDEX is unchanged, it means RET is referring a short
	 option.  Look it up in the mapping table.  */
      if (longindex == -1)
	longindex = optmap[ret - 32];
      val = long_options[longindex].val;

      /* Use the retrieved value to locate the option in the
	 option_data array, and to see if we're dealing with the
	 negated "--no-FOO" variant of the boolean option "--foo".  */
      opt = &option_data[val & ~BOOLEAN_NEG_MARKER];
      switch (opt->type)
	{
	case OPT_VALUE:
	  setoptval (opt->handle_cmd, optarg);
	  break;
	case OPT_BOOLEAN:
	  if (optarg)
	    /* The user has specified a value -- use it. */
	    setoptval (opt->handle_cmd, optarg);
	  else
	    {
	      /* NEG is true for `--no-FOO' style boolean options. */
	      int neg = val & BOOLEAN_NEG_MARKER;
	      setoptval (opt->handle_cmd, neg ? "0" : "1");
	    }
	  break;
	case OPT__APPEND_OUTPUT:
	  setoptval ("logfile", optarg);
	  append_to_log = 1;
	  break;
	case OPT__HELP:
	  print_help ();
#ifdef WINDOWS
	  ws_help (exec_name);
#endif
	  exit (0);
	  break;
	case OPT__EXECUTE:
	  run_command (optarg);
	  break;
	case OPT__NO:
	  {
	    /* We support real --no-FOO flags now, but keep these
	       short options for convenience and backward
	       compatibility.  */
	    char *p;
	    for (p = optarg; *p; p++)
	      switch (*p)
		{
		case 'v':
		  setoptval ("verbose", "0");
		  break;
		case 'H':
		  setoptval ("addhostdir", "0");
		  break;
		case 'd':
		  setoptval ("dirstruct", "0");
		  break;
		case 'c':
		  setoptval ("noclobber", "1");
		  break;
		case 'p':
		  setoptval ("noparent", "1");
		  break;
		default:
		  printf (_("%s: illegal option -- `-n%c'\n"), exec_name, *p);
		  print_usage ();
		  printf ("\n");
		  printf (_("Try `%s --help\' for more options.\n"), exec_name);
		  exit (1);
		}
	    break;
	  }
	case OPT__PARENT:
	case OPT__CLOBBER:
	  {
	    /* The wgetrc commands are named noparent and noclobber,
	       so we must revert the meaning of the cmdline options
	       before passing the value to setoptval.  */
	    int flag = 1;
	    if (optarg)
	      flag = (*optarg == '1' || TOLOWER (*optarg) == 'y'
		      || (TOLOWER (optarg[0]) == 'o'
			  && TOLOWER (optarg[1]) == 'n'));
	    setoptval (opt->type == OPT__PARENT ? "noparent" : "noclobber",
		       flag ? "0" : "1");
	    break;
	  }
	case OPT__VERSION:
	  printf ("GNU Wget %s\n\n", version_string);
	  printf ("%s", _("\
Copyright (C) 2003 Free Software Foundation, Inc.\n"));
	  printf ("%s", _("\
This program is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
GNU General Public License for more details.\n"));
	  printf (_("\nOriginally written by Hrvoje Niksic <hniksic@xemacs.org>.\n"));
	  exit (0);
	  break;
	}

      longindex = -1;
    }

  /* All user options have now been processed, so it's now safe to do
     interoption dependency checks. */

  if (opt.reclevel == 0)
    opt.reclevel = INFINITE_RECURSION;  /* see wget.h for commentary on this */

  if (opt.page_requisites && !opt.recursive)
    {
      /* Don't set opt.recursive here because it would confuse the FTP
	 code.  Instead, call retrieve_tree below when either
	 page_requisites or recursive is requested.  */
      opt.reclevel = 0;
      if (!opt.no_dirstruct)
	opt.dirstruct = 1;	/* normally handled by cmd_spec_recursive() */
    }

  if (opt.verbose == -1)
    opt.verbose = !opt.quiet;

  /* Sanity checks.  */
  if (opt.verbose && opt.quiet)
    {
      printf (_("Can't be verbose and quiet at the same time.\n"));
      print_usage ();
      exit (1);
    }
  if (opt.timestamping && opt.noclobber)
    {
      printf (_("\
Can't timestamp and not clobber old files at the same time.\n"));
      print_usage ();
      exit (1);
    }
  nurl = argc - optind;
  if (!nurl && !opt.input_filename)
    {
      /* No URL specified.  */
      printf (_("%s: missing URL\n"), exec_name);
      print_usage ();
      printf ("\n");
      /* #### Something nicer should be printed here -- similar to the
	 pre-1.5 `--help' page.  */
      printf (_("Try `%s --help' for more options.\n"), exec_name);
      exit (1);
    }

  if (opt.background)
    fork_to_background ();

  /* Initialize progress.  Have to do this after the options are
     processed so we know where the log file is.  */
  if (opt.verbose)
    set_progress_implementation (opt.progress_type);

  /* Fill in the arguments.  */
  url = alloca_array (char *, nurl + 1);
  for (i = 0; i < nurl; i++, optind++)
    {
      char *rewritten = rewrite_shorthand_url (argv[optind]);
      if (rewritten)
	url[i] = rewritten;
      else
	url[i] = xstrdup (argv[optind]);
    }
  url[i] = NULL;

  /* Change the title of console window on Windows.  #### I think this
     statement should belong to retrieve_url().  --hniksic.  */
#ifdef WINDOWS
  ws_changetitle (*url, nurl);
#endif

  /* Initialize logging.  */
  log_init (opt.lfilename, append_to_log);

  DEBUGP (("DEBUG output created by Wget %s on %s.\n\n", version_string,
	   OS_TYPE));

  /* Open the output filename if necessary.  */
  if (opt.output_document)
    {
      if (HYPHENP (opt.output_document))
	opt.dfp = stdout;
      else
	{
	  struct stat st;
	  opt.dfp = fopen (opt.output_document, opt.always_rest ? "ab" : "wb");
	  if (opt.dfp == NULL)
	    {
	      perror (opt.output_document);
	      exit (1);
	    }
	  if (fstat (fileno (opt.dfp), &st) == 0 && S_ISREG (st.st_mode))
	    opt.od_known_regular = 1;
	}
    }

#ifdef WINDOWS
  ws_startup ();
#endif

  /* Setup the signal handler to redirect output when hangup is
     received.  */
#ifdef HAVE_SIGNAL
  if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
    signal(SIGHUP, redirect_output_signal);
  /* ...and do the same for SIGUSR1.  */
  signal (SIGUSR1, redirect_output_signal);
  /* Writing to a closed socket normally signals SIGPIPE, and the
     process exits.  What we want is to ignore SIGPIPE and just check
     for the return value of write().  */
  signal (SIGPIPE, SIG_IGN);
#ifdef SIGWINCH
  signal (SIGWINCH, progress_handle_sigwinch);
#endif
#endif /* HAVE_SIGNAL */

  status = RETROK;		/* initialize it, just-in-case */
  /* Retrieve the URLs from argument list.  */
  for (t = url; *t; t++)
    {
      char *filename = NULL, *redirected_URL = NULL;
      int dt;

      if ((opt.recursive || opt.page_requisites)
	  && url_scheme (*t) != SCHEME_FTP)
	status = retrieve_tree (*t);
      else
	status = retrieve_url (*t, &filename, &redirected_URL, NULL, &dt);

      if (opt.delete_after && file_exists_p(filename))
	{
	  DEBUGP (("Removing file due to --delete-after in main():\n"));
	  logprintf (LOG_VERBOSE, _("Removing %s.\n"), filename);
	  if (unlink (filename))
	    logprintf (LOG_NOTQUIET, "unlink: %s\n", strerror (errno));
	}

      xfree_null (redirected_URL);
      xfree_null (filename);
    }

  /* And then from the input file, if any.  */
  if (opt.input_filename)
    {
      int count;
      status = retrieve_from_file (opt.input_filename, opt.force_html, &count);
      if (!count)
	logprintf (LOG_NOTQUIET, _("No URLs found in %s.\n"),
		   opt.input_filename);
    }
  /* Print the downloaded sum.  */
  if (opt.recursive || opt.page_requisites
      || nurl > 1
      || (opt.input_filename && total_downloaded_bytes != 0))
    {
      logprintf (LOG_NOTQUIET,
		 _("\nFINISHED --%s--\nDownloaded: %s bytes in %d files\n"),
		 time_str (NULL), legible_large_int (total_downloaded_bytes),
		 opt.numurls);
      /* Print quota warning, if exceeded.  */
      if (opt.quota && total_downloaded_bytes > opt.quota)
	logprintf (LOG_NOTQUIET,
		   _("Download quota (%s bytes) EXCEEDED!\n"),
		   legible (opt.quota));
    }

  if (opt.cookies_output && wget_cookie_jar)
    cookie_jar_save (wget_cookie_jar, opt.cookies_output);

  if (opt.convert_links && !opt.delete_after)
    convert_all_links ();

  log_close ();
  for (i = 0; i < nurl; i++)
    xfree (url[i]);
  cleanup ();

#ifdef DEBUG_MALLOC
  print_malloc_debug_stats ();
#endif
  if (status == RETROK)
    return 0;
  else
    return 1;
}

#ifdef HAVE_SIGNAL
/* Hangup signal handler.  When wget receives SIGHUP or SIGUSR1, it
   will proceed operation as usual, trying to write into a log file.
   If that is impossible, the output will be turned off.

   #### It is unsafe to do call libc functions from a signal handler.
   What we should do is, set a global variable, and have the code in
   log.c pick it up.  */

static RETSIGTYPE
redirect_output_signal (int sig)
{
  char *signal_name = (sig == SIGHUP ? "SIGHUP" :
		       (sig == SIGUSR1 ? "SIGUSR1" :
			"WTF?!"));
  log_request_redirect_output (signal_name);
  progress_schedule_redirect ();
  signal (sig, redirect_output_signal);
}
#endif /* HAVE_SIGNAL */
