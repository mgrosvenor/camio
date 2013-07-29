/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio netmap (newline separated) output stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <linux/ethtool.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>


#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../clocks/camio_time.h"
#include "../netmap/netmap.h"
#include "../netmap/netmap_user.h"
#include "../stream_description/camio_opt_parser.h"

#include "camio_ostream_netmap.h"

//static void hex_dump(void *data, int size)
//{
//    /* dumps size bytes of *data to stdout. Looks like:
//     * [0000] 75 6E 6B 6E 6F 77 6E 20
//     *                  30 FF 00 00 00 00 39 00 unknown 0.....9.
//     * (in a single line of course)
//     */
//
//    unsigned char *p = data;
//    unsigned char c;
//    int n;
//    char bytestr[4] = {0};
//    char addrstr[10] = {0};
//    char hexstr[ 16*3 + 5] = {0};
//    char charstr[16*1 + 5] = {0};
//    for(n=1;n<=size;n++) {
//        if (n%16 == 1) {
//            /* store address for this line */
//            snprintf(addrstr, sizeof(addrstr), "%.4x",
//               ((unsigned int)p-(unsigned int)data) );
//        }
//
//        c = *p;
//        if (isalnum(c) == 0) {
//            c = '.';
//        }
//
//        /* store hex str (for left side) */
//        snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
//        strncat(hexstr, bytestr, sizeof(hexstr)-strlen(hexstr)-1);
//
//        /* store char str (for right side) */
//        snprintf(bytestr, sizeof(bytestr), "%c", c);
//        strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);
//
//        if(n%16 == 0) {
//            /* line completed */
//            printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
//            hexstr[0] = 0;
//            charstr[0] = 0;
//        } else if(n%8 == 0) {
//            /* half line: add whitespaces */
//            strncat(hexstr, "  ", sizeof(hexstr)-strlen(hexstr)-1);
//            strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
//        }
//        p++; /* next byte */
//    }
//
//    if (strlen(hexstr) > 0) {
//        /* print rest of buffer if not empty */
//        printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
//    }
//}

