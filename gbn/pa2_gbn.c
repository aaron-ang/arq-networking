#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

/* ******************************************************************
   ARQ NETWORK EMULATOR: VERSION 1.1  J.F.Kurose
   MODIFIED by Chong Wang on Oct.21,2005 for csa2,csa3 environments

   This code should be used for PA2, unidirectional data transfer protocols
   (from A to B)
   Network properties:
   - one way network delay averages five time units (longer if there
     are other messages in the channel for Pipelined ARQ), but can be larger
   - packets can be corrupted (either the header or the data portion)
     or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
     (although some can be lost).
**********************************************************************/

/* a "msg" is the data unit passed from layer 5 (teachers code) to layer  */
/* 4 (students' code).  It contains the data (characters) to be delivered */
/* to layer 5 via the students transport level protocol entities.         */
struct msg
{
  char data[20];
};

/* a packet is the data unit passed from layer 4 (students code) to layer */
/* 3 (teachers code).  Note the pre-defined packet structure, which all   */
/* students must follow. */
struct pkt
{
  int seqnum;
  int acknum;
  int checksum;
  char payload[20];
  int sack[5];
};

/*- Your Definitions
  ---------------------------------------------------------------------------*/

/* Please use the following values in your program */

#define A 0
#define B 1
#define FIRST_SEQNO 0

/*- Declarations ------------------------------------------------------------*/
void restart_rxmt_timer(void);
void tolayer3(int AorB, struct pkt packet);
void tolayer5(char datasent[20]);

void starttimer(int AorB, double increment);
void stoptimer(int AorB);

void Simulation_done(void);

/* WINDOW_SIZE, RXMT_TIMEOUT and TRACE are inputs to the program;
   Please set an appropriate value for LIMIT_SEQNO.
   You have to use these variables in your
   routines --------------------------------------------------------------*/

extern int WINDOW_SIZE;     // size of the window
extern int LIMIT_SEQNO;     // when sequence number reaches this value, it wraps around
extern double RXMT_TIMEOUT; // retransmission timeout
extern int TRACE;           // trace level, for your debug purpose
extern double time_now;     // simulation time, for your debug purpose

/********* YOU MAY ADD SOME ROUTINES HERE ********/

#define BUFSIZE 50

// A
struct Sender
{
  int window_start;
  int send_next;
  int buffer_next;
  struct pkt *packet_buffer[BUFSIZE];
  struct timespec *packet_timer[BUFSIZE];
  bool retransmitted[BUFSIZE];
} A_ent;

// B
struct Receiver
{
  int window_start;
  struct pkt ack_pkt;
} B_ent;

struct timespec stop;

// Statistics
int num_original_transmitted = 0;
int num_retransmissions = 0;
int num_delivered = 0;
int num_ack_sent = 0;
int num_ack_received = 0;
int num_corrupted = 0;
double rtt_sum = 0;
int rtt_count = 0;
double comm_time_sum = 0;
int comm_time_count = 0;

int get_checksum(struct pkt packet)
{
  int checksum = 0;
  checksum += packet.seqnum;
  checksum += packet.acknum;
  for (int i = 0; i < 20; i++)
  {
    checksum += packet.payload[i];
  }
  return checksum;
}

void send_window(void)
{
  if (A_ent.send_next == A_ent.buffer_next || A_ent.send_next == A_ent.window_start + WINDOW_SIZE)
    return;

  restart_rxmt_timer();

  while (A_ent.send_next < A_ent.buffer_next && A_ent.send_next < A_ent.window_start + WINDOW_SIZE)
  {
    struct pkt *packet = A_ent.packet_buffer[A_ent.send_next % BUFSIZE];
    struct timespec *packet_start = (struct timespec *)malloc(sizeof(struct timespec));
    A_ent.packet_timer[A_ent.send_next % BUFSIZE] = packet_start;
    clock_gettime(CLOCK_MONOTONIC_RAW, packet_start);
    printf("  send_window: send packet (seq=%d): %s\n",
           packet->seqnum, packet->payload);
    tolayer3(A, *packet);
    num_original_transmitted++;
    A_ent.send_next++;
  }
}

