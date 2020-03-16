Audio over OSC
==============

Audio over OSC is aimed to be a message based audio system using 
Open Sound Control OSC_ as a syntax format.

A first implementation a library with externals for PureData (Pd).

.. _OSC: http://opensoundcontrol.org/

.. _Pd: http://puredata.info/

More on message based audio system see docu/lac2014_aoo.pdf

features
--------

* each endpoint can have multiple sources/sinks (each with their own ID)
* AoO sources can send audio to any sink at any time; if the sink ID is a wildcard,
  it will send to all sinks on the endpoint.
* AoO sinks can listen to several sources at the same time
* AoO sinks and sources can operate at different blocksizes and samplerates
* AoO sources can dynamically change the channel onset
* timing differences (e.g. because of clock drifts) are adjusted via a time DLL filter + dynamic resampling
* the stream format can be set dynamically
* plugin API to register codecs; currently only PCM (uncompressed) and Opus (compressed) are implemented
* aoo_source and aoo_sink C++ classes have a lock-free ringbuffer, so that audio processing and network IO
  can run on different threads.
  In the case of aoo_sink, the buffer also helps to deal with network jitter, packet reordering
  and packet loss at the cost of latency. The size can be adjusted dynamically.
* aoo_sink can ask the source(s) to resend dropped packets, the settings are free adjustable.
* settable UDP packet size for audio data (to optimize for local networks or the internet)
* sinks automatically send pings to all its sources (at a configurable rate).
  For example, sources might want to stop sending if they don't receive a ping in a certain time period.

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

download
--------

main git repository at git.iem.at:

git clone https://git.iem.at/cm/aoo

content
-------

docu -- documentation, papers
 
pd -- Pd library for OSC, first implementation for experiments

lib -- C++ library with a C interface, create and manage AoO sources/sinks
 
Changelog
---------

- New project page on Feb.2014 - winfried ritsch
- checked in in sourceforge repo (see above) 
- added aao_lib
- New test implementation Feb. 2020 - christof ressi
 
About Document
--------------
:authors: Winfried Ritsch, Christof Ressi
:date: march 2014 - february 2020
:version: 0.1
 