static void do_ioctl_flags(const char* ifname, size_t flags)
{
    int socket_fd;
    struct ifreq ifr;

    //Get a control socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        eprintf_exit( "Could not open device control socket\n");
    }

    bzero(&ifr, sizeof(&ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    ifr.ifr_flags = flags & 0xffff;

    if( ioctl(socket_fd, SIOCSIFFLAGS, &ifr) ){
        eprintf_exit( "Could not set interface flags 0x%08x\n", flags);
    }

    close(socket_fd);
}


static void do_ioctl_ethtool(const char* ifname, int subcmd)
{

    int socket_fd;
    struct ifreq ifr;

    //Get a control socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        eprintf_exit( "Could not open device control socket\n");
    }

    bzero(&ifr, sizeof(&ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    struct ethtool_value eval;
    eval.cmd = subcmd;
    eval.data = 0;
    ifr.ifr_data = (caddr_t)&eval;

    if( ioctl(socket_fd, SIOCETHTOOL, &ifr) ){
        eprintf_exit( "Could not set ethtool value\n");
    }

}

int camio_ostream_netmap_open(camio_ostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_ostream_netmap_t* priv = this->priv;
    int netmap_fd = -1;
    struct nmreq req;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;



    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(unlikely(!descr->query)){
        eprintf_exit( "No device supplied\n");
    }

    const char* iface = descr->query;

    //Open the netmap device
    netmap_fd = open("/dev/netmap", O_RDWR);
    //printf("Opening %s\n", "/dev/netmap");
    if(unlikely(netmap_fd < 0)){
        eprintf_exit( "Could not open file \"%s\". Error=%s\n", "/dev/netmap", strerror(errno));
    }

    //Request a specific interface
    bzero(&req, sizeof(req));
    req.nr_version = NETMAP_API;
    strncpy(req.nr_name, iface, sizeof(req.nr_name));
    req.nr_ringid = 0; //All hw rings

    if(ioctl(netmap_fd, NIOCGINFO, &req)){
        eprintf_exit( "Could not get info on netmap interface %s\n", iface);
    }

    //printf("IF = %s\n", req.nr_name);

    priv->mem_size = req.nr_memsize;

    if(ioctl(netmap_fd, NIOCREGIF, &req)){
        eprintf_exit( "Could not register netmap interface %s\n", iface);
    }

//    printf("Memsize  = %u\n"
//            "Ringid   = %u\n"
//            "Offset   = %u\n"
//            "tx rings = %u\n"
//            "tx slots = %u\n"
//			"pbuffs   = %u\n",
//            req.nr_memsize / 1024 / 1204,
//            req.nr_ringid,
//            req.nr_offset,
//            req.nr_tx_rings,
//            req.nr_tx_slots,
//            req.nr_tx_rings * req.nr_tx_slots);
//

    if(priv->params){
        priv->burst_size = priv->params->burst_size;
    }
    else{
        priv->burst_size = 0;
    }

    //printf("Setting burst size to %u\n", priv->burst_size);

    if(priv->params && priv->params->nm_mem){
        priv->nm_mem   = priv->params->nm_mem;
        priv->mem_size = priv->params->nm_mem_size;
        netmap_fd      = priv->params->fd;
    }
    else{
        priv->nm_mem = mmap(0, priv->mem_size, PROT_WRITE | PROT_READ, MAP_SHARED, netmap_fd, 0);
        if(unlikely(priv->nm_mem == MAP_FAILED)){
            eprintf_exit( "Could not memory map blob file \"%s\". Error=%s\n", descr->query, strerror(errno));
        }
    }


    //Make sure the interface is up and promiscuious
    do_ioctl_flags(iface, IFF_UP | IFF_PROMISC );

    //Turn off all the offload features on the card
    do_ioctl_ethtool(iface, ETHTOOL_SGSO);
    do_ioctl_ethtool(iface, ETHTOOL_STSO);
    do_ioctl_ethtool(iface, ETHTOOL_SRXCSUM);
    do_ioctl_ethtool(iface, ETHTOOL_STXCSUM);

    priv->nifp  = NETMAP_IF(priv->nm_mem, req.nr_offset);
    priv->begin = 0;
    priv->end   = req.nr_tx_rings;

    //Netmap lays out memory so that all packet buffers are interchangeable.
    //All packets begin at packet_buffer_bottom and are arranged in an array
    //of 2K offset from there. We need to get the address of the bottom of this
    //to calculate relative packets
    struct netmap_ring *txring = NETMAP_TXRING(priv->nifp, priv->begin);
    priv->packet_buff_bottom = ((uint8_t *)(txring) + (txring)->buf_ofs);

    //Get ready to use the packet buffs
    size_t i = 0;
    for(i = 0; i < req.nr_tx_rings;i++){
		priv->rings[i] = req.nr_tx_slots;
    }
    priv->total_slots		= req.nr_tx_rings * req.nr_tx_rings;
   //priv->available_buffs	= req.nr_tx_rings * req.nr_tx_rings;
    priv->ring_num			= 0;
    priv->slot_num			= priv->rings[priv->ring_num];
    priv->ring_count		= req.nr_tx_rings;
    priv->slot_count		= req.nr_tx_slots;

    this->fd = netmap_fd;
    priv->is_closed = 0;


    return 0;
}

void camio_ostream_netmap_close(camio_ostream_t* this){
    camio_ostream_netmap_t* priv = this->priv;
    priv->is_closed = 1;
   
    //printf("Flushing...\n"); 
    /* flush any remaining packets */
    ioctl(this->fd, NIOCTXSYNC, NULL);

    /* final part: wait all the TX queues to be empty. */
    size_t i = 0; 
    for (i = priv->begin; i < priv->end; i++) {
        struct netmap_ring* txring = NETMAP_TXRING(priv->nifp, i);
        while (!NETMAP_TX_RING_EMPTY(txring)) {
            if(ioctl(this->fd, NIOCTXSYNC, NULL) < 0){
                goto done;
            }
            usleep(1); /* wait 1 tick */
        }
    }

   //printf("Done\n\n");

//    if(priv->nm_mem){
//        munmap(priv->nm_mem, priv->mem_size);
//        priv->nm_mem   = NULL;
//        priv->mem_size = 0;
//    }
done:
    ioctl(this->fd, NIOCUNREGIF, NULL);
    //close(this->fd);
}



struct netmap_slot* get_free_slot(camio_ostream_t* this ){
    camio_ostream_netmap_t* priv = this->priv;
    struct netmap_slot* result = NULL;
    struct pollfd fds[1];
    fds[0].fd = this->fd;
    fds[0].events = (POLLOUT);


    size_t i;
    while(1){
		for (i = priv->begin; i < priv->end; i++) {
			//ringid = (i + offset) % priv->end;
			struct netmap_ring *ring = NETMAP_TXRING(priv->nifp, i);
			if(ring->avail != 0){
				ring->avail--;
				priv->ring_num = i;
				priv->slot_num = ring->cur;
				result = &ring->slot[ring->cur];
                if(ring->avail == 0){
				     //printf("Setting report\n");
                     result->flags |= NS_REPORT;
                }
				//printf("(Avail =%u)Packet buffer of size %lu is available on ring %lu at slot %u (%u) buff idx=%u\n", ring->avail, result->len, i, ring->cur,ring->avail, result->buf_idx );
                //printf("Slot at buffer ifx=%u\n",  result->buf_idx ); 
				return result;
			}
        }
        
        if(poll(fds, 1, 10000) <= 0){
		    eprintf_exit("Failed on poll %s\n", strerror(errno));
        }

    }

    return result;

}

static inline uint8_t* get_buffer(camio_ostream_netmap_t* priv, struct netmap_slot* slot){


    struct netmap_ring *ring = NETMAP_TXRING(priv->nifp, priv->ring_num);
    
    priv->packet = NETMAP_BUF(ring, slot->buf_idx);
    priv->packet_size = ring->slot->len;
    

    return  priv->packet;

}

//Returns a pointer to a space of size len, ready for data
uint8_t* camio_ostream_netmap_start_write(camio_ostream_t* this, size_t len ){
    camio_ostream_netmap_t* priv = this->priv;



 if(len > 2 * 1024){
        return NULL;
    }

    if(unlikely((size_t)priv->packet)){
        return priv->packet;
    }

    struct netmap_slot* slot = get_free_slot(this);
    return get_buffer(priv, slot);
}

//Returns non-zero if a call to start_write will be non-blocking
int camio_ostream_netmap_ready(camio_ostream_t* this){
    //Not implemented
    eprintf_exit( "\n");
    return 0;
}



//Commit the data to the buffer previously allocated
//Len must be equal to or less than len called with start_write
//returns a pointer to a
size_t pcount = 0; 
uint8_t* camio_ostream_netmap_end_write(camio_ostream_t* this, size_t len){
    camio_ostream_netmap_t* priv = this->priv;
    void* result = NULL;   

    if(unlikely(len > 2* 1024)){
        eprintf_exit( "The supplied length %lu is greater than the buffer size %lu\n", len, priv->packet_size);
    }

    //This is the fast path, for zero copy operation need and assigned buffer
    if(likely((size_t)priv->assigned_buffer)){
        struct netmap_slot* slot = get_free_slot(this);
        uint8_t* buffer = get_buffer(priv,  slot);

        //That comes from the netmap buffer range
        if(likely(priv->assigned_buffer >= priv->packet_buff_bottom && priv->assigned_buffer <= priv->nm_mem + priv->mem_size)){
            size_t offset = (priv->assigned_buffer - priv->packet_buff_bottom) / 2048;

            //printf("ostream: fast path with offset=%lu --input from istream\n", offset);
            camio_perf_event_stop(priv->perf_mon, CAMIO_PERF_EVENT_OSTREAM_NETMAP, CAMIO_PERF_COND_WRITE_ASSIGNED);
            slot->buf_idx = offset;
            slot->len     = len;
            slot->flags  |= NS_BUF_CHANGED;
            result        = buffer; //We've taken the buffer from the assignment and are holding on to it so give back another one
        
            //Reset everything
            struct netmap_ring *ring = NETMAP_TXRING(priv->nifp, priv->ring_num);
            ring->cur                = NETMAP_RING_NEXT(ring, ring->cur);
            priv->packet             = NULL;
            priv->packet_size        = 0;
            priv->assigned_buffer    = NULL;
            
            goto done;
        }
        else{
            camio_perf_event_stop(priv->perf_mon, CAMIO_PERF_EVENT_OSTREAM_NETMAP, CAMIO_PERF_COND_WRITE);
            //No fast path, looks like we have to copy
            memcpy(buffer,priv->assigned_buffer,len);
            priv->assigned_buffer    = NULL;
            priv->assigned_buffer_sz = 0;
            //Fall through to start_write() handling below
        }
    }

    struct netmap_ring *ring = NETMAP_TXRING(priv->nifp, priv->ring_num);
    ring->slot[priv->slot_num].len = len;
    ring->cur = NETMAP_RING_NEXT(ring, ring->cur);
    priv->packet = NULL;
    priv->packet_size = 0;

done:    
    if(unlikely(priv->burst_size && pcount && pcount % priv->burst_size == 0)){
        ioctl(this->fd, NIOCTXSYNC, NULL);
    }
    pcount++;


    return result;
}


void camio_ostream_netmap_flush(camio_ostream_t* this){
    camio_ostream_netmap_t* priv = this->priv;

     //printf("Flushing...\n");
     /* flush any remaining packets */
     ioctl(this->fd, NIOCTXSYNC, NULL);

     /* final part: wait all the TX queues to be empty. */
     size_t i = 0;
     for (i = priv->begin; i < priv->end; i++) {
         struct netmap_ring* txring = NETMAP_TXRING(priv->nifp, i);
         while (!NETMAP_TX_RING_EMPTY(txring)) {
             ioctl(this->fd, NIOCTXSYNC, NULL);
             usleep(1); /* wait 1 tick */
         }
     }

    //printf("Done\n\n");


}



void camio_ostream_netmap_delete(camio_ostream_t* ostream){
    ostream->close(ostream);
    camio_ostream_netmap_t* priv = ostream->priv;
    free(priv);
}

//Is this stream capable of taking over another stream buffer
int camio_ostream_netmap_can_assign_write(camio_ostream_t* this){
    return 0;
}

//Assign the write buffer to the stream
int camio_ostream_netmap_assign_write(camio_ostream_t* this, uint8_t* buffer, size_t len){
    camio_ostream_netmap_t* priv = this->priv;

    if(!buffer){
        eprintf_exit("Assigned buffer is null.");
    }

    priv->assigned_buffer    = buffer;
    priv->assigned_buffer_sz = len;

    return 0;
}


/* ****************************************************
 * Construction heavy lifting
 */

camio_ostream_t* camio_ostream_netmap_construct(camio_ostream_netmap_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_netmap_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("netmap stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed             = 0;
    priv->nm_mem                = NULL;
    priv->mem_size              = 0;
    priv->nifp                  = NULL;
    priv->tx                    = NULL;
    priv->begin                 = 0;
    priv->end                   = 0;
    //priv->available_buffs       = 0;
    priv->packet                = 0;
    priv->packet_size           = 0;
    priv->slot_num              = 0;
    priv->ring_num              = 0;
    priv->assigned_buffer       = NULL;
    priv->assigned_buffer_sz    = 0;
    priv->packet_buff_bottom    = NULL;
    priv->params                = params;
    priv->burst_size            = 0;


    //Populate the function members
    priv->ostream.priv              = priv; //Lets us access private members from public functions
    priv->ostream.open              = camio_ostream_netmap_open;
    priv->ostream.close             = camio_ostream_netmap_close;
    priv->ostream.start_write       = camio_ostream_netmap_start_write;
    priv->ostream.end_write         = camio_ostream_netmap_end_write;
    priv->ostream.ready             = camio_ostream_netmap_ready;
    priv->ostream.delete            = camio_ostream_netmap_delete;
    priv->ostream.can_assign_write  = camio_ostream_netmap_can_assign_write;
    priv->ostream.assign_write      = camio_ostream_netmap_assign_write;
    priv->ostream.flush             = camio_ostream_netmap_flush;
    priv->ostream.clock             = clock;
    priv->ostream.fd                = -1;

    //Call open, because its the obvious thing to do now...
    priv->ostream.open(&priv->ostream, descr, perf_mon);

    //Return the generic ostream interface for the outside world
    return &priv->ostream;

}

camio_ostream_t* camio_ostream_netmap_new( const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_netmap_params_t* params, camio_perf_t* perf_mon){
    camio_ostream_netmap_t* priv = malloc(sizeof(camio_ostream_netmap_t));
    if(!priv){
        eprintf_exit("No memory available for ostream netmap creation\n");
    }
    return camio_ostream_netmap_construct(priv, descr, clock, params, perf_mon);
}



