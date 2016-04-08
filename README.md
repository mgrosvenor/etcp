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
*


