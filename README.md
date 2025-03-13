C++ is generally used for this project.

At a later point, we intend to implement
bundle functionality, which is similar to
regular packet functionality but transmitted
datagrams are saved at relays, to reduce 
retransmission and loss. Relay stations may 
be terrestrial satellites, terrestrial network
infrastructure, lunar satellites, space
stations etc.

We intend to build upon basic UDP
infrastructure, as UDP is by its nature
quite barebones. The only thing is guarantees is
best-effort service, which essentially
means nothing.

Initially, a network with capabilities of sending
at least four data points, is created. Packet
tracking is verified via print messages
and WireShark data. Port 12345 is generally used
because this port is not reserved for special
protocols or uses.

**USING THE PROGRAMME:**
The programme consists of a server.cpp file
and a main.cpp window. If communicating on
the same computer, the loopback address
(127.0.0.1) is used.
If communicating on different computers, 
the IP of the computer being communicated with
is used. A common port must be used by the
server and client.

The server.cpp file is first built and run
on the host computer. The client.cpp
file is then built on the client. Upon 
successful connection, periodic random
numbers are sent between the server 
and client, and by typing 'rock' into the
console, the client can receive a rock of 
one of four random types: Basalt,
regolith, anorthosite and breccia. These
rock types are commonly found on the moon,
and are taken from this Britannica article:
https://www.britannica.com/place/Moon/Lunar-rocks-and-soil

NOTE FOR GROUP: CREATE PYTHON OR OTHER SCRIPT
THAT COMPARES DATA IN WIRESHARK CSV AND
SERVER/CLIENT TO MATCH THEM.

TRACK STATS LIKE LATENCY, ADD CUSTOM DELAY
OPTION/VARIABLE, FIGURE OUT HOW TO
BUILD ON TOP OF UDP. 

ADD DATA SENT 
TIMESTAMPS FOR CLIENT + SERVER.
CHECK IF THEY CAN COMMUNICATE ON 
SEPARATE WI-FI NETWORKS. 

ADD OPTION TO IMPLEMENT INTERMEDIATE NETWORKS
AND "BUNDLING"

IF SOMEONE COULD MAKE A COOL GUI THAT WOULD
ALSO BE APPRECIATED. WE CAN ALL
CONTRIBUTE BUT WE SHOULD JUST LIST OUR GOALS
AND COMMENT CODE HEAVILY.