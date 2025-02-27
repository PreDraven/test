#ifndef UDPCAST_CONFIG_H
# define UDPCAST_CONFIG_H
# include "config.h"
#endif

#if SIZEOF_OFF_T < 8
# ifdef HAVE_LSEEK64
#  define NEED_LSEEK64 1
# endif
#endif

#ifdef NEED_LSEEK64
#  ifndef _LARGEFILE64_SOURCE
#   define _LARGEFILE64_SOURCE
#  endif
#endif


#include <unistd.h>
#include "log.h"
#include "statistics.h"
#include "util.h"
#include "socklib.h"
#include "udpcast.h"

#include <fcntl.h>
#include <errno.h>

/**
 * Common part between receiver and sender stats
 */
struct stats {
    int fd;
    struct timeval lastPrinted;
    long statPeriod;
    int printUncompressedPos;
    int noProgress;
};

struct receiver_stats {
    struct timeval tv_start;
    int bytesOrig;
    unsigned long long totalBytes;
    int timerStarted;
    struct stats s;
};

/**
 * Answers whether statistics should be printed now
 */
static int shouldPrint(struct stats *s, struct timeval *now, int isFinal) {
    long long sinceLastPrint;
    if(isFinal)
	return 1;
    sinceLastPrint =
	(now->tv_sec - s->lastPrinted.tv_sec) * 1000000 +
	(now->tv_usec - s->lastPrinted.tv_usec);
    if(sinceLastPrint < s->statPeriod)
	return 0;
    s->lastPrinted = *now;
    return 1;
}

static void initStats(struct stats *s,
		      int fd, long statPeriod, int printUncompressedPos,
		      int noProgress)
{
    struct timeval now;
    gettimeofday(&now, 0);
    s->fd = fd;
    s->statPeriod = statPeriod;    
    s->printUncompressedPos = printUncompressedPos;
    s->lastPrinted = now;
    s->noProgress = noProgress;
}

receiver_stats_t allocReadStats(int fd,
				long statPeriod,
				int printUncompressedPos,
				int noProgress) {
    receiver_stats_t rs =  MALLOC(struct receiver_stats);
    initStats(&rs->s, fd, statPeriod, printUncompressedPos, noProgress);
    return rs;
}

void receiverStatsAddBytes(receiver_stats_t rs, unsigned long bytes) {
    if(rs != NULL)
	rs->totalBytes += bytes;
}

void receiverStatsStartTimer(receiver_stats_t rs) {
    if(rs != NULL && !rs->timerStarted) {
	gettimeofday(&rs->tv_start, 0);
	rs->timerStarted = 1;
    }
}

static void printFilePosition(int fd)
{
    if(fd != -1) {
#ifdef __linux__
	char fn[80];
	int pfd;
	sprintf(fn, "/proc/self/fdinfo/%d", fd);
	pfd=open(fn, O_RDONLY);
	if(pfd != -1) {
	    char buf[161];
	    ssize_t n;
	    n=read(pfd, buf, 160);
	    if(n >= 0) {
		char *num;
		long long offset;
		buf[n]='\0';
		num = strpbrk(buf, "0123456789");
		offset = strtoll(num, 0, 10);
		if(offset >= 0)
			printLongNum((unsigned long long)offset);
	    }
	    close(pfd);
	} else {
	    fprintf(stderr, "%s --> %s\n", fn, strerror(errno));
	}
#else
#ifdef NEED_LSEEK64
	long long offset = lseek64(fd, 0, SEEK_CUR);
	if(offset != -1)
	    printLongNum(offset);
#else
	off_t offset = lseek(fd, 0, SEEK_CUR);
	if(offset != -1)
#ifdef __MINGW32__
	    fprintf(stderr, "%10lld", offset);
#else
	    fprintf(stderr, "%10d", offset);
#endif
#endif
#endif
    }
}


int udpc_shouldPrintUncompressedPos(int deflt, int fd, int ref)
{
    if(deflt != -1)
	return deflt;
    if(ref == fd)
	return 0; /* No pipe used => printing "uncompressed" statistics would be
		     redundant */
    {
#ifdef NEED_LSEEK64
	long long offset = lseek64(fd, 0, SEEK_CUR);
#else
	off_t offset = lseek(fd, 0, SEEK_CUR);
#endif
	if(offset != -1)
	    return 1;
    }
    return 0;
}

