# Automatic Repeat Request (ARQ)

This project implements two variants of **ARQ**, a reliable network transport protocol.

The full report can be found [here](CS455_PA2.pdf).

ARQ "is an **error-control** method for data transmission that uses **acknowledgments** (messages sent by the receiver indicating that it has correctly received a message)
and **timeouts** (specified periods allowed to elapse before an acknowledgment is to be received)
to achieve **reliable data transmission** over an unreliable communication channel." [Wikipedia](https://en.wikipedia.org/wiki/Automatic_repeat_request)

Data will be transmitted in 20-byte chunks.

The network packet contains the following attributes:
```C
struct pkt {
  int seqnum;
  int acknum;
  int checksum;
  char payload[20];
};
```

## Selective Repeat with cumulative ACKs

The receiver will buffer out-of-order packets, and send cumulative ACKs.

The sender will retransmit only the next missing (unACK’d) packet on a timeout or a duplicate ACK.

## Go-Back-N with selective acknowlegdement (SACK)

The sender will behave like a GBN sender but retransmit all outstanding unACK'd packets that have not been selectively ACK’ed, not just the next missing data packet.
This is similar to [TCP SACK](https://wiki.geant.org/display/public/EK/SelectiveAcknowledgements).

The SACK option has a limit of 5, so an ACK packet can selectively acknowledge at most 5 packets.

