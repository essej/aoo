Audio over OSC based audio streaming
====================================

"Audio over OSC" aka *AoO* is aimed to be a message based audio system using 
Open Sound Control OSC_ as a syntax format.

history
-------

The vision of *AoO* has been done in 2009, a first implementation a library with externals for PureData (Pd) has been done 2010, but major issues with needed networking objects, like netsend, netreceive made this version unpracticable.
More on this version of message based audio system was published at LAC 2014 [LAC14]

.. [AOO-LAC14] see docu/lac2014_aoo.pdf

A new Version has been started again in 2020, targeting a network streaming Project for Kunsthaus Graz for Bill Fontana using a independent wireless network infrastructure Funkfeuer in Graz [0xFF].

.. _OSC: http://opensoundcontrol.org/

.. _Pd: http://puredata.info/

.. _0xFF: http://graz.funkfeuer.at/

Also a seminar on the IEM to scratch, rehearse and play an internet concert using this tools, for further enhancement and proof of concept, is in progress (April 2020)

Based on the AOO idea of Winfried Ritsch with a first draft of realisation on embedded devices, the new version going to be V1.0 and more, was mainly written by Christof Ressi, as a complete rewrite of the version from Wolfgang Jäger in C in 2010.

features
--------

* each endpoint can have multiple sources/sinks (each with their own ID)
* AoO sources can send audio to any sink at any time; 
* AoO sinks can listen and add signals to several sources at the same time
* AoO sinks and sources can operate at different blocksizes and samplerates
* AoO sources can dynamically change the channel onset
* AoO is internet time based, with means all signal from the same timestamp are added correctly at the same point in the receiver.
* timing differences (e.g. because of clock drifts) are adjusted via a time DLL filter + dynamic resampling
* the stream format can be set per audio message differently
* plugin API to register codecs; currently only PCM (uncompressed) and Opus (compressed) are implemented
* aoo_source and aoo_sink C++ classes have a lock-free ringbuffer, so that audio processing and network IO can run on different threads.
* aoo_sink buffer helps to deal with network jitter, packet reordering
  and packet loss at the cost of latency. The size can be adjusted dynamically.
* aoo_sink can ask the source(s) to resend dropped packets, the settings are free adjustable.
* settable UDP packet size for audio data (to optimize for local networks or the internet)
* sinks automatically send pings to all its sources (at a configurable rate).
  For example, sources might want to stop sending if they don't receive a ping in a certain time period.


Installation
------------

from source
...........

Get the source:

over ssh::

   git clone git@git.iem.at:cm/aoo.git

or https::

   git clone https://git.iem.at/pd/aoo.git

make it (using pd-libbuilder)::

    make -C aoo/pd clean
    make -C aoo/pd -j4 

install eg. in aoo dir::

    make -C aoo/pd PDLIBDIR=./ install

use help and testfiles there to test it.

from deken
..........

in Pd->Help->"find in externals" enter aoo

Note: TO BE DONE

   
Pd externals
------------

* [aoo_pack~] takes audio signals and outputs OSC messages (also accepts /request messages from sinks)
* [aoo_unpack~] takes OSC messages from several sources and turns them into audio signals
* [aoo_route] takes OSC messages and routes them based on the ID
* [aoo_send~] send an AoO stream (with threaded network IO)
* [aoo_receive~] receive one or more AoO streams (with threaded network IO)

OSC messages
------------

* message to notify sinks about format changes:

 /AoO/<sink>/format src(i) salt(i) nchannels(i) samplerate(i) blocksize(i) codec(s) options(b)

* message to deliver audio data, large blocks are split across several frames:

 /AoO/<sink>/data src(i) salt(i) seq(i) samplerate(d) channel_onset(i) totalsize(i) nframes(i) frame(i) data(b)

* message from sink to source to request the format (e.g. the salt has changed):

 /AoO/<src>/request sink(i)

* message from sink to source to request dropped packets; the arguments are pairs of sequence + frame (-1 = whole block):

 /AoO/<src>/resend sink(i) salt(i) [ seq(i) frame(i) ... ]

* ping message from sink to source (usually sent once per second):

 /AoO/<src>/ping sink(i)


todo
----

* fade in/fade out ?
* unit tests!
* publish

download
--------

main git repository at git.iem.at:

git clone https://git.iem.at/cm/aoo

content
-------

doku -- documentation, papers
 
pd -- Pd library for OSC, first implementation for experiments

lib -- C++ library with a C interface, create and manage AoO sources/sinks
 
Changelog
---------

- April 2020: go public
- New project page on Feb.2014 - winfried ritsch now on git.iem.at
- checked in in sourceforge repo (see above) 
- added aao_lib
- New test implementation Feb. 2020 - christof ressi
 
About Document
--------------
:authors: Winfried Ritsch, Christof Ressi
:date: march 2014 - february 2020
:version: 1.0-a1
 
