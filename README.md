# libsigrok with driver for TinyLogicFriend

This is a version of libsigrok with an added driver for TinyLogicFriend, a way to use many available development boards as a logic analyzer.

To load the TinyLogicFriend support onto your board, [go to the TinyLogicFriend firmware repository](https://github.com/kmatch98/tinylogicfriend) and follow the instructions of loading the `.UF2` file onto your board.

To use sigrok's PulseView software with TinyLogicFriend, right now you need to rebuild `libsigrok` (with this repository) and also build `sigrok-cli` and `PulseView`.

Right now, you need to build it all for yourself using the tinyLogicFriend driver from my github [`libsigrok` repository](https://github.com/kmatch98/libsigrok)**.  I strongly recommend using **[sigrok-util](https://github.com/sigrokproject/sigrok-util) to build for your platform**.  You will need to replace the baseline `libsigrok` library with this version that includes the tinyLogicFriend driver.  Then build everything, including `libsigrok`, `sigrok-cli` and `PulseView`. So far, I have succesfully built PulseView with tinyLogicFriend on an iMac running MacOS Big Sur.

----------
# building PulseView on MacOS

I've been successfull at building `libsigrok`, `sigrok-cli` and `PulseView` using the sigrok-util library, along with my special version of `libsigrok` with the TinyLogicFriend added.

I had to make some minor modifications to get it to build on MacOS.  Other platforms may differ.

I changed lines from these files:

directory: `/usr/local/Cellar/glib/2.68.1/include/glib-2.0/glib`

files: Update these 4 files
	- `gatomic.h`
	- `gmem.h`
	- `gmacros.h`
	- `grcbox.h`

In these four files, I commented out these lines, like this:

```c
/*
#if defined(glib_typeof_2_68) && GLIB_VERSION_MIN_REQUIRED >= GLIB_VERSION_2_68

#include <type_traits>
#endif
*/
```

--------

## Here are my system details

Running MacOS Big Sur 11.3 on iMac (Retina 5k, 27-inch, late 2015)

-------

MacOS gcc Version running:

```
margaret@iMac-86 bin % ./gcc --version
Configured with: --prefix=/Library/Developer/CommandLineTools/usr --with-gxx-include-dir=/usr/include/c++/4.2.1
Apple clang version 12.0.5 (clang-1205.0.22.9)
Target: x86_64-apple-darwin20.4.0
Thread model: posix
InstalledDir: /Library/Developer/CommandLineTools/usr/bin
```

———————

The result of my libsigrok/configure, showing what library versions that I have installed.

```
margaret@iMac-86 libsigrok % ./configure
checking for a BSD-compatible install... /usr/local/bin/ginstall -c
checking whether build environment is sane... yes
checking for a race-free mkdir -p... /usr/local/bin/gmkdir -p
checking for gawk... no
checking for mawk... no
checking for nawk... no
checking for awk... awk
checking whether make sets $(MAKE)... yes
checking whether make supports nested variables... yes
checking whether make supports nested variables... (cached) yes
checking whether make supports the include directive... yes (GNU style)
checking for gcc... gcc
checking whether the C compiler works... yes
checking for C compiler default output file name... a.out
checking for suffix of executables...
checking whether we are cross compiling... no
checking for suffix of object files... o
checking whether the compiler supports GNU C... yes
checking whether gcc accepts -g... yes
checking for gcc option to enable C11 features... none needed
checking whether gcc understands -c and -o together... yes
checking dependency style of gcc... gcc3
checking for ar... ar
checking the archiver (ar) interface... ar
checking build system type... x86_64-apple-darwin20.4.0
checking host system type... x86_64-apple-darwin20.4.0
checking for gcc... (cached) gcc
checking whether the compiler supports GNU C... (cached) yes
checking whether gcc accepts -g... (cached) yes
checking for gcc option to enable C11 features... (cached) none needed
checking whether gcc understands -c and -o together... (cached) yes
checking dependency style of gcc... (cached) gcc3
checking for g++... g++
checking whether the compiler supports GNU C++... yes
checking whether g++ accepts -g... yes
checking for g++ option to enable C++11 features... none needed
checking dependency style of g++... gcc3
checking whether ln -s works... yes
checking whether make supports order-only prerequisites... yes
checking how to print strings... printf
checking for a sed that does not truncate output... /usr/bin/sed
checking for grep that handles long lines and -e... /usr/bin/grep
checking for egrep... /usr/bin/grep -E
checking for fgrep... /usr/bin/grep -F
checking for ld used by gcc... /Library/Developer/CommandLineTools/usr/bin/ld
checking if the linker (/Library/Developer/CommandLineTools/usr/bin/ld) is GNU ld... no
checking for BSD- or MS-compatible name lister (nm)... /usr/bin/nm -B
checking the name lister (/usr/bin/nm -B) interface... BSD nm
checking the maximum length of command line arguments... 786432
checking how to convert x86_64-apple-darwin20.4.0 file names to x86_64-apple-darwin20.4.0 format... func_convert_file_noop
checking how to convert x86_64-apple-darwin20.4.0 file names to toolchain format... func_convert_file_noop
checking for /Library/Developer/CommandLineTools/usr/bin/ld option to reload object files... -r
checking for objdump... objdump
checking how to recognize dependent libraries... pass_all
checking for dlltool... no
checking how to associate runtime and link libraries... printf %s\n
checking for archiver @FILE support... no
checking for strip... strip
checking for ranlib... ranlib
checking command to parse /usr/bin/nm -B output from gcc object... ok
checking for sysroot... no
checking for a working dd... /bin/dd
checking how to truncate binary pipes... /bin/dd bs=4096 count=1
checking for mt... no
checking if : is a manifest tool... no
checking for dsymutil... dsymutil
checking for nmedit... nmedit
checking for lipo... lipo
checking for otool... otool
checking for otool64... no
checking for -single_module linker flag... yes
checking for -exported_symbols_list linker flag... yes
checking for -force_load linker flag... yes
checking for stdio.h... yes
checking for stdlib.h... yes
checking for string.h... yes
checking for inttypes.h... yes
checking for stdint.h... yes
checking for strings.h... yes
checking for sys/stat.h... yes
checking for sys/types.h... yes
checking for unistd.h... yes
checking for dlfcn.h... yes
checking for objdir... .libs
checking if gcc supports -fno-rtti -fno-exceptions... yes
checking for gcc option to produce PIC... -fno-common -DPIC
checking if gcc PIC flag -fno-common -DPIC works... yes
checking if gcc static flag -static works... no
checking if gcc supports -c -o file.o... yes
checking if gcc supports -c -o file.o... (cached) yes
checking whether the gcc linker (/Library/Developer/CommandLineTools/usr/bin/ld) supports shared libraries... yes
checking dynamic linker characteristics... darwin20.4.0 dyld
checking how to hardcode library paths into programs... immediate
checking whether stripping libraries is possible... yes
checking if libtool supports shared libraries... yes
checking whether to build shared libraries... yes
checking whether to build static libraries... yes
checking how to run the C++ preprocessor... g++ -E
checking for ld used by g++... /Library/Developer/CommandLineTools/usr/bin/ld
checking if the linker (/Library/Developer/CommandLineTools/usr/bin/ld) is GNU ld... no
checking whether the g++ linker (/Library/Developer/CommandLineTools/usr/bin/ld) supports shared libraries... yes
checking for g++ option to produce PIC... -fno-common -DPIC
checking if g++ PIC flag -fno-common -DPIC works... yes
checking if g++ static flag -static works... no
checking if g++ supports -c -o file.o... yes
checking if g++ supports -c -o file.o... (cached) yes
checking whether the g++ linker (/Library/Developer/CommandLineTools/usr/bin/ld) supports shared libraries... yes
checking dynamic linker characteristics... darwin20.4.0 dyld
checking how to hardcode library paths into programs... immediate
checking for pkg-config... /usr/local/bin/pkg-config
checking pkg-config is at least version 0.22... yes
checking for libserialport... yes
checking for libftdi... yes
checking for libhidapi... no
checking for libbluez... no
checking for libnettle... no
checking for libusb... yes
checking for librevisa... no
checking for libgpib... no
checking for libieee1284... no
checking for libgio... yes
checking compiler flag for C99... -std=c99
checking compiler flag for visibility... -fvisibility=hidden
checking which C compiler warning flags to use... -Wall -Wextra -Wmissing-prototypes
checking for special C compiler options needed for large files... no
checking for _FILE_OFFSET_BITS value needed for large files... no
checking whether byte ordering is bigendian... no
checking for sys/mman.h... yes
checking for sys/ioctl.h... yes
checking for sys/timerfd.h... no
checking for library containing pow... none required
checking for SunRPC support... yes
checking for libtirpc... no
checking for __int128_t... yes
checking for __uint128_t... yes
checking which C++ compiler warning flags to use... -Wall -Wextra
checking whether g++ supports C++11 features by default... no
checking whether g++ supports C++11 features with -std=c++11... yes
checking for doxygen... yes
checking for library containing __cxa_throw... none required
checking for a Python interpreter with version >= 2.7... python
checking for python... /Users/margaret/.pyenv/shims/python
checking for python version... 2.7
checking for python platform... darwin
checking for python script directory... ${prefix}/lib/python2.7/site-packages
checking for python extension module directory... ${exec_prefix}/lib/python2.7/site-packages
checking for stoi and stod... yes
checking python module: setuptools... yes
checking python module: numpy... no
checking for swig... swig
checking for swig version... 4.0.2
checking for ruby... /usr/bin/ruby
checking for Ruby version... /System/Library/Frameworks/Ruby.framework/Versions/2.6/usr/lib/ruby/2.6.0/universal-darwin20/rbconfig.rb:229: warning: Insecure world writable dir /Users/margaret in PATH, mode 040777
2.6.3
/System/Library/Frameworks/Ruby.framework/Versions/2.6/usr/lib/ruby/2.6.0/universal-darwin20/rbconfig.rb:229: warning: Insecure world writable dir /Users/margaret in PATH, mode 040777
checking for javac... yes
checking for gcj... no
checking for guavac... no
checking for jikes... no
checking for javac... javac
checking if javac works... yes
checking for javac... /usr/bin/javac
./configure: line 24923: [: : integer expression expected
checking jni headers... none
checking for jni.h... no
checking for glib-2.0 >= 2.32.0 libserialport >= 0.1.1 libftdi1 >= 1.0 libusb-1.0 >= 1.0.16 gio-2.0 >= 2.24.0 libzip >= 0.10... yes
checking for check >= 0.9.4 glib-2.0 libserialport >= 0.1.1 libftdi1 >= 1.0 libusb-1.0 >= 1.0.16 gio-2.0 >= 2.24.0 libzip >= 0.10... yes
checking for glibmm-2.4 >= 2.32.0... yes
checking for pygobject-3.0 >= 3.0.0 glibmm-2.4 >= 2.32.0... yes
checking for  glibmm-2.4 >= 2.32.0... yes
Must specify package names on the command line
Must specify package names on the command line
Must specify package names on the command line
checking for libusb_os_handle... no
checking for zip_discard... yes
checking that generated files are newer than configure... done
configure: creating ./config.status
config.status: creating Makefile
config.status: creating libsigrok.pc
config.status: creating bindings/cxx/libsigrokcxx.pc
config.status: creating config.h
config.status: creating include/libsigrok/version.h
config.status: executing depfiles commands
config.status: executing libtool commands

libsigrok configuration summary:
 - Package version................. 0.6.0-git-acdb0a9c
 - Library ABI version............. 4:0:0
 - Prefix.......................... /usr/local
 - Building on..................... x86_64-apple-darwin20.4.0
 - Building for.................... x86_64-apple-darwin20.4.0
 - Building shared / static........ yes / yes

Compile configuration:
 - C compiler...................... gcc
 - C compiler version.............. Apple clang version 12.0.5 (clang-1205.0.22.9)
 - C compiler flags................ -g -O2
 - Additional C compiler flags..... -std=c99 -fvisibility=hidden
 - C compiler warnings............. -Wall -Wextra -Wmissing-prototypes
 - C++ compiler.................... g++ -std=c++11
 - C++ compiler version............ Apple clang version 12.0.5 (clang-1205.0.22.9)
 - C++ compiler flags.............. -g -O2
 - C++ compiler warnings........... -Wall -Wextra
 - Linker flags....................

Detected libraries (required):
 - glib-2.0 >= 2.32.0.............. 2.68.1
 - libzip >= 0.10.................. 1.7.3

Detected libraries (optional):
 - libserialport >= 0.1.1.......... 0.1.1
 - libftdi1 >= 1.0................. 1.5
 - hidapi >= 0.8.0................. no
 - hidapi-hidraw >= 0.8.0.......... no
 - hidapi-libusb >= 0.8.0.......... no
 - bluez >= 4.0.................... no
 - nettle.......................... no
 - libusb-1.0 >= 1.0.16............ 1.0.24
 - librevisa >= 0.0.20130412....... no
 - libgpib......................... no
 - libieee1284..................... no
 - gio-2.0 >= 2.24.0............... 2.68.1
 - check >= 0.9.4.................. 0.15.2
 - glibmm-2.4 >= 2.32.0............ 2.66.0
 - python = 2.7.................... no
 - python2 = 2.7................... no
 - python27 = 2.7.................. no
 - python-2.7 = 2.7................ no
 - pygobject-3.0 >= 3.0.0.......... 3.40.1
 - ruby >= 2.5.0................... no
 - ruby-2.6 >= 2.5.0............... no

Enabled hardware drivers:
 - agilent-dmm..................... yes
 - appa-55ii....................... yes
 - arachnid-labs-re-load-pro....... yes
 - asix-sigma...................... yes
 - atten-pps3xxx................... yes
 - baylibre-acme................... no (missing: sys_timerfd_h)
 - beaglelogic..................... yes
 - cem-dt-885x..................... yes
 - center-3xx...................... yes
 - chronovu-la..................... yes
 - colead-slm...................... yes
 - conrad-digi-35-cpu.............. yes
 - demo............................ yes
 - dreamsourcelab-dslogic.......... yes
 - fluke-45........................ yes
 - fluke-dmm....................... yes
 - ftdi-la......................... yes
 - fx2lafw......................... yes
 - gmc-mh-1x-2x.................... yes
 - gwinstek-gds-800................ yes
 - gwinstek-gpd.................... yes
 - hameg-hmo....................... yes
 - hantek-4032l.................... yes
 - hantek-6xxx..................... yes
 - hantek-dso...................... yes
 - hp-3457a........................ yes
 - hp-3478a........................ no (missing: libgpib)
 - hung-chang-dso-2100............. no (missing: libieee1284)
 - ikalogic-scanalogic2............ yes
 - ikalogic-scanaplus.............. yes
 - ipdbg-la........................ yes
 - itech-it8500.................... yes
 - kecheng-kc-330b................. yes
 - kern-scale...................... yes
 - kingst-la2016................... yes
 - korad-kaxxxxp................... yes
 - lascar-el-usb................... yes
 - lecroy-logicstudio.............. yes
 - lecroy-xstream.................. yes
 - manson-hcs-3xxx................. yes
 - mastech-ms6514.................. yes
 - maynuo-m97...................... yes
 - mic-985xx....................... yes
 - microchip-pickit2............... yes
 - mooshimeter-dmm................. no (missing: bluetooth_comm)
 - motech-lps-30x.................. yes
 - norma-dmm....................... yes
 - openbench-logic-sniffer......... yes
 - pce-322a........................ yes
 - pipistrello-ols................. yes
 - rdtech-dps...................... yes
 - rdtech-um....................... yes
 - rdtech-tc....................... no (missing: libnettle)
 - rigol-ds........................ yes
 - rigol-dg........................ yes
 - rohde-schwarz-sme-0x............ yes
 - saleae-logic16.................. yes
 - saleae-logic-pro................ yes
 - scpi-dmm........................ yes
 - scpi-pps........................ yes
 - serial-dmm...................... yes
 - serial-lcr...................... yes
 - siglent-sds..................... yes
 - sysclk-lwla..................... yes
 - sysclk-sla5032.................. yes
 - teleinfo........................ yes
 - testo........................... yes
 - tiny-logic-friend-la............ yes
 - tondaj-sl-814................... yes
 - uni-t-dmm....................... yes
 - uni-t-ut181a.................... yes
 - uni-t-ut32x..................... yes
 - yokogawa-dlm.................... yes
 - zeroplus-logic-cube............. yes
 - zketech-ebd-usb................. yes

Enabled serial communication transports:
  - serial comm ................... yes
  - libserialport ................. yes
  - hidapi ........................ no
  - bluetooth ..................... no
  - bluez ......................... no

Enabled SCPI backends:
 - TCP............................. yes
 - SunRPC ......................... yes
 - TI-RPC ......................... no
 - RPC............................. yes
 - serial.......................... yes
 - VISA............................ no
 - GPIB............................ no
 - USBTMC.......................... yes

Enabled language bindings:
 - C++............................. yes
 - Python.......................... no (missing: Headers, numpy)
 - Ruby............................ no (missing: Headers)
 - Java............................ no (missing: JNI headers)

margaret@iMac-86 libsigrok %
```
