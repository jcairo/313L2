#include <cnet.h>
#include <stdlib.h>
#include <string.h>

/*  This is an implementation of a stop-and-wait data link protocol.
    It is based on Tanenbaum's `protocol 4', 2nd edition, p227
    (or his 3rd edition, p205).
    This protocol employs only data and acknowledgement frames -
    piggybacking and negative acknowledgements are not used.

    It is currently written so that only one node (number 0) will
    generate and transmit messages and the other (number 1) will receive
    them. This restriction seems to best demonstrate the protocol to
    those unfamiliar with it.
    The restriction can easily be removed by "commenting out" the line

	    if(nodeinfo.nodenumber == 0)

    in reboot_node(). Both nodes will then transmit and receive (why?).

    Note that this file only provides a reliable data-link layer for a
    network of 2 nodes.
 */

typedef enum    { DL_DATA, DL_ACK }   FRAMEKIND;

typedef struct {
    char        data[MAX_MESSAGE_SIZE];
} MSG;

typedef struct {
    FRAMEKIND    kind;      	// only ever DL_DATA or DL_ACK
    size_t	 len;       	// the length of the msg field only
    int          checksum;  	// checksum of the whole frame
    int          seq;       	// only ever 0 or 1
    MSG          msg;
} FRAME;

#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)


/* STOP AND WAIT VARIABLES FOR HOSTS */
static  MSG       	*lastmsg;
static  size_t		lastlength		= 0;
static  CnetTimerID	lasttimer		= NULLTIMER;

static  int       	ackexpected		= 0;
static	int		nextframetosend		= 0;
static	int		frameexpected		= 0;
/* /STOP AND WAIT VARIABLES FOR HOSTS */

/* STOP AND WAIT VARIABLES FOR LEFT NEIGHBOUR*/
static  int     SW_buffer_full       = 0;
/* STOP AND WAIT VARIABLES FOR LEFT NEIGHBOUR */

/* STOP AND WAIT VARIABLES FOR RIGHT NEIGHBOUR*/
/* STOP AND WAIT VARIABLES FOR RIGHT NEIGHBOUR */

// This is used in the hosts. It builds and forwards the frame and also
// sets the timer we need to timeout packets if they are data packets.
// Routers don't use this to forward packets because they do not timeout packets
// they only forward them.
static void transmit_frame(MSG *msg, FRAMEKIND kind, size_t length, int seqno, int link)
{
    FRAME       f;
    // int		link = 1;

    f.kind      = kind;
    f.seq       = seqno;
    f.checksum  = 0;
    f.len       = length;

    switch (kind) {
    case DL_ACK :
        printf("ACK transmitted, seq=%d\n", seqno);
	break;

    case DL_DATA: {
	CnetTime	timeout;

        printf(" DATA transmitted, seq=%d\n", seqno);
        memcpy(&f.msg, msg, (int)length);

	timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
				linkinfo[link].propagationdelay;

        lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
	break;
      }
    }
    length      = FRAME_SIZE(f);
    f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
    CHECK(CNET_write_physical(link, &f, &length));
}

static EVENT_HANDLER(application_ready)
{
    CnetAddr destaddr;

    lastlength  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, lastmsg, &lastlength));
    CNET_disable_application(ALLNODES);

    printf("down from application, seq=%d\n", nextframetosend);
    transmit_frame(lastmsg, DL_DATA, lastlength, nextframetosend, 1);
    nextframetosend = 1-nextframetosend;
}


