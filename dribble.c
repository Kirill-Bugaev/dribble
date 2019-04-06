#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>

#include "config.h"

#define UUIDCHARSET	"0123456789abcdefABCDEF-"
#define DEV			"/dev/"
#define DEVUUID		"/dev/disk/by-uuid/"

typedef struct {
	long int ball;
	char *hole;
	char *part;
	char *uuidpath;
	char label[PATH_MAX];
	long int pause;
	unsigned int useuuid: 1;
	unsigned int daemonize: 1;
	unsigned int verbose: 1;
} Options;

static void usage(const char *);
static void die(const char *, ...);
static void parsecmdargs(int, char *[]);
static void getlabel(void);
static int getmountpoint(char **);
static char *getfilepath(char *, size_t);
static void dribble(char *);

static Options opt = {
	.ball = defaultball,
	.hole = NULL,
	.part = NULL,
	.uuidpath = NULL,
	.label[0] = '\0',
	.pause = defaultpause,
	.useuuid = 0,
	.daemonize = 0,
	.verbose = 0,
};

#define HELPMSG 	"Try '%s --help' for more information.\n"
#define PROGPRFX 	"dribble: "
#define BALLERR		PROGPRFX "invalid ball value\n"
#define HOLEERR 	PROGPRFX "invalid hole value\n"
#define PAUSEERR	PROGPRFX "invalid pause value\n"
#define OPTERR		PROGPRFX "illegal option -- '%c'\n"
#define PARTERR		PROGPRFX "partition not specified\n"
#define UUIDERR		PROGPRFX "invalid uuid\n"
#define LABELERR	PROGPRFX "device label too long. PATH_MAX = %d\n"
#define ALLOCERR	PROGPRFX "can't allocate memory. errno=%d\n"
#define DAEMONERR	PROGPRFX "can't run daemon\n"
#define PROCOPENERR	PROGPRFX "can't open '/proc/mounts'. errno=%d\n"
#define PROCFMTERR	PROGPRFX "wrong '/proc/mounts' format\n"
#define FOPENERR	PROGPRFX "can't open '%s'. errno=%d\n"
#define FWRITEERR	PROGPRFX "can't write to '%s'. errno=%d\n"
#define STARTSCCSS	PROGPRFX "started for '%s' device \n"
#define FWRITESCCSS	PROGPRFX "file '%s' written successfully\n"
#define UUIDFOUND	PROGPRFX "'%s' device found: %s\n"
#define NOTUUID		PROGPRFX "'%s' device not found\n"
#define NOTMNT		PROGPRFX "'%s' device not mounted\n"

#define PERMS 0666

void
usage(const char *pn)
{
	printf("Usage: %s [OPTION]... PARTITION\n"
			"Write charecter specified by -b option ('\\%o' by default) in file\n"
		   	"specified by -h option ('%s' by default) on PARTITION each time\n"
		   	"interval (in seconds) specified by -p option (%ld by default).\n\n"
			"  -b CHARCODE \tball (written value), oct. 0-377\n"
			"  -h FILENAME\thole (name of file which will be written)\n"
			"  -p INTEGER\tpause\n"
			"  -u\tuuid specified instead of device label\n"
			"  -d\trun as daemon\n"
			"  -v\tprint verbose messages\n"
			"  --\tdisplay this help and exit\n", 
			pn, defaultball, defaulthole, defaultpause);
}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
parsecmdargs(int argc, char *argv[])
{
	char *progname = *argv, c, *endptr;
	size_t pl;

	while (--argc > 0) {
		if ((*++argv)[0] == '-') {
nextmergedarg:
			if (!(c = *++argv[0]))
				continue;
			switch (c) {
				case 'b':
					errno = 0;
					if (*++argv[0] != '\0')
						opt.ball = strtol(*argv, &endptr, 8);
					else if (--argc > 0)
						opt.ball = strtol(*++argv, &endptr, 8);
					else
						die(PAUSEERR HELPMSG, progname);
					if (errno != 0 || *endptr != '\0'
						   	|| opt.ball < 0 || opt.ball > UCHAR_MAX)
						die(BALLERR HELPMSG, progname);
					break;
				case 'h':
					if (*++argv[0] != '\0')
						opt.hole = *argv;
					else
						if (--argc > 0)
							opt.hole = *++argv;
						else
							die(HOLEERR HELPMSG, progname);
					break;
				case 'p':
					errno = 0;
					if (*++argv[0] != '\0')
						opt.pause = strtol(*argv, &endptr, 10);
					else if (--argc > 0)
						opt.pause = strtol(*++argv, &endptr, 10);
					else
						die(PAUSEERR HELPMSG, progname);
					if (errno != 0 || *endptr != '\0'
						   	|| opt.pause < 0 || opt.pause > UINT_MAX)
						die(PAUSEERR HELPMSG, progname);
					break;
				case 'u':
					opt.useuuid = 1;
					goto nextmergedarg;
					break;
				case 'd':
					opt.daemonize = 1;
					goto nextmergedarg;
					break;
				case 'v':
					opt.verbose = 1;
					goto nextmergedarg;
					break;
				case '-':
					usage(progname);
					exit(EXIT_SUCCESS);
					break;
				default:
					die(OPTERR HELPMSG, c, progname);
					break;
			}
		} else
			opt.part = *argv;
	}

	if (opt.part == NULL)
		die(PARTERR HELPMSG, progname);
	else if (opt.useuuid) {
		if (strcspn(opt.part, UUIDCHARSET))
			die(UUIDERR);
		if (!( opt.uuidpath = malloc(sizeof(DEVUUID) + strlen(opt.part)) ))
			die(ALLOCERR, errno);
		strcpy(opt.uuidpath, DEVUUID);
		strcat(opt.uuidpath, opt.part);
	} else {
		pl = strlen(opt.part);
		if (pl + 1 > PATH_MAX)
			die(LABELERR, PATH_MAX);
		/* Check dev prefix */
		if (strstr(opt.part, DEV) != opt.part) {
			if (sizeof(DEV) + pl > PATH_MAX)
				die(LABELERR, PATH_MAX);
			strcpy(opt.label, DEV);
			strcat(opt.label, opt.part);
		} else
			strcpy(opt.label, opt.part);
	}

	if (opt.hole == NULL)
		opt.hole = defaulthole;
}

