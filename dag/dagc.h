/*
 * Copyright (c) 2003
 * NetGroup, Politecnico di Torino (Italy)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define DAGC_ERRBUF_SIZE   512
#define FILEBUFSIZE         65536
#define MAXDAGCARDS         32

#ifndef _WIN32

typedef long long         long_long;
typedef long long         ull_t;
#define TRUE            1
#define devicestring      "/dev/dag%d\0"
#define dagc_sleepms(_MS)   usleep(_MS * 1000)
#else /* _WIN32 */

typedef LONGLONG         long_long;
typedef ULONGLONG         ull_t;
#define dagc_sleepms(_MS)   Sleep(_MS)
#define devicestring      "\\\\.\\dag%d\0"
#define device_streamstring      "\\\\.\\dag%d:%d\0"


#endif /* _WIN32 */

#define MIN_DAG_SNAPLEN      12
#define MAX_DAG_SNAPLEN      2040

#define erffilestring      "erffile://"


#define ATM_SNAPLEN         48
/* Size of ATM payload */
#define ATM_WLEN(h)         ATM_SNAPLEN
#define ATM_SLEN(h)         ATM_SNAPLEN

/* Size Ethernet payload */
#define ETHERNET_WLEN(h, b)   ((u_int)ntohs((h)->wlen) - ((b) >> 3))
#define ETHERNET_SLEN(h, b)   min(ETHERNET_WLEN(h, b), \
                (u_int)ntohs((h)->rlen) - dag_record_size - 2)

/* Size of HDLC payload */
#define HDLC_WLEN(h, b)      ((u_int)ntohs((h)->wlen) - ((b) >> 3))
#define HDLC_SLEN(h, b)      min(HDLC_WLEN(h, b), \
                (u_int)ntohs((h)->rlen) - dag_record_size)

/* Flags for dagc_open */
#define DAGC_OPEN_SHARED   1
#define DAGC_OPEN_EXCLUSIVE 2

/*
 * Card statistics.
 */
typedef struct dagc_stats_t
{
    ull_t received;            /* (NOT IMPLEMENTED) total number of frames received by the DAG */
    ull_t dropped;            /* number of frames dropped for buffer full */
    ull_t captured;            /* (NOT IMPLEMENTED) number of frames that actually reach the 
                                 application, i.e that are not filtered or dropped */
} dagc_stats_t;

/*
 * Descriptor of an open session.
 * Note: the dagc_t descriptor is completely opaque to the application. It can be compared 
 * to a file descriptor.
 */
typedef struct dagc dagc_t;

/*
 * Card description.
 */
typedef struct dagc_if_t
{
   struct   dagc_if_t *next;
   char   *name;               /* pointer to a string to pass to dagc_open*/
   char   *description;         /* human-understandable description (e.g. Endace 3.5e Fast 
                              Ethernet Card) */
} dagc_if_t;



#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/*
 * returns a string with last dagc lib error
 */
char* dagc_getlasterror(dagc_t *dagcfd);


/*
 * returns a linked list with the cards available on the systems. For every card, it scans the 
 * card type and converts it to a human-understandable string, in order to provide a description 
 * useful for example when a system has more than one card
 */
int dagc_finddevs (dagc_if_t **alldevsp, char *ebuf);


/*
 * frees the card list.
 */
void dagc_freedevs (dagc_if_t *alldevsp);

   
/*
 * Opens a card (or a file) for capture. Snaplen is the portion of packet delivered to the 
 * application, flags can contain specific settings (for example promisc mode??), minbufsize 
 * is the smallest buffer that the API can provide to the application (to limit CPU waste 
 * with several small buffers under moderated network  throughputs)
 */
dagc_t* dagc_open(const char *source, unsigned flags, char *ebuf);

/*
 * Sets the snaplen of a card
 * Returns -1 on failure. On success, the actual snaplen is returned (snap len has to be a multiple of 4
 * with DAG cards).
 */
int dagc_setsnaplen(dagc_t *dagcfd, unsigned snaplen);
   
/*
 * closes a capture instance
 */
void dagc_close(dagc_t *dagcfd);


/*
 * returns the linktype of a card
 */
int dagc_getlinktype(dagc_t *dagcfd);


