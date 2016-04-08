# ETCP: External Tranmission Control Protocol

ETCP is a transport protocol designed as a replacement for TCP in local area networks (eg. datacenters). Unlike TCP, ETCP takes the general approach that "bits are cheap, latency is expensive". It assumes a fast (10Gb/s+) network connection (ideally, 25-100Gb/s) where each bit costs less than 100ps to serialize, but, can take at least 2us (2,000,000ps!) for an RTT. ETCP is designed for situations where throughput concerns need to be intelligently ballanced against latency concerns. To do this, ETCP provides users with a TCP like sockets interface. However, unlike TCP, ETCP gives Transmission Control decisions directly over to the users. Users can decide how and when to acknowledge packets and if/when to send retransmits and how to respond to congestion signals (eg packet loss, round trip times, timeouts etc). This gives users more control in complex environments where TCP's congestion controll mechanisms may work against the users interests. For example
* If the user knows ahead of time that a connection will be point-to-point then the slow-start mechanims of TCP will only hinder the transfer.
* In situations where external congestion control/network scheduling is applied (e.g. QJump, FastPass, R2C2, TDMA etc) TCP's congestion contol is unaware of and can fight with the external mechanisms. 
* In situations with rapid remote state change (e.g. mechanical actuation, financial trading etc), it's possible that retransmiting a stale packet will cause more harm than good. 

ETCP makes the following assumptions: 
* Bits are cheap - no great lengths are taken to minimise header sizes. Where possible feilds are 64bit sized and 64bit aligned. 
* Latency is expensive - wherever possible latency is minimised. 
* Bandwidth is important - where bandwidth and latency needs to be ballanced, these decisions are given over to users
* MTUs are large. ETCP assumes at least an Ethernet transport with an MTU of ~1500B. LAN networks typically support "jumbo frames" of up to 9kB. 
* Measurement is king - ETCP assumes modern network cards with hardware timestamps for at least RX. This makes RTT estimates far more accurate. 
* Userspace networking - ETCP provides callbacks to the user for application invovlent in network decisions. Doing this at mimum latency cost requires usersapce access to the protocol state. 


Todo / Somewhat broken assumptions:
* Address enlargement to 128bit: Addresses are currently assumed to be 64bit, 64bit port. Since it currently runs directly over ethernet which has 48bit addresses, this is sufficient, however, if it were to run over IPv6, 128bit addresses would be necessary. This is a simple change. 
* Sequence number randomization: Currently the starting sequence number is 0 and the sequence numbers represent the absolute number of packets sent. 64bits means we can 1 packet per nano-second for the next 400 years. (Should be enough...) TCP uses a random inital sequence numbers on both ends and increments it by the number of bytes sent. This gives some protection from zombie packets However, this process requires a 3-way handshake to initiate. "The principle reason for the three-way handshake is to prevent old duplicate connection initiations from causing confusion."(RFC793). ETCP has no handshake phase because it tries to limit latency. A half-way point might be for the client end to pick a random seqence number, and use that number as a seed to a pseudo-random number generator, for the return sequence number. This would obviate the need for the 3-way handshake, but give some of the benefits that it offers. 
