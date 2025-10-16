INSTALL
=======

# 1 Usage

There are several ways of integrating the AOO library into your own projects.

---

## 1.1 CMake subproject
  
You can copy the source code into your project, e.g. as a git submodule or subtree,
and then simply include it in your CMake project:

```cmake
project(my_project)

# assuming that `deps` contains the `aoo` source code
add_subdirectory(deps/aoo EXCLUDE_FROM_ALL)

add_executable(my_project main.cpp)

target_link_libraries(my_project PRIVATE Aoo::aoo)
```

If `BUILD_SHARED_LIBS` or `AOO_BUILD_SHARED_LIBRARIES` is set to `ON`,
`aoo` will be built as a shared library. Otherwise it is built as a static library.

See [Build instructions](INSTALL.md#build-instructions) for the most important options.
The default configuration should be fine for most projects.

---

## 1.2 CMake package
<a name="cmake-package"></a>

If the `aoo` library is installed (locally or system-wide), you can import to your CMake project with `find_package()`:

```cmake
project(my_project)

find_package(Aoo REQUIRED)

add_executable(my_project main.cpp)

target_link_libraries(my_project PRIVATE Aoo::aoo)
```

The CMake package honors the `BUILD_SHARED_LIBS` variable.
In addition, you can force the shared or static library by setting `AOO_SHARED_LIBS` to `ON` resp. `OFF` *before* calling `find_library(Aoo)`.

Typically, the `AooConfig.cmake` file is located at `<aoo_install_prefix>/lib/cmake/aoo`.

**NOTE:** If `aoo` has been installed in a non-standard location, you need to add the *install prefix* to the `CMAKE_PREFIX_PATH`:

```cmake
list(APPEND CMAKE_PREFIX_PATH "<aoo_install_prefix>")
```

If you build and install `aoo` yourself, make sure that `AOO_INSTALL_CMAKE_CONFIG_MODULE` is set to `ON` (= default).

---

## 1.3 pkg-config
<a name="pkg-config"></a>

By default, `aoo` also provides a pkg-config file (`aoo.pc`).
This allows `aoo` to be used with other build systems via `pkg-config`.

Here is a simple example Makefile:

```make
CC = gcc
CFLAGS = -g -Wall `pkg-config --cflags aoo`
LDFLAGS = `pkg-config --libs aoo`

all: my_project

main.o: main.c
        $(CC) -c main.c $(CFLAGS)

my_project: main.o
        $(CC) -o my_project main.o $(LDFLAGS)
```

Typically, the `aoo.pc` file is located at `<aoo_install_prefix>/lib/pkgconfig`.

**NOTE:** If `aoo` has been installed in a non-standard location, you need to add the relevant path to the `PKG_CONFIG_PATH` environment variable, e.g.:

```sh
export PKG_CONFIG_PATH="${PKG_CONFIG_PATH}:<aoo_install_prefix>/lib/pkconfig"
```

If you build and install `aoo` yourself, make sure that `AOO_INSTALL_PKG_CONFIG_MODULE` is set to `ON` (= default).

---

## 1.4 Manual

If you cannot use any of the methods above, you have to manually link with the `aoo` library and its dependencies.

With the shared library you only need to link with `aoo` itself.

With the static library you also need to link to some dependencies:

Linux/macOS: `-lpthread`

Windows (Msys2): `-lpthread -lws2_32 -lssp`

Windows (MSVC): `ws2_32.lib`

**NOTE:** If `aoo` has been built with a non-standard configuration, you need to add the appropriate preprocessor defines to your compiler flags. This is particularly important if `aoo` has been built with a different sample size (`AOO_SAMPLE_SIZE`)!

### 1.4.1 Opus

If `aoo` has been built with Opus support and you want to use the `codec/aoo_opus.h` header, you need to make sure that the `opus_multistream.h` header can be found by adding the appropriate include directory.

With the static library you also need to manually link with the `opus` library!

---

# 2 Build instructions

## 2.1 Prerequisites

1. Install CMake [^CMake]

2. Install Git [^Git]

3. Get the AOO source code: http://git.iem.at/cm/aoo/

   HTTPS: `git clone https://git.iem.at/cm/aoo.git`

   SSH: `git clone git@git.iem.at:cm/aoo.git`

4. Fetch submodules:
   `git submodule update --init`

---

### 2.1.2 Opus

Opus[^Opus] is a high quality low latency audio codec.

If you want to build AOO with integrated Opus support there are two options:

a. Use local Opus library (= default)

1. Make sure that `AOO_LOCAL_OPUS` CMake variable is `ON` (= default).

2. Now Opus will be included in the project and you can configure it as needed.

   **NOTE**: by default Opus will be built as a static library.
   If `BUILD_SHARED_LIBS` or `AOO_BUILD_SHARED_LIBRARY` is `ON`,
   both AOO and Opus will be built as shared libraries.

---

b. Link with the system `opus` library

1. Install Opus:

   macOS (homebrew): `brew install opus`

   Windows (Msys2):

   `pacman -S mingw32/mingw-w64-i686-opus` (32 bit) resp.

   `pacman -S mingw64/mingw-w64-x86_64-opus` (64 bit)

   Linux (apt): `sudo apt-get install libopus-dev`

2. Set the `AOO_LOCAL_OPUS` CMake variable to `OFF` (see below)

---

### 2.1.3 Pure Data

Follow these instructions if you want to build the Pd external.

1. Install Pure Data.

   Windows/macOS: http://msp.ucsd.edu/software.html

   Linux: install the `puredata-dev` package, e.g. `sudo apt install puredata-dev`

2. The `AOO_BUILD_PD_EXTERNAL` CMake variable must be `ON`.

3. If Pd cannot be found by the build system, or is installed in a non-standard location,
   you have to manually set `PD_INCLUDE_DIR` to the directory containing `m_pd.h`.
   On Windows you also have to set `PD_BIN_DIR` to the `bin` folder (containing `pd.dll`).

   On Windows and macOS you can alternatively set `PD_PATH` to your Pd application folder,
   which will in turn set `PD_INCLUDE_DIR` and `PD_BIN_DIR`.

4. Set `PD_INSTALL_DIR` to the desired installation path (if you're not happy with the default).

**NOTE**: If you *only* want to build and install the Pd external, set `AOO_INSTALL_LIBRARY`
to `OFF` to prevent the `aoo` library from being installed as well.

Additional options:

- `PD_EXTENSION` (STRING) - Override the default plugin extension.
  Example: `-DPD_EXTENSION=m_amd64_32`

- `PD_MULTI_INSTANCE` (BOOL) - Build with multi-instance support (for libpd). (Default = `OFF`)

- `PD_FLOAT_SIZE` (STRING) - Specify the float size. (Default = 32)
  Possible values: 32 (single precision) or 64 (double precision)

---

### 2.1.4 SuperCollider

1. macOS/Windows: clone the SuperCollider source code from https://github.com/supercollider/supercollider

   Linux: same as above. Alternatively, install the SuperCollider development package (e.g. Debian: `apt install supercollider-dev`).

2. The `AOO_BUILD_SC_EXTENSION` CMake variable must be `ON`.

3. Set `SC_PATH` to the SuperCollider source code directory, which will in turn set `SC_INCLUDE_DIR`.

   Linux: this is not necessary if you have installed `supercollider-dev`. In this case,  `SC_INCLUDE_DIR`
   is automatically set to the correct SuperCollider include directory, e.g. `/usr/include/SuperCollider`.

4. Set `SC_INSTALL_DIR` to the desired installation path (if you're not happy with the default).

5. Set `SC_SUPERNOVA` to `ON` if you want to also build the Supernova version.

**NOTE**: If you *only* want to build and install the SuperCollider external, set
`AOO_INSTALL_LIBRARY` to `OFF` to prevent the `aoo` library from being installed as well.

---

### 2.1.5 PortAudio

PortAudio is required for the example programs (`AOO_BUILD_EXAMPLES=ON`).

By default, the local portaudio in `deps/portaudio` will be used. If for some reason you want to use the system portaudio library, you can set `AOO_LOCAL_PORTAUDIO` to `OFF`.

---

### 2.1.6 Doxygen

Doxygen is required for building the API documentation (`AOO_BUILD_DOCUMENTATION=ON`).

---

### 2.1.7 macOS

By default, the minimum macOS deployment target is OSX 10.13. You may choose a *higher* version by setting the `CMAKE_OSX_DEPLOYMENT_TARGET` CMake variable.

---

## 2.2 Build

1. In the "aoo" folder create a subfolder named "build".

2. Navigate to `build` and execute `cmake .. <options>`. Available options are listed below.

3. Build the project with `cmake --build .`

4. Install the project with `cmake --build . --target install/strip`

---

## 2.3 CMake options

CMake options are set with the following syntax:
`cmake .. -D<name1>=<value1> -D<name2>=<value2>` etc.

**HINT**: setting options is much more convenient with a graphical UI like `cmake-gui` or `ccmake`.

These are the most important project options:

- `CMAKE_BUILD_TYPE` (STRING) - Choose one of the following build types:
  "Release", "RelMinSize", "RelWithDebInfo", "Debug". Default: "Release".

- `CMAKE_INSTALL_PREFIX` (PATH) - Where to install the AOO C/C++ library.

- `AOO_BUILD_SHARED_LIBRARY` (BOOL) - Build shared AOO library. (Default = `OFF`)

- `AOO_BUILD_DOCUMENTATION` (BOOL) - Build the API documentation. (Default = `OFF`)

- `AOO_BUILD_EXAMPLES` (BOOL) - Build the example programs. (Default = `OFF`)

- `AOO_BUILD_PD_EXTERNAL` (BOOL) - Build the Pd external. (Default = `OFF`)

* `AOO_BUILD_SC_EXTENSION` (BOOL) - Build the SuperCollider extension. (Default = `OFF`)

- `AOO_BUILD_SERVER` (BOOL) - Build the `aooserver` program. (Default = `OFF`)

- `AOO_BUILD_TESTS` (BOOL) - Build the test suite. (Default = `OFF`)

- `AOO_USE_OPUS` (BOOL) - Enable/disable built-in Opus support

- `AOO_LOG_LEVEL` (STRING) - Choose one of the following log levels:
  "None", "Error", "Warning", "Verbose", "Debug". Default: "Warning".

- `AOO_NET` (BOOL) - Build with integrated networking support (`AooClient` and `AooServer`).
  Disable it if you don't need it and want to reduce code size.
  **NOTE**: This option is required for the Pd external and `aooserver` program. (Default = `ON`)

- `AOO_STATIC_RUNTIME` (BOOL) - Linux and MinGW only:
  Link all binaries with static versions of `libgcc` and `libstdc++` and - on MinGW - also `libpthread`.
  This makes sure that the resulting binaries are portable across systems.
  (MingGW: default = `ON`, Linux: default = `OFF`)

- `AOO_NATIVE` (BOOL) - Optimize for this particular machine. (Default = `OFF`)
  NB: the resulting binaries are not portable and might not run on other machines!

- `AOO_SAMPLE_SIZE` (STRING) - Set the size of the audio sample type. (Default = 32)
  Possible values: 32 (single precision) or 64 (double precision)

- `CMAKE_INSTALL_LIBRARY` (BOOL) - Install the `aoo` library. (Default = `ON`)

- `CMAKE_INSTALL_CMAKE_CONFIG_MODULE` (BOOL) - Install CMake config module, see [1.2 CMake package](#cmake-package). (Default = `ON`)

- `CMAKE_INSTALL_CMAKE_CONFIG_MODULE` (BOOL) - Install pkg-config module, see [1.3 pkg-config](#pkg-config). (Default = `ON`)


`cmake-gui` resp. `ccmake` will show all available options. Alternatively, run `cmake . -LH` from the `build` folder.

---

# Footnotes

[^CMake]: https://cmake.org/

[^Git]: https://git-scm.com/