void
getlabel(void)
{
	if (realpath(opt.uuidpath, opt.label)) {
		if (opt.verbose && !opt.daemonize)
			printf(UUIDFOUND, opt.part, opt.label);
	} else {
		opt.label[0] = '\0';
		if (opt.verbose && !opt.daemonize)
			printf(NOTUUID, opt.part);
	}
}

int
getmountpoint(char **mp)
{
	FILE *fp;
	char *line = NULL, *mpstart, *mpend, *mpp, *cp, *esc, *endptr;
	size_t ls = 0, mpl;
	int found = 0;

	if (!(fp = fopen("/proc/mounts", "r")))
		die(PROCOPENERR, errno);
					
	/* find partition in '/proc/mounts' */
	while (getline(&line, &ls, fp) != -1)
		if (strstr(line, opt.label) == line) {
			found = 1;
			break;
		}

	fclose(fp);
	if (!found) {
		free(line);	
		*mp = NULL;
		return -1;
	}

	/* parse mount point */
	mpstart = line + strlen(opt.label) + 1;
	if (!( mpend = strstr(mpstart, (char *) " ") ))
		die(PROCFMTERR);
	if (!( mpp = *mp = malloc(mpend - mpstart + 1) ))
		die(ALLOCERR, errno);

	/* replace escape sequences */
	esc = malloc(4);
	for (cp = mpstart; mpend - cp > 0; ++cp)
		if (*cp == '\\') {
			if (mpend - cp < 4)
				die(PROCFMTERR);
			strncpy(esc, cp + 1, 3);
			*(esc + 3) = '\0'; 
			errno = 0;
			*mpp++ = (char) strtol(esc, &endptr, 8);
			if (errno != 0 || *endptr != '\0')
				die(PROCFMTERR);
			cp += 3;
		} else
			*mpp++ = *cp;
	*mpp = '\0';
	mpl = mpp - *mp;
	if (!( *mp = realloc(*mp, mpl + 1) ))
		die(ALLOCERR, errno);
	free(esc);
	
	free(line);	
	return mpl;
}

char *
getfilepath(char *mp, size_t mpl)
{
	char *fpath;

	fpath = malloc(mpl + strlen(opt.hole) + 2);
	strcpy(fpath, mp);
	*(fpath + mpl) = '/';
	*(fpath + mpl + 1) = '\0';
	strcat(fpath, opt.hole);

	return fpath;
}

void
dribble(char *fpath)
{
	int fd;
	
	if ((fd = open(fpath, O_CREAT|O_WRONLY|O_SYNC, PERMS)) == -1) {
		if (opt.verbose && !opt.daemonize)
			fprintf(stderr, FOPENERR, fpath, errno);
		return;
	}

	if ((write(fd, &opt.ball, 1)) != 1 && opt.verbose && !opt.daemonize) 
		fprintf(stderr, FWRITEERR, fpath, errno);
	else if (opt.verbose && !opt.daemonize)
		printf(FWRITESCCSS, fpath);
	
	close(fd);
}

int
main(int argc, char *argv[])
{
	char *mp, *fpath;
	size_t mpl;

	parsecmdargs(argc, argv);

	if (opt.daemonize && daemon(0, 0) == -1)
		die(DAEMONERR);
	
	if (opt.verbose && !opt.daemonize)
		printf(STARTSCCSS, opt.part);

	while (1) {
		if (opt.useuuid)
			getlabel();
		if (*opt.label)
			if ((mpl = getmountpoint(&mp)) != -1) {
				fpath = getfilepath(mp, mpl);
				dribble(fpath);
				free(fpath);
				free(mp);
			} else if (opt.verbose && !opt.daemonize)
				printf(NOTMNT, opt.part);
		sleep(opt.pause);
	}
}