void displayReceiverStats(receiver_stats_t rs, int isFinal) {
    long long timePassed;
    struct timeval tv_now;

    if(rs == NULL || rs->s.noProgress)
	return;

    gettimeofday(&tv_now, 0);
    if(!shouldPrint(&rs->s, &tv_now, isFinal))
	return;

    fprintf(stderr, "bytes=");
    printLongNum(rs->totalBytes);
    fprintf(stderr, " (");
    
    timePassed = tv_now.tv_sec - rs->tv_start.tv_sec;
    timePassed *= 1000000;
    timePassed += tv_now.tv_usec - rs->tv_start.tv_usec;
    if (timePassed > 0) {
	    int mbps = (int) (rs->totalBytes * 800 /
			      (unsigned long long) timePassed);
	fprintf(stderr, "%3d.%02d", mbps / 100, mbps % 100);
    } else {
	fprintf(stderr, "***.**");
    }
    fprintf(stderr, " Mbps)");
    if(rs->s.printUncompressedPos)
	printFilePosition(rs->s.fd);
    fprintf(stderr, "\r");
    fflush(stderr);
}


struct sender_stats {
    FILE *log;    
    unsigned long long totalBytes;
    unsigned long long retransmissions;
    uint32_t clNo;
    unsigned long periodBytes;
    struct timeval periodStart;
    long bwPeriod;
    struct stats s;
};

sender_stats_t allocSenderStats(int fd, FILE *logfile, long bwPeriod,
				long statPeriod, int printUncompressedPos,
				int noProgress) {
    sender_stats_t ss = MALLOC(struct sender_stats);
    ss->log = logfile;
    ss->bwPeriod = bwPeriod;
    gettimeofday(&ss->periodStart, 0);
    initStats(&ss->s, fd, statPeriod, printUncompressedPos, noProgress);
    return ss;
}

void senderStatsAddBytes(sender_stats_t ss, unsigned long bytes) {
    if(ss != NULL) {
	ss->totalBytes += bytes;

	if(ss->bwPeriod) {
	    long double tdiff, bw;
	    struct timeval tv;
	    gettimeofday(&tv, 0);
	    ss->periodBytes += bytes;
	    if(tv.tv_sec - ss->periodStart.tv_sec < ss->bwPeriod-1)
		return;
	    tdiff = (tv.tv_sec-ss->periodStart.tv_sec) * 1000000.0L +
		    (tv.tv_usec - ss->periodStart.tv_usec);
	    if(tdiff < ss->bwPeriod * 1000000.0L)
		return;
	    bw = ss->periodBytes * 8.0L / tdiff;
	    ss->periodBytes=0;
	    ss->periodStart = tv;
	    logprintf(ss->log, "Inst BW=%f\n", (double) bw);
	    fflush(ss->log);
	}
    }
}

void senderStatsAddRetransmissions(sender_stats_t ss,
				   unsigned int retransmissions) {
    if(ss != NULL) {
	ss->retransmissions += retransmissions;
#ifdef __MINGW32__
	logprintf(ss->log, "RETX %9I64d %4d\n", ss->retransmissions, 
		  retransmissions);
#else
	logprintf(ss->log, "RETX %9lld %4d\n", ss->retransmissions, 
		  retransmissions);
#endif
    }
}


void displaySenderStats(sender_stats_t ss, unsigned int blockSize,
			unsigned int sliceSize, int isFinal) {
    unsigned long long blocks;
    unsigned int permille;
    struct timeval tv_now;
    
    if(ss == NULL || ss->s.noProgress)
	return;

    gettimeofday(&tv_now, 0);
    if(!shouldPrint(&ss->s, &tv_now, isFinal))
	return;

    blocks = (ss->totalBytes + blockSize - 1) / blockSize;
    if(blocks == 0)
	permille = 0;
    else
	permille = (unsigned int)((1000L * ss->retransmissions) / blocks);
    
    fprintf(stderr, "bytes=");
    printLongNum(ss->totalBytes);
    fprintf(stderr, " re-xmits=%07llu (%3u.%01u%%) slice=%04d ",
	    ss->retransmissions, permille / 10, permille % 10, sliceSize);
    if(ss->s.printUncompressedPos)
	printFilePosition(ss->s.fd);
    fprintf(stderr, "- %3d\r", ss->clNo);
    fflush(stderr);
}

void senderSetAnswered(sender_stats_t ss, uint32_t clNo) {
    if(ss != NULL)
	ss->clNo = clNo;
}