void send_ack(void)
{
  int acknum = B_ent.window_start % LIMIT_SEQNO;
  B_ent.ack_pkt.acknum = acknum;
  B_ent.ack_pkt.checksum = get_checksum(B_ent.ack_pkt);
  printf("  send_ack: send ACK (ack=%d)\n", acknum);
  tolayer3(B, B_ent.ack_pkt);
  num_ack_sent++;
}

void print_window(int AorB)
{
  if (AorB == A)
  {
    printf("  A_window:");
    for (int i = A_ent.window_start; i < A_ent.window_start + WINDOW_SIZE; i++)
    {
      struct pkt *packet = A_ent.packet_buffer[i % BUFSIZE];
      packet ? printf(" %d", packet->seqnum) : printf(" -");
    }
  }
  else
  {
    printf("  B_window: %d", B_ent.window_start % LIMIT_SEQNO);
    printf(" (SACK:");
    for (int i = 0; i < 5; i++)
    {
      int val = B_ent.ack_pkt.sack[i];
      val >= 0 ? printf(" %d", val) : printf(" -");
    }
    printf(")");
  }
  printf("\n");
}

bool insert_sack(struct pkt packet)
{
  int sack_start = B_ent.window_start + 1;
  for (int i = 0; i < 5; i++)
  {
    if ((sack_start + i) % LIMIT_SEQNO == packet.seqnum)
    {
      if (B_ent.ack_pkt.sack[i] >= 0)
      {
        printf("  insert_sack: duplicate SACK (seq=%d)\n", packet.seqnum);
        return false;
      }
      printf("  insert_sack: deliver packet and insert SACK (seq=%d)\n", packet.seqnum);
      tolayer5(packet.payload);
      num_delivered++;
      B_ent.ack_pkt.sack[i] = packet.seqnum;
      return true;
    }
  }
  return false;
}

int sack_offset(void)
{
  int i;
  for (i = 0; i < 5; i++)
  {
    if (B_ent.ack_pkt.sack[i] < 0)
    {
      break;
    }
  }
  return i;
}

void record_time_measurement(int i)
{
  struct timespec *packet_start = A_ent.packet_timer[i % BUFSIZE];
  if (!packet_start)
    return;

  double measurement_time = (stop.tv_sec - packet_start->tv_sec) * 1000 +
                            (stop.tv_nsec - packet_start->tv_nsec) / 1000000.0;
  free(packet_start);
  A_ent.packet_timer[i % BUFSIZE] = NULL;

  comm_time_sum += measurement_time;
  comm_time_count++;
  if (!A_ent.retransmitted[i % BUFSIZE])
  {
    rtt_sum += measurement_time;
    rtt_count++;
  }
  A_ent.retransmitted[i % BUFSIZE] = false;
}

/********* STUDENTS WRITE THE NEXT SEVEN ROUTINES *********/

/* called from layer 5, passed the data to be sent to other side */
void A_output(struct msg message)
{
  printf("  A_output: buffer packet (seq=%d): %s\n",
         A_ent.buffer_next % LIMIT_SEQNO, message.data);
  struct pkt *packet = A_ent.packet_buffer[A_ent.buffer_next % BUFSIZE];
  if (packet)
  {
    printf("  A_output: buffer full\n");
    Simulation_done();
    exit(1);
  }
  packet = (struct pkt *)malloc(sizeof(struct pkt));
  packet->seqnum = A_ent.buffer_next % LIMIT_SEQNO;
  memmove(packet->payload, message.data, 20);
  packet->checksum = get_checksum(*packet);
  A_ent.packet_buffer[A_ent.buffer_next % BUFSIZE] = packet;
  A_ent.buffer_next++;
  send_window();
}

