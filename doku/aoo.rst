=========================================
AOO - towards message based audio systems
=========================================

The deployment of distributed audio systems in the context of computermusic 
and audio installation is explored within this library, to implement the vision of 
static streaming audio networks to flexible dynamic audio networks. Audiodata is send on demand only. Sharing sources and sinks allows us to create arbitrary audio networks.

This idea of message based audio systems, which has been investigated within several projects from  playing Ambisonics spatial audio systems, streaming over Large Area Networks (LAN) and playing within a computermusic ensembles.

In a first implementation Open Sound Control (OSC) is used as the content 
format proposing a definition of "Audio over OSC" - AoO.

The paradigm, audio has to be synchronous data streams, has led to an audio infrastructure, which not always fit the needs. Audio messages, especially in computermusic can also be short messages like notes, and time bounded content. So each audio message is always a pair with a timestamp, the synchronization of distributed audio sources can be accomplished within the accuracy of network time protocols.

.. figure:: ./figures/aoo_vision.pdf
   :alt: first idea of a message audio system with sources ``S_n`` and sinks :math: ``D_n``
   :width: 80%

   first idea of a message audio system with sources :math:`S_n` and sinks :math:`D_n`


Introduction
============

The first idea of a message based audio system came up with the requirement of playing a multi-speaker environment of distributed networked embedded devices from several computers, avoiding a central mixing desk.

Another demand for a message based audio network came up during the development of a flexible audio network within the ICE-ensemble [ICE]_. A variable number of computermusic musicians sending time bounded audio material with their computers to other participants (for monitoring or collecting audio material), would have caused a complex audio-matrix setup of quasi-permanent network connections with all the negotiations and initializations for these streams. Not only because of the limited rehearsal time, this seems to be both too error prone and an overkill in terms of network load.

The structure of a functional audio-network for ICE, especially during improvising sessions, cannot always be foreseen and is therefore hard to implement as a static network. It is therefore important to be able to easily change the audio network during performance, as musicians come and leave (and reboot). On the other hand, the need for low latency, responsiveness and sufficient audio quality has to be respected even during the dynamic change of network connections. No strict requirements on sample-rates, sample-accurate synchronization and the use of unique audio formats should be made in such situations. It should be possible to freely add or remove audio related devices to/from the system without having to go through complicated setup of audio streams and without having to negotiate meta data between the participants. This should simplify the implementation of the particular nodes.

Of course, special care has to be taken when playing together in an ensemble. Factors like network overload, especially peaks, can lead to bad sound and feedbacks. On the other hand, we also find such situations when playing together in the analog world. In any case, the limits have to be explored during rehearsals.

Setting up continuous streams where audio data, including silence, is send continuously to all possible destinations is an overhead, that can easily touch the limits of available network bandwidth. But also can cause wasteful/costly implementations. If we can send audio from different sources to sinks (like speaker systems) only on demand, simplifies the setup. Also, reducing the needs for negotiation for establishing connections simplifies this task, and therefore stabilizes the setup.

The use of messages for the delivery of audio-signals in a network seems to contradict the usual implementation of real-time audio-processing implementations in digital audio workstations, where mostly continuous synchronized audio streams are used. If these audio messages are send repeatedly in such a way that they can be combined together in time, they can been seen as limited audio data streams and supersede continuous audio streams.

Also audio streamed from different sources should be added time synchronous, which means even if the have different transportation times, latencies they should be added at the sinks with their exact source time. This is essential to preserve time information and the jitter should be mostly eliminated to get exact timing between sources.

Summing up these demands, the overall vision is to implement a distributed audio network, where a variable amount of nodes act as sound sources and sound sinks (sinks). It should be possible to send audio messages from any source to any sink, from multiple sources simultaneously to a single sink, respectively broadcasting audio messages from one source to multiple sinks. Accordingly, the cross-linking between the audio components is arbitrary.

There should not be a “Before you stream audio, you first have to negotiate and connect with ...”, Instead, any participant should be able to just send their audio data to others when needed. The receivers should be able to decide how to handle the audio, depending if they can or want to use them.

Following features can be outlined:

- audio signal intercommunication between distributed audio systems

- arbitrary ad-hoc connections

- various audio formats and samplerates

- audio-data on demand only

- time synchronous adding of sources