// Handles reading from physical layer when packet arrives.
static EVENT_HANDLER(physical_ready)
{
    FRAME        f;
    size_t	 len;
    int          link, checksum;

    len         = sizeof(FRAME);
    CHECK(CNET_read_physical(&link, &f, &len));

    // Ensure checksum is ok otherwise ignore the frame.
    checksum    = f.checksum;
    f.checksum  = 0;
    if(CNET_ccitt((unsigned char *)&f, (int)len) != checksum) {
        printf("\t\t\t\tBAD checksum - frame ignored\n");
        return;           // bad checksum, ignore frame
    }
    f.checksum  = CNET_ccitt((unsigned char *)&f, (int)len);

/* ROUTER STOP AND WAIT PROTOCOL */
    // If this node is a router forward the message to the next node.
    if (nodeinfo.nodetype == NT_ROUTER) {

        /* LEFT PROTOCOL */
        // Distinguish between left and right protocol
        // IF the frame is a data frame this is the 'left' protocol
        if (f.kind == DL_DATA) {
            // Forward message on to next node, ack sending node and wait for ACK
            // if buffer is not full otherwise do nothing.
            if (SW_buffer_full) {
                return;
            }

            // Make sure the frame sequence number is correct.
            // If so increment the next frame expected transmit the data
            // frame to the next router and send ack to the data sender.
            if (frameexpected == f.seq){
                // Increment the frame expected in the left protocol
                frameexpected = 1-frameexpected;

                // Send the ACK and forward the data frame.
                transmit_frame(NULL, DL_ACK, 0, f.seq, 1);
                transmit_frame(&f.msg, DL_DATA, f.len, f.seq, 2);

                // Store the data frame in the buffer until receiving ack from next node.
                SW_buffer_full = 1;

                memcpy(lastmsg, &f.msg, (int)f.len);
                memcpy(&lastlength, &f.len, sizeof((int)f.len));
                printf("Router has received a data frame and forwarded it. Also sent ack.\n");
            }
        /* /LEFT PROTOCOL */

        /* RIGHT PROTOCOL */
        // This protocol handles the Acks received. All it does is clear the buffer if the
        // correct ack seqno is received.
        } else {
            // Chceck whether ack is correct sequence number
            if (ackexpected == f.seq) {
                ackexpected = 1-ackexpected;
                SW_buffer_full = 0;
                CNET_stop_timer(lasttimer);
                printf("Router has received and ack and cleared its buffer.\n");
            } else {
                printf("Received a frame but Buffer SW_RIGHT_frameexpected != f.seq\n");
            }
        }
        /* /RIGHT PROTOCOL */

        return;
    }

/* /ROUTER STOP AND WAIT PROTOCOL */


/* HOST STOP AND WAIT PROTOCOL */
    switch (f.kind) {

    // If the packet is an ack and the node expected and ack
    // stop the timeout timer on the packet. Set ack expected to no
    // and enable application in all nodes?
    case DL_ACK :
        if(f.seq == ackexpected) {
            printf("\t\t\t\tACK received, seq=%d\n", f.seq);
            CNET_stop_timer(lasttimer);
            ackexpected = 1-ackexpected;
            CNET_enable_application(ALLNODES);
        }
	break;

    // If the packet is a data frame check to see if its the correct sequence number.
    // If it is write it to the application and reset the frame sequence number
    // expected. If its the incorrect sequence number ignore the frame.
    case DL_DATA :
        printf("\t\t\t\tDATA received, seq=%d, ", f.seq);
        if(f.seq == frameexpected) {
            printf("up to application\n");
            printf("Node number %d\n", nodeinfo.nodenumber);
            len = f.len;
            // Write the data packet to the application layer and send an ack.
            CHECK(CNET_write_application(&f.msg, &len));
            frameexpected = 1-frameexpected;
            transmit_frame(NULL, DL_ACK, 0, f.seq, 1);
        }
        else
            printf("ignored\n");

        // Why are we transmitting frame if the sequence number may have been wrong?
	break;
    }
/* HOST STOP AND WAIT PROTOCOL */
}

static EVENT_HANDLER(timeouts)
{
    printf("timeout, seq=%d\n", ackexpected);
    if (nodeinfo.nodetype == NT_ROUTER) {
        printf("Data packet timed out. Resending.");
    }

    // If this is a router send the data out on 2.
    // If this is a host send the data out on 1.
    if (nodeinfo.nodetype == NT_ROUTER) {
        transmit_frame(lastmsg, DL_DATA, lastlength, ackexpected, 2);
    } else {
        transmit_frame(lastmsg, DL_DATA, lastlength, ackexpected, 1);
    }
}

static EVENT_HANDLER(showstate)
{
    printf(
    "\n\tackexpected\t= %d\n\tnextframetosend\t= %d\n\tframeexpected\t= %d\n",
		    ackexpected, nextframetosend, frameexpected);
}

EVENT_HANDLER(reboot_node)
{
 //    if(nodeinfo.nodenumber > 1) {
	// fprintf(stderr,"This is not a 2-node network!\n");
	// exit(1);
 //    }

    lastmsg	= calloc(1, sizeof(MSG));

    // Prevent application ready from running in routers since they are only go betweens
    if (nodeinfo.nodetype == NT_HOST) {
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
    }

    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));

    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    if(nodeinfo.nodenumber == 0)
	CNET_enable_application(ALLNODES);
}