/* called from layer 3, when a packet arrives for layer 4 */
void A_input(struct pkt ack_packet)
{
  print_window(A);
  num_ack_received++;

  if (ack_packet.checksum != get_checksum(ack_packet))
  {
    num_corrupted++;
    printf("  A_input: recv corrupted ACK\n");
    return;
  }

  clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
  printf("  A_input: recv ACK (ack=%d)\n", ack_packet.acknum);

  if (ack_packet.acknum == A_ent.window_start % LIMIT_SEQNO)
  {
    // process SACKs
    for (int i = 0, j = A_ent.window_start + 1; i < 5 && j < A_ent.send_next; i++, j++)
    {
      int sack = ack_packet.sack[i];
      struct pkt *packet = A_ent.packet_buffer[j % BUFSIZE];
      if (sack >= 0 && packet)
      {
        printf("  A_input: recv SACK (seq=%d)\n", sack);
        free(packet);
        A_ent.packet_buffer[j % BUFSIZE] = NULL;
        record_time_measurement(j);
      }
    }
    return;
  }

  // Move window forward
  int i = A_ent.window_start;
  for (; i < A_ent.send_next && i % LIMIT_SEQNO != ack_packet.acknum; i++)
  {
    struct pkt *packet = A_ent.packet_buffer[i % BUFSIZE];
    if (packet)
    {
      free(packet);
      A_ent.packet_buffer[i % BUFSIZE] = NULL;
      record_time_measurement(i);
    }
  }
  int diff = i - A_ent.window_start;
  if (diff > 0)
  {
    printf("  A_input: moved window by %d (window_start=%d, send_next=%d)\n",
           diff, i % LIMIT_SEQNO, A_ent.send_next % LIMIT_SEQNO);
    A_ent.window_start = i;
  }
  if (A_ent.window_start == A_ent.send_next) // Send any new packets waiting in the buffer
  {
    send_window();
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  if (A_ent.window_start == A_ent.send_next)
    return;
  starttimer(A, RXMT_TIMEOUT);
  for (int i = A_ent.window_start; i < A_ent.send_next; i++)
  {
    struct pkt *packet = A_ent.packet_buffer[i % BUFSIZE];
    if (packet)
    {
      printf("  A_timerinterrupt: Case3 -> retransmit unACKed packet (seq=%d): %s\n",
             packet->seqnum, packet->payload);
      tolayer3(A, *packet);
      A_ent.retransmitted[i % BUFSIZE] = true;
      num_retransmissions++;
    }
  }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  A_ent.window_start = FIRST_SEQNO;
  A_ent.send_next = FIRST_SEQNO;
  A_ent.buffer_next = FIRST_SEQNO;
}

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  if (packet.checksum != get_checksum(packet))
  {
    num_corrupted++;
    printf("  B_input: recv corrupted packet\n");
    return;
  }

  print_window(B);
  if (packet.seqnum != B_ent.window_start % LIMIT_SEQNO)
  {
    printf("  B_input: recv out-of-order packet (seq=%d): %s\n",
           packet.seqnum, packet.payload);
    if (!insert_sack(packet))
    {
      printf("  B_input: drop packet (seq=%d): %s\n",
             packet.seqnum, packet.payload);
    }
  }
  else
  {
    printf("  B_input: recv in-order packet (seq=%d): %s\n",
           packet.seqnum, packet.payload);
    tolayer5(packet.payload);
    num_delivered++;
    int offset = 1 + sack_offset();
    B_ent.window_start += offset;
    for (int i = 0; i < 5; i++) // shift SACK
    {
      B_ent.ack_pkt.sack[i] = i + offset < 5 ? B_ent.ack_pkt.sack[i + offset] : -1;
    }
  }

  send_ack();
}

