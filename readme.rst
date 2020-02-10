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
* AoO sinks and sources can operate at different blocksizes
* AoO sources can dynamically change the channel onset of the sink
* format message to notify sinks about format changes:
  /AoO/<sink>/format <src> <salt> <mime-type> <bitdepth> <nchannels> <samplerate> <blocksize> <overlap>
* data message to deliver audio data, large blocks are split across several frames:
  /AoO/<sink>/data <src> <salt> <seq> <t> <channel_onset> <numframes> <frame> <data...>
* request message from sink to source to ask about the format if necessary
  /AoO/<src>/request <sink>
* invitation message broadcasted by sinks at low intervals, inviting sources to send audio
  /AoO/<src>/invite
* aoo_source and aoo_sink C++ classes have a lock-free ringbuffer, so that audio processing and network IO
  can run on different threads. The buffer also helps to avoid network jitter at the cost of latency.
  The size can be adjusted dynamically.
* data messages can arrive out of order up to a certain bound (determined by buffer size)
* Pd externals:
  [aoo_source~] takes audio signals and outputs OSC messages (also accepts /request messages from sinks)
  [aoo_sink~] takes OSC messages from several sinks and turns them into audio signals
  [aoo_route] takes OSC messages and routes them based on the sink ID

todo
----

* resampling
* overlap
* interpolation of dropped packets
* fade in/fade out
* time correction (timestamps + DLL)
* settable max. OSC packet size
* [aoo_send~] + [aoo_receive~] with integrated (threaded) network IO
* replace oscpack with our own (optimized) OSC routines
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
 