The most common way of communication within local networks is Ethernet. Therefore “Audio over Ethernet“ has become a widely used technique. However, there is roughly only a single approach: Stream based audio transmission, representing the data as a continuous sequence. For audio messages as on-demand packet based streams [#2]_ we found no usable implementation (2009). This lead to the design and implementation of a new audio transmission protocol for the demands shown before. As a first approach, an implementation in user space (on the application layer) without the need of special OS-drivers was intended. This can also be seen as the idea of “dynamic audio networks”.

Audio over OSC
==============

Looking for a modern, commonly used transmission format for messaging systems within the computermusic domain, we found “Open Sound Control” (OSC) [OSC]_. With its flexible address pattern in URL-style and its implementation of high resolution time tags, OSC provides everything needed as a communication format [BPOSC]_. OSC specifications points out that it does not require specific underlying transport protocol, but often uses Ethernet network. In our case this would be UDP in a first implementation but is not limited to these. TCP/IP as transport protocol can also be used, but would make some features obsolete and some more complicated, like the requirement for negotiations to initialize connections. Wolfgang Jäger implemented “Audio over OSC” (AoO) within a first project at the IEM [AOO]_. This was used in tests and”AUON“ (all under one net), a concert installation for network art [#3]_

the AoO-protocol
----------------

The definition of AoO protocol was made with simplicity in mind,
targeting also small devices like microcontrollers:

message to notify sinks about format changes:
 ``/AoO/<sink>/format ,iiiisb <src> <salt> <nchannels> <samplerate> <blocksize> <codec> <options>``

message to deliver audio data, large blocks are split across several frames:
 ``/AoO/<sink>/data ,iiidiiiib <isrc> <salt> <seq> <samplerate> <channel_onset> <totalsize> <nframes> <frame> <data>``

message from sink to source to request the format (e.g. the salt has changed):
 ``/AoO/<src>/request ,i <sink>``

message from sink to source to request dropped packets; the arguments are pairs of sequence + frame (-1 = whole block):
 ``/AoO/<src>/resend ,iib <sink> <salt> [<seq> <frame>]*``

ping message from sink to source (usually sent once per second):
 ``/AoO/<src>/ping ,i sink``

``src``
   Identification number of the source

``sink``
   Identification number for the sink

``salt`` 
   Unique random number
    
``seq`` 
   sequence of sequent data blocks

``samplerate`` 
   Different sampling rates of sources are possible, which will be re-sampled in the sink. The samplerate in the format is the formal one as integer, the samplerate in the data, the measured == corrected one and is therefore double precision.
   
``nchannels``
    number of channels to send

``channel_onset`` 
    first channel number in sink to write ``nchannels``

``blocksize``
    number of samples in a data block

``totalsize`` 
    total size of package
    
``nframes`` 
    number of frames to send

``frame`` 
    starting frame in block

``codec``
    which codec is used
    
``options``
    options for codec
    
``data``
    data content like defined above

Unlike Version~1, messages are not bundled, and meta-information is split in a format and a data message to reduce size. No ``#bundle`` means no ``timestamp``.

Since timestamping OSC of messages is done on send time, it does not help on synchronisation since the message can be send somewhere in the range of the buffer time of the audio application. A new strategy was chosen see Timing section.

Data types preferred are uncompressed packets with data types defined by OSC, like 32-Bit float. However, it’s also possible to use blobs with an arbitrary bit-length audio data. This can become handy if bandwidth matters. Sources must be aware, which formats can be handled by the sinks. Using codecs the codec defines the data. At the  moment only opus is use, since it also supports low latency.

To provide low latency, time-bounded audio transmissions is sliced into shorter messages and send individually to be reconstructed at the receiver.

There should always be at least one format message before sending data messages to a specific sink.

For the addressing scheme the structure of the resources in network is used as the base. Each device in the network with an unique network-address (IP-number and Port number) can have one or more sinks. Each of these sinks can have one or more channels. There can be an arbitrary amount of sinks, and each sink could have an arbitrary amount of channels.

.. Like described in ”Best Practices for Open Sound Control“ [BPOSC]_, REST (Representational State Transfer) style is used. With its stateless representation each message is a singleton containing all information needed.

In OSC, there is a type of query operators called address pattern matching. These can be used to address multiple channels or sinks in one message. Since pattern matching can be computational intensive, we propose only to use the ”*“ wild-char for addressing all channels of a sink or all sinks of a device.

Integer was chosen in favor for processors without hardware floating point support. Channel specific data information like the id number of the message stream, the sequence number in the channel message allow more easily to detect lost packages. The resolution of a sample and an individual resampling factor is contained in the channel messages, where the resampling factor enables channels to differ from the samplerate specified in the format message, allowing lower rates for sub channels, control streams or higher rates for specific other needs.




For re-arranging the audio packages there is a need to do some sort of labeling of the messages, since it is not clear if they are intended to overlap or are different material. This is handled via the “identification number” (id) and salt. Identical identification numbers means to recognize the material as one material and they can be cross-faded. So these numbers has to has to unique at least at the sink. Salt means different Audio Messages even on the same id.

.. The first audio packet has to be faded in and the last faded out. A sequence of audio messages must be concatenated. At least one message has to be buffered to know if a next one arrives. If messages are in overlapping mode, they always have to be cross-faded.


addressing problems
~~~~~~~~~~~~~~~~~~~

Like described above, to deliver audio messages to a sink, additionally to the sink number and channel number, the address of the device has to be known. A decision was made, that the address is not part of the message, since the sender has to know about the sink on the receiver and the network system has to handle the addressing. 

Like stated in in the vision, we do want negotiations and requests, but in situations where IPs are unknown, we needed a mechanism to grasp it. One implementation was announcement message broadcasted by each sink, with the address and a human readable meaningful name. Even more polite we implemented them as invitation messages, which also states: ”ready to receive“.

A second problem arose, since broadcasting to all sinks with the same number, the destination information is not contained in the audio message, we cannot use broadcast to reduce network load and address specific destinations. For this the sink has to know about the sources it will accept. Anyway this worked fine, but made some additional efforts in communication before.

One other problem is if drains or sinks are behind a firewall. So if A is behind the firewall, B cannot send data to A directly. So a receiver can use the back-channel of the receiver, which normally is provided using TCP/IP protocol but not UDP. But since a normal "NATing" firewall stores session data, there is a chance that it can work when the sink uses the known sources. This has to be explored further.

mixing modes
~~~~~~~~~~~~

In the first implementation we used two different modes: Mode 1
provides the possibility of summation of the received audio signals and
Mode 2 should perform an arithmetic averaging of parallel signals. The
reason for this is that summing audio signals with maximum amplitudes
each causes distortion. Using Mode 2 this cannot happen.

In the Version-2 of AoO only Mode 1 is implemented, since samples are added within a floating point domain and the audio application can take care to reduce the volume as needed.

.. _subsec:timing:

timing and sample-rates
-----------------------

Timing is critical in audio-systems, not only for synchronizing audio, but also to prevent jitter noise. Timestamps of the packets are represented by a 64 bit fixed point number, as specified by OSC, to a precision of about 230 picoseconds. This conforms to the representation used by the Network Time Protocol NTP [RFC5905]_.

Also another timeprotocoll can be used like the Precision Time Protocol PTP, since this is handled by the system, we only access exact timer information.

Using fixed buffering mode, the buffer size has to be chosen large enough to prevent dropouts. In the automatic buffer control mode, the sink should use the shortest possible size for buffering. If packets arrive too late, buffering should be dynamically extended and then slowly reduced. This has to be handled by the audio application. Number of dropouts, ping times and a method for resend is provided to be used for this purpose.

Since audio packets can arrive with different sample-rates, re-sampling is executed before the audio data is added to the internal sound stream synchronized with the local audio environment. This provides the opportunity to synchronize audio content respecting the timing differences and time drifts between sources and sinks. This strategy of resampling is shown in a figure `re-sampling`:

.. _fig-aoo_resampling:
.. figure:: ./figures/aoo_resampling.pdf
   :alt: re-sampling rate :math:`R_n` between source :math:`S` and sink :math:`D` is not constant
   :width: 80.0%

   re-sampling rate :math:`R_n` between source :math:`S` and sink :math:`D` is not constant



Looking at synchronization in digital audio system, mostly a common master-clock is used for all devices. Since each device has its own audio environment, which may not support external synchronization sources, the time :math:`T_Sn` of the local audio environment is used to calculate the corrected samplerate for outgoing audio messages.

Using the incoming corrected samplerate from the remote source, we can compare
them with the local time :math:`t_Dn` and correct the re-sampling factor
:math:`R_n` dynamically for each message. The change of the correction
should be small if averaged over a longer time, but can be bad for first
audio messages received. Therefore a DLL filter is used, like described in the paper "Using a DLL to filter time" by Fons Adriaensen [FA05]_ . 

Since the local time source of a device can differ from the timing of the audio environment, each device needs a correction factor between this time source and the audio hardware time including the time master device. This factor has to be communicated between the devices, so the re-sampling correction factor can be calculated before the first audio message is sent, guaranteeing a quasi sample-synchronous network-wide system starting with the first message send.

.. .. centered::
.. _fig-aoo_overlapped:

.. figure:: ./figures/aoo_overlapping.pdf
   :alt: audio messages are arranged as single, combined or overlapped
   :width: 80%

   audio messages are arranged as single, combined or overlapped using different salts.

Implementation
--------------

As a first proof of concept, AoO was implemented within user space using Pure Data. [MILLER:96]_  This implementation has shown various problems to be solved in future. Using the network library iemnet additional ”externals“ have been written in C to extend the OSC-Protocol, split continuous audio signals into packets and mix OSC audio messages in sinks.  

In the new Version-2 the network infrastructure has been implemented within the AoO library to overcome these problems and use new concepts for threading, to avoid blocking the main task.

As a first test environment, a number of different open-source audio hardware implementations, using Debian Linux OS-System, has been used. The new Version was implemented for most OS-System as Pd-Externals in a first place.

message based Ambisonics spatial audio systems
==============================================

.. .. centered::

.. _fig-aoo_embedded:

.. figure:: ./figures/aoo_ave.pdf
   :alt: AoO with embedded devices for spatial audio system
   :width: 80%

   AoO with embedded devices for spatial audio system


As a first goal, the geodesic sound-dome in Pischelsdorf (with a diameter of 20 m and a height of about 10 m) as an environmental landscape sculpture in Pischelsdorf should transmute into 3D a sound-sphere. Therefore as special hardware and software, a low power solar power driven multichannel Ambisonics system was developed and installed prototypically. This should result in a low cost implementation of multichannel audio system Up to 48 speakers should be mounted in a hemisphere, forming an Ambisonics sound system. Using 6 nodes, each with 8 speakers, special embedded controllers are used to render the audio in the system

(figure `fig-aoo_embedded`_ ).

.. .. centered::

.. _fig-aoo_domespeaker:

.. figure:: ./images/dome_node_small.jpg
   :alt: One node with one speaker in the dome
   :width: 80%

   One node with one speaker in the dome


Each node is a small embedded computer equipped with an 8-channel sound-card, including amplifiers and speakers. Each speaker can been calibrated and fed individually. However, since each unit is aware of its speaker positions, it can also render the audio with an internal Ambisonics encoder/decoder combination.

So instead of sending 48 channels of audio to spatialize one or more sources, the sources can be broadcast combined with OSC-spatialization data and the sinks render them independently. Another possibility is to broadcast an encoded Ambisonics-encoded multichannel signal, where the devices decode the Ambisonics signal for their subset of speakers. The Sound Environment can be sent from one master controller or any other connected computer.

The first implementation of the nodes has been done with special micro-controller boards escher2 which drive the custom designed DA-Amp boards. Since these devices have very limited memory (max. 16 samples of 64 channels), standard Linux audio system cannot provide the packets small and fast enough for a stable performance without special efforts, like own driver in kernel space for the packet delivery. Therefore a major problem has been the synchronization and the reliability of the transmission, but providing latency.


.. .. centered::
.. _fig-aoo_dome:

.. figure:: ./images/dome_cut.jpg
   :alt: sounddome as hemisphere, 20 m diameter in cornfield
   :width: 80%

   sounddome as hemisphere, 20 m diameter in cornfield


The main advantage, besides the low cost and autonomous system, is that one or more sound technicians or computer musicians can enter the dome, plug into the network with their portable devices and play the sound dome either addressing speakers individually, with audio material spatializing live with additional OSC messages or a generated or prerecorded Ambisonics audio material.

Playing together
----------------

.. .. centered::

.. _fig-ice_playing:

.. figure:: ./images/ice_porgy_and_bess_small.jpg
   :alt: first concert of IEM computermusic ensemble ICE playing over a HUB
   :width: 80%

   first concert of IEM computermusic ensemble ICE playing over a HUB


When specifying an audio-network for playing togehter within an ensemble, a focus was set on the collaborating efforts to be done to gain the unity of the individuals.

So, like a musicians with acoustic instrument, joining a band with Linux audio-computer implies a need for a place where the musician has a ”virtual sound space“ they can join. So they provide sound sources and need to plugin audio channels on a virtual mixing desk. With AoO the participant just needs to connect to the network, wireless or wired, choosing the sinks to play to and send phrases of audio with AoO when needed.

For the ICE ensemble Ambisonics as an virtual audio environment was chosen, which can be rendered to different concert halls. Within the Ambisonics each musician can always use the same playing parameters for spatializing her or his musical contribution. So the imagination of the musician is ”playing in a virtual 3D environment“, sending their audio signals together with 3D-spatial data to a distributed mixing system which is rendering it on the speakers.

Additional there is an audio communication between the musicians, where each musicians can hear into the signal produced by the other, if there is one or on special offered sinks send audio intervention to the others for e.g. monitoring purposes. The musicians can do their own monitor mix, depending on the piece and space where the play.

Using a message audio system, each musicians only sends sound data if playing, like audio bursts just notes, or just sending their audio-data to another musicians, who will process this further and so on. There should be no border on the imagination of these situations, (as long it can be grasped by the participants).

.. .. centered::

.. _fig-aoo_ice:

.. figure:: ./figures/aoo_ice.pdf
   :alt: ICE using AoO as space for playing together and on a PA system
   :width: 80%

   ICE using AoO as space for playing together and on a PA system

.. 
.. state of the work
.. =================
.. 
.. The AoO has been implemented for proof of concept and special
.. applications in a first draft version. The next version should fixate
.. the protocol, after having discussed it in public, in a way that makes
.. it compatible with future protocol upgrades.
.. 
.. The usage of AoO in an ensemble has been explored in a workshop with
.. students at the IEM, but the implemented software was not stable enough
.. on the different platforms used for stage performance. This was
.. especially true, when we tried to reach the short latencies needed for
.. concerts. Some more programming efforts has to be done, to guarantee
.. better timing using different computer types, within different
.. Linux-implementations and setups.
.. 
.. Running AoO on embedded Linux devices has shown to be successful, if the
.. devices are tweaked for real-time audio usage. The development on the micro-controller board has been abandoned in favor of
.. the new generation of small low power embedded devices with arm
.. processors. A first version of implementation (V1.0) of AoO is scheduled
.. for April 2014 for a public installation in the sound-dome, where the
.. Ambisonics audio-system should be finalized for permanent performance
.. and open access. More documentation and source code should be released
.. and open-hardware as AoO-audio devices should be available.
.. 
.. Special focus is done on using embedded devices with AoO as networked
.. multichannel audio hardware interfaces for low cost solutions adding
.. audio processing for calibration filters, beam-forming,…for
.. speaker-systems optional powered over Ethernet.
.. 
.. Conclusions
.. ===========
.. 
.. Starting as a vision, these experiments and implementations have shown,
.. that message based audio systems can enhance the collaboration in
.. ensembles, playing open audio systems. Also network art projects using
.. the Internet can use AoO to contribute to sound installation from
.. outside, just knowing the IP and ports to use.
.. 
.. The implementation is far from being complete, and more restrictions
.. will be included in order to simplify the system. Synchronization and
.. re-sampling is not perfect, but usable for most cases and it has been
.. shown, that audio message systems can work reliable in different
.. situations.
.. 
.. Audio message systems can also be implemented in other formats than OSC
.. and lower layers of the Linux OS, like jack-plugins or ALSA-modules as
.. converters between message based audio system and synchronous data flow
.. models.
.. 
.. For really low latency (below 1 ms) using AoO as audio over Ethernet
.. system, kernel-drivers must be developed and with time-slotted Ethernet
.. transmissions, systems with latencies down to 8 us on transmission time
.. can be implemented using hard RT-systems.
.. 
.. Networking
.. ==========
.. 
.. (fragments  fro IOhannes)
.. 
.. setup is like::
.. 
..     client A
..         will use listening port 10001
..         private IP: 192.168.1.100
..         public IP: 192.0.2.0.72
..     client B
..         will use listening port 10002
..         private IP: 192.168.7.22
..         public IP: 198.51.100.190
.. 
.. both clients need to know the public IP (and listening port) of the peer beforehand. 
.. 
.. initiate session::
.. 
..     clientA: initiate connection [connect 198.51.100.190 10002 10001( -> [netsend -u]
..     clientA: send some data
..         the data won't arrive on clientB yet
..         but the NATting routerA sets up the forwarding rules
..     clientB: initiate connection [connect 192.0.2.0.72 10001 10002( -> [netsend -u]
..     clientB: send some data
..         data should arrive at clientA (2nd outlet of [netsend])
..     clientA: send more data
..         data should arrive at clientB (2nd outlet of [netsend])
.. 
.. 
.. A hole-punching server setup::
.. 
..     serverX
..         reachable via a public IP:port
..     clientA, clientB
..         live in (separate) private (NATted) networks
..         don't know the public IPs
.. 
.. network connection flow::
.. 
..     1 clientA sends <channel-token> <clientA-name> <portA> to serverX
..         : some string known to all peers (e.g. "covid19")
..         - <clientA-name>: some string identifying clientA
..         - <portA>: the port where clientA listens for incoming payload data
..     2 serverX notes the public IP of clientA and remembers it along with the data-tuple.
..     3 clientB sends <channel-token> <clientB-name> <portB> to serverX
..     4 serverX notes the public IP of clientB and remembers it along with the data-tuple
..     5 serverX sends the public IP:port of clientB to clientA
..     6 serverX sends the public IP:port of clientA to clientB
..         in practice serverX might just "broadcast" the entire stored information (public IP, port, name) of all clients with the same channel-token to all clients with that same channel-token; clients will filter out their own public IP based on the client-name
..     7 clientA opens a UDP-connection to the public IP:port of clientB
..     8 clientB opens a UDP-connection to the public IP:port of clientA
..     9 tada
.. 
.. Acknowledgements
.. ================
.. 
.. Thanks to …my colleagues on the IEM supporting me with their help,
.. especially Wolfgang Jäger for a first implementation as a
.. sound-engineering project. Also for helping set up the ”Klangdom“
.. especially to Marian Weger, Matthias Kronlachner and the cultu ral
.. initiative K.U.L.M. in Pischelsdorf and the members of the ICE Ensemble
.. helping to experiment and many others. Thanks also for corrections of
.. this paper and useful hints, to enhance the understanding.
.. 
.. 
.. About Document
.. --------------
.. :authors: Winfried Ritsch, Christof Ressi
.. :date: march 2014 - february 2020
.. :version: 1.0-a1

.. [ICE] IEM (Institute of Electronic Music and Acoustics) Computermusic Ensemble

.. [OSC] "Matt Wright", http://opensoundcontrol.org/spec-1\_0 , [Online; accessed 1-Feb-2014], "The open sound control 1.0 specification.", 2002

.. [BPOSC] Andrew Schmeder and Adrian Freed and David Wessel, "Best Practices for Open Sound Control", "Linux Audio Conference", 01/05/2010, Utrecht, NL

.. [AOO] Wolgang Jaeger and Winfried Ritsch, "AOO", https://iem.kug.ac.at/en/projects/workspace/2009/audio-over-internet-using-osc.html , [Online; accessed 12-Dez-2011], Graz, 2009

.. [RFC5905] "D. Mills and J. Martin and J. Burbank and W. Kasch", 
        "RFC 5905 (Proposed Standard)", 
        "Network Time Protocol Version 4: Protocol and Algorithms Specification" , published by "Internet Engineering Task Force" IETF, "Request for Comments", number 5905,
	http://www.ietf.org/rfc/rfc5905.txt ,
	june 2010

.. [FA05] Fons Adriaensen, "Using a DLL to filter time", 2005,
        https://kokkinizita.linuxaudio.org/papers/usingdll.pdf

.. [#2] not to be mistaken with ”streaming on demand” or UDP packets
   

.. [#3] performed 17.1.2010 in Medienkunstlabor Kunsthaus Graz see
 http://medienkunstlabor.at/projects/blender/ArtsBirthday17012010/index.html