/* the following rouytine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  B_ent.window_start = FIRST_SEQNO;
  B_ent.ack_pkt.seqnum = -1;
  memset(B_ent.ack_pkt.sack, -1, sizeof(B_ent.ack_pkt.sack));
}

void restart_rxmt_timer(void)
{
  stoptimer(A);
  starttimer(A, RXMT_TIMEOUT);
}

/* called at end of simulation to print final statistics */
void Simulation_done()
{
  double lost_ratio = (double)(num_retransmissions - num_corrupted) /
                      (num_original_transmitted + num_retransmissions + num_ack_sent);
  double corrupted_ratio = (double)num_corrupted /
                           (num_original_transmitted + num_retransmissions + num_ack_sent - (num_retransmissions - num_corrupted));
  /* TO PRINT THE STATISTICS, FILL IN THE DETAILS BY PUTTING VARIBALE NAMES. DO NOT CHANGE THE FORMAT OF PRINTED OUTPUT */
  printf("\n\n===============STATISTICS======================= \n\n");
  printf("Number of original packets transmitted by A: %d \n", num_original_transmitted);
  printf("Number of retransmissions by A: %d \n", num_retransmissions);
  printf("Number of data packets delivered to layer 5 at B: %d \n", num_delivered);
  printf("Number of ACK packets sent by B: %d \n", num_ack_sent);
  printf("Number of corrupted packets: %d \n", num_corrupted);
  printf("Ratio of lost packets: %.3f \n", lost_ratio);
  printf("Ratio of corrupted packets: %.3f \n", corrupted_ratio);
  printf("Average RTT (ms): %.3f \n", rtt_sum / rtt_count);
  printf("Average communication time (ms): %.3f \n", comm_time_sum / comm_time_count);
  printf("==================================================");

  /* PRINT YOUR OWN STATISTIC HERE TO CHECK THE CORRECTNESS OF YOUR PROGRAM */
  printf("\nEXTRA: \n");
  /* EXAMPLE GIVEN BELOW */
  printf("Number of ACK packets received by A: %d \n", num_ack_received);
  printf("Total RTT (ms): %.3f \n", rtt_sum);
  printf("Number of RTT measurements: %d \n", rtt_count);
  printf("Total communication time (ms): %.3f \n", comm_time_sum);
  printf("Number of communication time measurements: %d \n", comm_time_count);
}

/*****************************************************************
***************** NETWORK EMULATION CODE STARTS BELOW ***********
The code below emulates the layer 3 and below network environment:
  - emulates the tranmission and delivery (possibly with bit-level corruption
    and packet loss) of packets across the layer 3/4 interface
  - handles the starting/stopping of a timer, and generates timer
    interrupts (resulting in calling students timer handler).
  - generates message to be sent (passed from later 5 to 4)

THERE IS NOT REASON THAT ANY STUDENT SHOULD HAVE TO READ OR UNDERSTAND
THE CODE BELOW.  YOU SHOLD NOT TOUCH, OR REFERENCE (in your code) ANY
OF THE DATA STRUCTURES BELOW.  If you're interested in how I designed
the emulator, you're welcome to look at the code - but again, you should have
to, and you defeinitely should not have to modify
******************************************************************/

struct event
{
  double evtime;      /* event time */
  int evtype;         /* event type code */
  int eventity;       /* entity where event occurs */
  struct pkt *pktptr; /* ptr to packet (if any) assoc w/ this event */
  struct event *prev;
  struct event *next;
};
struct event *evlist = NULL; /* the event list */

/* Advance declarations. */
void init(void);
void generate_next_arrival(void);
void insertevent(struct event *p);

/* possible events: */
#define TIMER_INTERRUPT 0
#define FROM_LAYER5 1
#define FROM_LAYER3 2

#define OFF 0
#define ON 1

int TRACE = 0; /* for debugging purpose */
int fileoutput;
double time_now = 0.000;
int WINDOW_SIZE;
int LIMIT_SEQNO;
double RXMT_TIMEOUT;
double lossprob;    /* probability that a packet is dropped  */
double corruptprob; /* probability that one bit is packet is flipped */
double lambda;      /* arrival rate of messages from layer 5 */
int ntolayer3;      /* number sent into layer 3 */
int nlost;          /* number lost in media */
int ncorrupt;       /* number corrupted by media*/
int nsim = 0;
int nsimmax = 0;
unsigned int seed[5]; /* seed used in the pseudo-random generator */