/*
 * returns the link speed of the adapter, in MB/s.
 * If the link speed of the card is unknown, -1 is returned.
 * XXX NOTE: Currently, there is no consistent way to get linkspeed querying the card.
 * As a consequence, we determine this value statically from the card model. For cards that can run at
 * different speeds, we report only the *maximum* speed.
 */
int dagc_getlinkspeed(dagc_t *dagcfd);


/*
 * Returns the length of the CRC checksum that the card associates with any packet in the hole. This 
 * information will be used to understand the actual length of the packet on the wire.
 * Note: this information is not provided consistently by DAG cards, so we gather it from an environment
 * variable in Unix and from a registry key in Windows.
 */
unsigned dagc_getfcslen(dagc_t *dagcfd);

/*
 * provides a buffer with the new packets (from the board or from the file) and its size. 
 * On success, the return value is 0. If an error has occurred, the return value is -1.
 * If EOF has reached, the return value is -2. Note that this function always returns 
 * immediately, eventually with an empty buffer, so it is possible to have a success (0)
 * return value and bufsize = 0.
 */
int dagc_receive(dagc_t *dagcfd, u_char **buffer, u_int *bufsize);


/*
 * returns nonzero if any data is available from dagcfd, -1 if an error occurred. Waits until almost the time 
 * specified by timeout has past or any data is available. If timeout=0, returns immediately. 
 * If timeout=NULL, blocks until a packet arrives.
 */
int dagc_wait(dagc_t *dagcfd, struct timeval *timeout);


/*
 * returns statistics about current capture session
 */
int dagc_stats(dagc_t *dagcfd, dagc_stats_t *ps);


/*
 * Opens a dump file to store the data of this capture.
 * Returns 0 on success.
 * NOTE: currently, dagc_dumpfile_open, dagc_dumpfile_close and dagc_dump are simply wrappers
 * for open, close and write. However, if the programmer uses these functions, he is more protected
 * against file format changes (for example if the file format will have an header in the future).
 * Moreover, assuming that the user knows the file format is a bad practice: providing 
 * simple simple save functionality is more intutive and user-friendly.
 */
int dagc_dumpfile_open(dagc_t *dagcfd, char* name);


/*
 * Closes a dump file
 */
int dagc_dumpfile_close(dagc_t *dagcfd);


/*
 * Writes a buffer of packets to a dump file
 * Returns 0 on success.
 */
int dagc_dump(dagc_t *dagcfd, u_char *buffer, u_int bufsize);


#ifdef __cplusplus
}
#endif /* __cplusplus */

/*
 * Descriptor of an open session.
 * Note: the dagc_t descriptor is completely opaque to the application. It can be compared 
 * to a file descriptor.
 */
struct dagc 
{
   dagc_t *next;
   int			fd;			/* handle to the open device if this is a live capture, 
							otherwise NULL */
   FILE			*infile;    /* pointer to the file if this is an offline capture, 
							otherwise NULL */
   char			*buf;       /* Buffer containing the packets */
   u_int		bufsize;    /* Size of buffer containing the packets */
   u_int		borrow;     /* Size of buffer containing the packets */
   int			linktype;   /* libpcap style linktype. NOTE: for DLT_PPP_SERIAL encapsulation, 
							linktype=-TYPE_HDLC_POS. MINUS!! */
   u_int		snaplen;    /* portion of the packet actually captured */
   FILE			*dumpfile;  /* file used to save the packets */

   char			*hole;      /* DAG card memory base address */
   u_int		holesize;   /* for PBMOFFSET */
   u_int		holebottom; /* Current position inside the hole */
   u_int		holetop;    /* Current end of buffer inside the hole */   
   dag_reg_t	*regs;      /* Card registers */
   u_char		*iom;       /* IO memory pointer */
   unsigned		brokencuraddr;	/* for PBMOFFSET */
   unsigned		byteswap;	/* endinness for 3.4/3.51ecm */
   char			name[32];   /* device name */
   char			errbuf[DAGC_ERRBUF_SIZE + 1]; /* for functions error strings. Far better than 
									       panic */
   u_int		flags;		/* Information about how the card should operate*/
   dagc_stats_t stats;		/* Statistcs about current session */
   u_int        stream;     /* stream number to use */
};

struct stream_sts
{  
	uint8_t started;
	uint8_t opened;
	uint8_t attached;
	uint32_t pid_attch;
	uint32_t pid_start;
	uint8_t lock_cnt_a;
	uint8_t lock_cnt_s;
};
