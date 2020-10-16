AM_CXXFLAGS =
AM_LDFLAGS  =  
AM_CPPFLAGS =
AM_CFLAGS   =

ACLOCAL_AMFLAGS = -I m4 # use custom macro's in ./m4 


# if we are building and linking to a nested copy of libcm
if BUILD_LIBCM
#  SUBDIRS      = src/libcm  # causes recursive make into given sub-directories
  AM_CPPFLAGS += -I$(srcdir)/src/libcm/src  -I$(srcdir)/src/libcm/src/dsp  -I$(srcdir)/src/libcm/src/vop  -I$(srcdir)/src/libcm/src/app
  AM_LDFLAGS  += -Lsrc/libcm/src
endif

# To Profile w/ gprof:
# 1) Modify configure: ./configure --disable-shared CFLAGS="-pg"
# 2) Run the program. ./foo
# 3) Run gprof /libtool --mode=execute gprof ./foo


# C compiler flags
# _GNU_SOURCE - turns on GNU specific extensions and gives correct prototype for double log2(double) 
# -Wall turn on all warnings
# -Wno-multichar - turns off multi-character constant warnings from cmAudioFile.c



AM_CPPFLAGS += -D _GNU_SOURCE  -I..
AM_CFLAGS   += -Wno-multichar 


# debug/release switches
if DEBUG
  AM_CFLAGS   += -g 
  AM_CXXFLAGS += -g
else
  AM_CFLAGS   += -O3
  AM_CXXFLAGS += -O3
endif

MYLIBS      = -lpthread  -lfftw3f -lfftw3 -lcm

# Linux specific
if OS_LINUX
 MYLIBS    +=  -lsatlas  -lasound
if OS_64
  AM_CFLAGS  += -m64
  AM_LDFLAGS += -L/usr/lib64/atlas -L/usr/lib64
  MYLIBS     += -lrt -lm 
endif	
endif

if OS_OSX
 AM_CPPFLAGS +=  -I/opt/local/include	# Search macports directory for fftw headers
 AM_LDFLAGS  +=  -L/opt/local/lib       # and libraries.
 AM_LDFLAGS  +=  -framework Cocoa -framework CoreAudio -framework CoreMIDI -framework Carbon -framework Accelerate
endif

src_cmtools_cmtools_SOURCES  = src/cmtools/cmtools.c 
src_cmtools_cmtools_LDADD    = $(MYLIBS)
bin_PROGRAMS                 = src/cmtools/cmtools

src_cmtools_mas_SOURCES  = src/cmtools/mas.c 
src_cmtools_mas_LDADD    = $(MYLIBS)
bin_PROGRAMS            += src/cmtools/mas 

src_cmtools_audiodev_SOURCES  = src/cmtools/audiodev.c 
src_cmtools_audiodev_LDADD    = $(MYLIBS)
bin_PROGRAMS            += src/cmtools/audiodev

# See: https://www.gnu.org/savannah-checkouts/gnu/automake/manual/html_node/Clean.html#Clean
# 'make distclean' sets the source tree back to it's pre-configure state
# 'distclean-local' is used by automake 'distclean' to perform customized local actions
# ${exec_prefix} is the install prefix given to 'configure' by the user.
# ${srcdir} is the directory of this Makefile and is set by autoconf.
distclean-local:
	rm -rf ${exec_prefix}/src        
	rm -rf ${exec_prefix}/bin        
	rm -rf ${exec_prefix}/include
	rm -rf ${exec_prefix}/lib        