int main(int argc, char **argv)
{
  struct event *eventptr;
  struct msg msg2give;
  struct pkt pkt2give;

  int i, j;

  init();
  A_init();
  B_init();

  while (1)
  {
    eventptr = evlist; /* get next event to simulate */
    if (eventptr == NULL)
      goto terminate;
    evlist = evlist->next; /* remove this event from event list */
    if (evlist != NULL)
      evlist->prev = NULL;
    if (TRACE >= 2)
    {
      printf("\nEVENT time: %f,", eventptr->evtime);
      printf("  type: %d", eventptr->evtype);
      if (eventptr->evtype == 0)
        printf(", timerinterrupt  ");
      else if (eventptr->evtype == 1)
        printf(", fromlayer5 ");
      else
        printf(", fromlayer3 ");
      printf(" entity: %d\n", eventptr->eventity);
    }
    time_now = eventptr->evtime; /* update time to next event time */
    if (eventptr->evtype == FROM_LAYER5)
    {
      generate_next_arrival(); /* set up future arrival */
                               /* fill in msg to give with string of same letter */
      j = nsim % 26;
      for (i = 0; i < 20; i++)
        msg2give.data[i] = 97 + j;
      msg2give.data[19] = '\n';
      nsim++;
      if (nsim == nsimmax + 1)
        break;
      A_output(msg2give);
    }
    else if (eventptr->evtype == FROM_LAYER3)
    {
      pkt2give.seqnum = eventptr->pktptr->seqnum;
      pkt2give.acknum = eventptr->pktptr->acknum;
      pkt2give.checksum = eventptr->pktptr->checksum;
      for (i = 0; i < 20; i++)
        pkt2give.payload[i] = eventptr->pktptr->payload[i];
      for (i = 0; i < 5; i++)
        pkt2give.sack[i] = eventptr->pktptr->sack[i];
      if (eventptr->eventity == A) /* deliver packet by calling */
        A_input(pkt2give);         /* appropriate entity */
      else
        B_input(pkt2give);
      free(eventptr->pktptr); /* free the memory for packet */
    }
    else if (eventptr->evtype == TIMER_INTERRUPT)
    {
      A_timerinterrupt();
    }
    else
    {
      printf("INTERNAL PANIC: unknown event type \n");
    }
    free(eventptr);
  }
terminate:
  Simulation_done(); /* allow students to output statistics */
  printf("Simulator terminated at time %.12f\n", time_now);
  return (0);
}

void init(void) /* initialize the simulator */
{
  int i = 0;
  printf("----- * Network Simulator Version 1.1 * ------ \n\n");
  printf("Enter number of messages to simulate: ");
  scanf("%d", &nsimmax);
  printf("Enter packet loss probability [enter 0.0 for no loss]:");
  scanf("%lf", &lossprob);
  printf("Enter packet corruption probability [0.0 for no corruption]:");
  scanf("%lf", &corruptprob);
  printf("Enter average time between messages from sender's layer5 [ > 0.0]:");
  scanf("%lf", &lambda);
  printf("Enter window size [>0]:");
  scanf("%d", &WINDOW_SIZE);
  LIMIT_SEQNO = WINDOW_SIZE * 2; // set appropriately; here assumes SR
  printf("Enter retransmission timeout [> 0.0]:");
  scanf("%lf", &RXMT_TIMEOUT);
  printf("Enter trace level:");
  scanf("%d", &TRACE);
  printf("Enter random seed: [>0]:");
  scanf("%d", &seed[0]);
  for (i = 1; i < 5; i++)
    seed[i] = seed[0] + i;
  fileoutput = open("OutputFile", O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fileoutput < 0)
    exit(1);
  ntolayer3 = 0;
  nlost = 0;
  ncorrupt = 0;
  time_now = 0.0;          /* initialize time to 0.0 */
  generate_next_arrival(); /* initialize event list */
}

