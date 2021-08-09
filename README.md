AOO (audio over OSC) v2.0-test3
===============================

AOO ("audio over OSC") is a message based audio system, using Open Sound Control [^OSC] as the underlying transport protocol.
It is fundamentally connectionless and allows to send audio in real time and on demand between arbitrary network endpoints.

**WARNING**: AOO is still alpha software, there are breaking changes between pre-releases!



### Features

* peer-to-peer audio networks of any topology with arbitrary ad-hoc connections

* each endpoint can have multiple sources/sinks (each with their own ID)

* AOO sources can send audio to any sink at any time;

* AOO sinks can listen to several sources at the same time, summing the signals

* AOO is connectionless: streams can start/stop at any time, enabling a "message-based audio" approach.

* AOO sinks can "invite" and "uninvite" sources, i.e. ask them to send resp. stop sending audio.
  The source may accept the (un)invitation or decline it.

* streams and invitations can contain arbitrary metadata

* AOO sinks and sources can operate at different blocksizes and samplerates

* AOO sources can dynamically change the channel onset

* timing differences (e.g. because of clock drifts) can be adjusted via a time DLL filter + dynamic resampling

* the stream format can be set independently for each source

* support for different audio codecs. Currently only PCM (uncompressed) and Opus (compressed) are implemented,
  but additional codecs can be added with the codec plugin API

* AooSource and AooSink C++ classes use lock-free queues, so that audio processing and network I/O
  can run on different threads without having to wait for each other

* the sink jitter buffer helps to deal with network jitter, packet reordering
  and packet loss at the cost of latency. The size can be adjusted dynamically.

* sinks can ask the source(s) to resend dropped packets; the settings are free adjustable.

* settable UDP packet size (to optimize for local networks or the internet)

* ping messages are automatically sent between sources and sinks at a configurable rate.
  They carry NTP timestamps, so the user can calculate the network latency and perform latency compensation.
  The source also receives the current average packet loss percentage.

* several diagnostic events about packet loss, packet reordering, resent packets, etc.

* AooServer is a UDP hole punching server [^Udp] that facilitates peer-to-peer communication over the public internet.

* AooClient connects to an AooServer and manages several AOO sources/sinks.



### History

The vision of AOO was first proposed in 2009 by Winfried Ritsch together with an initial draft of realisation for embedded devices.
In 2010 it has been implemented by Wolfgang Jäger as a library (v1.0-b2) with externals for Pure Data (Pd) [^Pd],
but major issues with the required networking objects made this version unpracticable and so it was not used extensively.
More on this version of AOO as "message based audio system" was published at LAC 2014 [^LAC14]

A new version (2.0, not backwards compatible) has been written from scratch by Christof Ressi, with a first pre-release in April 2020.
It has been initially developed in February 2020 for a network streaming project at Kunsthaus Graz with Bill Fontana, using an independent
wireless network infrastructure FunkFeuer Graz [^0xFF]. It was subsequently used for the Virtual Rehearsal Room project [^VRR]
and also helped reviving the Virtual IEM Computer Music Ensemble [^VICE] within a seminar at the IEM [^IEM] in Graz.



### Content

`aoo` -        AOO C/C++ library source code
`common` -     shared code
`deps` -       dependencies
`doku` -       documentation, papers
`include` -    public headers
`pd` -         Pd external
`sc` -         SC extension
`tests` -      test suite


#### C/C++ library

See the headers in the `aoo/include` directory for documentation.

The library features 4 object classes:

`AooSource` - AOO source object, see `aoo/include/aoo_source[.h|.hpp]`

`AooSink` - AOO sink object, see `aoo/include/aoo_sink[.h|.hpp]`

`AooClient` - AOO client object, `see aoo/include/aoo_client[.h|.hpp]`

`AooServer` - AOO UDP hole punching server, `see aoo/include/aoo_server[.h|.hpp]`

The `.h` files contain a set of C functions, the `.hpp` files contain C++ interfaces.

**NOTE**: By following certain COM conventions (no virtual destructors, no method overloading,
only POD parameters and simple return types), we achieve portable C++ interfaces on Windows
and all other platforms where the vtable layout for such classes is stable (generally true
for Linux and macOS, in my experience). This means you only have to build 'aoo' once as a
shared library and can then use its C++ interface in applications built with different
compilers resp. compiler versions. If you want to play it extra safe, use the C interface :-)


#### Pd external

Objects:

`[aoo_send~]` - send an AOO stream (with integrated threaded network IO)

`[aoo_receive~]` - receive one or more AOO streams (with integrated threaded network IO)

`[aoo_client]` - connect to AOO peers over the public internet or in a local network

`[aoo_server]` - AOO connection server

For documentation see the corresponding help patches.

The Pd external is available on Deken (in Pd -> Help -> "Find externals" search for "aoo".)


#### SC extension

TODO



### Build instructions

#### Prerequisites

1. Install CMake [^CMake]
2. Install Git [^Git]
2. Get the AOO source code: http://git.iem.at/cm/aoo/
   SSH: `git clone git@git.iem.at:cm/aoo.git`
   HTTPS: `git clone https://git.iem.at/cm/aoo.git`


