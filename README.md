# ETCP: External Tranmission Control Protocol

ETCP is a modern transport protocol designed as a replacement for TCP. ETCP provides users with a bytestream, sockets like interface desgined to operate on modern hardware. Unlike TCP, ETCP gives Transmission Control decisions over to the users. Users can decide how and when to acknowledge packets and if/when to send retransmits. This gives users more control in more complex environments where TCP's congestion controll mechanisms might become confused. 