/****************************************************************************/
/* mrand(): return a double in range [0,1].  The routine below is used to */
/* isolate all random number generation in one location.  We assume that the*/
/* system-supplied rand() function return an int in therange [0,mmm]        */
/*     modified by Chong Wang on Oct.21,2005                                */
/****************************************************************************/
int nextrand(int i)
{
  seed[i] = seed[i] * 1103515245 + 12345;
  return (unsigned int)(seed[i] / 65536) % 32768;
}

double mrand(int i)
{
  double mmm = 32767;    /* largest int  - MACHINE DEPENDENT!!!!!!!!   */
  double x;              /* individual students may need to change mmm */
  x = nextrand(i) / mmm; /* x should be uniform in [0,1] */
  if (TRACE == 0)
    printf("%.16f\n", x);
  return (x);
}

/********************* EVENT HANDLINE ROUTINES *******/
/*  The next set of routines handle the event list   */
/*****************************************************/
void generate_next_arrival(void)
{
  double x, log(), ceil();
  struct event *evptr;
  //   char *malloc(); commented out by matta 10/17/2013

  if (TRACE > 2)
    printf("          GENERATE NEXT ARRIVAL: creating new arrival\n");

  x = lambda * mrand(0) * 2; /* x is uniform on [0,2*lambda] */
                             /* having mean of lambda        */
  evptr = (struct event *)malloc(sizeof(struct event));
  evptr->evtime = time_now + x;
  evptr->evtype = FROM_LAYER5;
  evptr->eventity = A;
  insertevent(evptr);
}

void insertevent(struct event *p)
{
  struct event *q, *qold;

  if (TRACE > 2)
  {
    printf("            INSERTEVENT: time is %f\n", time_now);
    printf("            INSERTEVENT: future time will be %f\n", p->evtime);
  }
  q = evlist; /* q points to header of list in which p struct inserted */
  if (q == NULL)
  { /* list is empty */
    evlist = p;
    p->next = NULL;
    p->prev = NULL;
  }
  else
  {
    for (qold = q; q != NULL && p->evtime > q->evtime; q = q->next)
      qold = q;
    if (q == NULL)
    { /* end of list */
      qold->next = p;
      p->prev = qold;
      p->next = NULL;
    }
    else if (q == evlist)
    { /* front of list */
      p->next = evlist;
      p->prev = NULL;
      p->next->prev = p;
      evlist = p;
    }
    else
    { /* middle of list */
      p->next = q;
      p->prev = q->prev;
      q->prev->next = p;
      q->prev = p;
    }
  }
}

void printevlist(void)
{
  struct event *q;
  printf("--------------\nEvent List Follows:\n");
  for (q = evlist; q != NULL; q = q->next)
  {
    printf("Event time: %f, type: %d entity: %d\n", q->evtime, q->evtype, q->eventity);
  }
  printf("--------------\n");
}

/********************** Student-callable ROUTINES ***********************/

/* called by students routine to cancel a previously-started timer */
void stoptimer(int AorB) /* A or B is trying to stop timer */
{
  struct event *q /* ,*qold */;
  if (TRACE > 2)
    printf("          STOP TIMER: stopping timer at %f\n", time_now);
  /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
  for (q = evlist; q != NULL; q = q->next)
    if ((q->evtype == TIMER_INTERRUPT && q->eventity == AorB))
    {
      /* remove this event */
      if (q->next == NULL && q->prev == NULL)
        evlist = NULL;          /* remove first and only event on list */
      else if (q->next == NULL) /* end of list - there is one in front */
        q->prev->next = NULL;
      else if (q == evlist)
      { /* front of list - there must be event after */
        q->next->prev = NULL;
        evlist = q->next;
      }
      else
      { /* middle of list */
        q->next->prev = q->prev;
        q->prev->next = q->next;
      }
      free(q);
      return;
    }
  printf("Warning: unable to cancel your timer. It wasn't running.\n");
}