##### Pure Data

1. Install Pure Data.
   Windows/macOS: http://msp.ucsd.edu/software.html
   Linux: `sudo apt-get install pure-data-dev
2. The `BUILD_PD_EXTERNAL` CMake variable must be `ON`
3. Make sure that `PD_INCLUDEDIR` points to the Pd `src` or `include` directory.
4. Windows: make sure that `PD_BINDIR` points to the Pd `bin` directory.
5. Set `PD_INSTALLDIR` to the desired installation path (if you're not happy with the default).

If you do *not* want to build the Pd external, set `BUILD_PD_EXTERNAL` to `OFF`!


##### SuperCollider

1. Clone the SuperCollider source code from https://github.com/supercollider/supercollider.
2. The `BUILD_SC_EXTENSION` CMake variable must be `ON`
3. Set `SC_INCLUDEDIR` to the `supercollider` folder (which should contain the subfolders `common` and `include`)
4. Set `SC_INSTALLDIR` to the desired installation path (if you're not happy with the default).
5. Set `SUPERNOVA` to `ON` if you want to also build a Supernova version.

If you do *not* want to build the SC extension, set `BUILD_SC_EXTENSION` to `OFF`!


##### Opus

Opus[^Opus] is a high quality low latency audio codec.
If you want to build AOO with integrated Opus support there are two options:

a) Link with the system wide `opus` library

1. Install Opus:
  * macOS -> homebrew: `brew install opus`
  * Windows -> Msys2: `pacman -S mingw32/mingw-w64-i686-opus` (32 bit) resp.
                     `pacman -S mingw64/mingw-w64-x86_64-opus` (64 bit)
  * Linux -> apt: `sudo apt-get install libopus-dev`
2. Set the `SYSTEM_OPUS` CMake variable to `ON` (see below).

b) Build Opus from source

1. Clone from `https://github.com/xiph/opus.git` into the `deps` folder.
2. Follow the build instructions and install it directly into the `opus` folder.
   (The static library should be in `opus/lib`.)
   **NOTE**: Don't build the shared library, i.e. `OPUS_BUILD_SHARED_LIBRARY` should be `OFF`.
3. Set the `SYSTEM_OPUS` CMake variable to `OFF` (see below).

**NOTE**: by default, a) will link dynamically with the Opus library and b) will link statically.
For local builds, a) is more convenient. If you want to ship it to other people, b) might be preferred.


#### Build

1. In the "aoo" folder create a subfolder named "build".
2. Navigate to `build` and execute `cmake .. <options>`. Available options are listed below.
3. Build the project with `cmake --build .`
4. Install the project with `cmake --build . --target install/strip`


##### CMake options

CMake options are set with the following syntax:
`cmake .. -D<name1>=<value1> -D<name2>=<value2>` etc.

**HINT**: setting options is much more convenient in a graphical interface like `cmake-gui`.
(You might need to install it seperately.)

These are the most important project options:
* `AOO_NET` (BOOL) - Build with integrated networking support (`AooClient` and `AooServer`).
   Disable it if you don't need it and want to reduce code size.
   **NOTE**: It is required for the Pd and SC extensions. Default: `ON`.
* `BUILD_AOO_STATIC` (BOOL) - Build static AOO library. Default: `ON`.
* `BUILD_AOO_SHARED` (BOOL) - Build shared AOO library. Default: `ON`.
* `BUILD_PD_EXTERNAL` (BOOL) - Build the Pd external. Default: `ON`.
* `BUILD_SC_EXTENSION` (BOOL) - Build the SuperCollider extension. Default: `ON`
* `CMAKE_BUILD_TYPE` (STRING) - Choose one of the following build types:
   "Release", "RelMinSize", "RelWithDebInfo", "Debug". Default: "Release".
* `CMAKE_INSTALL_PREFIX` (PATH) - Where to install the AOO C/C++ library.
* `CODEC_OPUS` (BOOL) - Enable/disable built-in Opus support
* `LOG_LEVEL` (STRING) - Choose one of the following log levels:
   "None", "Error", "Warning", "Verbose", "Debug". Default: "Warning".
* `STATIC_LIBS` (BOOL) - Linux and MinGW only:
   Link statically with `libgcc`, `libstdc++` and `libpthread`. Makes sure that the
   resulting binaries don't depend on specific system library versions. Default: `ON`.

`cmake-gui` will show all available options. Alternatively, run `cmake . -LH` from the `build` folder.


### Footnotes

[^CMake]: https://cmake.org/
[^Git]: https://git-scm.com/
[^IEM]: http://iem.at/
[^Jaeger]: https://phaidra.kug.ac.at/view/o:11413
[^LAC14]: see docu/lac2014_aoo.pdf
[^Opus]: https://opus-codec.org/
[^OSC]: http://opensoundcontrol.org/
[^Pd]: http://puredata.info/
[^Udp]: https://en.wikipedia.org/wiki/UDP_hole_punching
[^VICE]: https://iaem.at/projekte/ice/overview
[^VRR]: https://vrr.iem.at/
[^0xFF]: http://graz.funkfeuer.at/