void starttimer(int AorB, double increment) /* A or B is trying to stop timer */
{

  struct event *q;
  struct event *evptr;
  // char *malloc(); commented out by matta 10/17/2013

  if (TRACE > 2)
    printf("          START TIMER: starting timer at %f\n", time_now);
  /* be nice: check to see if timer is already started, if so, then  warn */
  /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
  for (q = evlist; q != NULL; q = q->next)
    if ((q->evtype == TIMER_INTERRUPT && q->eventity == AorB))
    {
      printf("Warning: attempt to start a timer that is already started\n");
      return;
    }

  /* create future event for when timer goes off */
  evptr = (struct event *)malloc(sizeof(struct event));
  evptr->evtime = time_now + increment;
  evptr->evtype = TIMER_INTERRUPT;
  evptr->eventity = AorB;
  insertevent(evptr);
}

/************************** TOLAYER3 ***************/
void tolayer3(int AorB, struct pkt packet) /* A or B is trying to stop timer */
{
  struct pkt *mypktptr;
  struct event *evptr, *q;
  // char *malloc(); commented out by matta 10/17/2013
  double lastime, x;
  int i;

  ntolayer3++;

  /* simulate losses: */
  if (mrand(1) < lossprob)
  {
    nlost++;
    if (TRACE > 0)
      printf("          TOLAYER3: packet being lost\n");
    return;
  }

  /* make a copy of the packet student just gave me since he/she may decide */
  /* to do something with the packet after we return back to him/her */
  mypktptr = (struct pkt *)malloc(sizeof(struct pkt));
  mypktptr->seqnum = packet.seqnum;
  mypktptr->acknum = packet.acknum;
  mypktptr->checksum = packet.checksum;
  for (i = 0; i < 20; i++)
    mypktptr->payload[i] = packet.payload[i];
  for (i = 0; i < 5; i++)
    mypktptr->sack[i] = packet.sack[i];
  if (TRACE > 2)
  {
    printf("          TOLAYER3: seq: %d, ack %d, check: %d ", mypktptr->seqnum,
           mypktptr->acknum, mypktptr->checksum);
  }

  /* create future event for arrival of packet at the other side */
  evptr = (struct event *)malloc(sizeof(struct event));
  evptr->evtype = FROM_LAYER3;      /* packet will pop out from layer3 */
  evptr->eventity = (AorB + 1) % 2; /* event occurs at other entity */
  evptr->pktptr = mypktptr;         /* save ptr to my copy of packet */
                                    /* finally, compute the arrival time of packet at the other end.
                                       medium can not reorder, so make sure packet arrives between 1 and 10
                                       time units after the latest arrival time of packets
                                       currently in the medium on their way to the destination */
  lastime = time_now;
  /* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next) */
  for (q = evlist; q != NULL; q = q->next)
    if ((q->evtype == FROM_LAYER3 && q->eventity == evptr->eventity))
      lastime = q->evtime;
  evptr->evtime = lastime + 1 + 9 * mrand(2);

  /* simulate corruption: */
  /* modified by Chong Wang on Oct.21, 2005  */
  if (mrand(3) < corruptprob)
  {
    ncorrupt++;
    if ((x = mrand(4)) < 0.75)
      mypktptr->payload[0] = '?'; /* corrupt payload */
    else if (x < 0.875)
      mypktptr->seqnum = 999999;
    else
      mypktptr->acknum = 999999;
    if (TRACE > 0)
      printf("          TOLAYER3: packet being corrupted\n");
  }

  if (TRACE > 2)
    printf("          TOLAYER3: scheduling arrival on other side\n");
  insertevent(evptr);
}

void tolayer5(char datasent[20])
{
  write(fileoutput, datasent, 20);
}
