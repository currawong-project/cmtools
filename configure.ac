#
# Use "autoreconf --force --install" to update depedent files after changing 
# this configure.ac or any of the Makefile.am files.
#

AC_INIT([cmtools],[1.0],[kl@currawongproject.org])
AC_CONFIG_AUX_DIR([build-aux])                    # put aux files in build-aux
AM_INIT_AUTOMAKE([1.9 -Wall foreign subdir-objects]) # subdir-objects needed for non-recursive make
AC_CONFIG_SRCDIR([src/cmtools/cmtools.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_PROG_AR

LT_INIT

# Check for programs
AC_PROG_CC
AC_PROG_CXX
# AC_PROG_RANLIB  # required for static librarires

AM_PROG_CC_C_O

# Checks for libraries.
# AC_CHECK_LIB([cairo],[cairo_debug_reset_static_data],[AC_MSG_RESULT([The 'cairo' library was found.])],[AC_MSG_ERROR([The 'cairo' library was not found.])])
#TODO: add more library checks 

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h limits.h mach/mach.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_FSEEKO
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([clock_gettime floor memmove memset mkdir pow rint select socket sqrt strcasecmp strchr strcspn strerror strspn strstr strtol])


# The following is a custom macro in ./m4/os_type.m4
# be sure to also set "ACLOCAL_AMFLAGS = -I m4" in ./Makefile.am
# Defines the config.h variable OS_LINUX or OS_OSX
AX_FUNC_OS_TYPE

AX_FUNC_OS_64

# ac_cv_os_type is set by AX_FUNC_OS_TYPE
AM_CONDITIONAL([OS_LINUX],[test x"${ax_cv_os_type}" = xLinux])
AM_CONDITIONAL([OS_OSX],[test x"${ax_cv_os_type}" = xDarwin])
echo "OS='${ax_cv_os_type}'"

AM_CONDITIONAL([OS_64],[test x"${ax_cv_os_64}" == xx86_64])
echo "ptr width='${ax_cv_os_64}'"

# check if a request has been made to build libcm
AC_ARG_ENABLE([build_libcm],
     [  --enable-build_libcm    libcm is included in the local source tree],
     [case "${enableval}" in
       yes) build_libcm=true ;;
       no)  build_libcm=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-build_libcm]) ;;
     esac],[build_libcm=false])
     
echo "build_libcm=${build_libcm}"

# check if a nested copy of libcm exists in /src/libcm
AC_CHECK_FILE([${srcdir}/src/libcm/src/cmGlobal.h],[local_libcm=true],[local_libcm=false])
echo "local_libcm=${local_libcm}"

# set BUILD_LIBCM if a libcm build request was set and a nested copy of libcm exists
AM_CONDITIONAL([BUILD_LIBCM], [test x$build_libcm = xtrue -a x$local_libcm = xtrue ])

AC_ARG_ENABLE([debug],
     [  --enable-debug    Turn on debugging],
     [case "${enableval}" in
       yes) debug=true ;;
       no)  debug=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
     esac],[debug=false])
     
echo "debug=${debug}"
     
AM_CONDITIONAL([DEBUG], [test x$debug = xtrue])

if test x$debug = xfalse; then
AC_DEFINE([NDEBUG], 1,[Debugging off.])
fi

AC_ARG_ENABLE([vectop],
     [  --enable-vectop    Turn on use of Lapack and Atlas vector/matrix operations. ],
     [case "${enableval}" in
       yes) vectop=true ;;
       no)  vectop=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-vectop]) ;;
     esac],[vectop=true])
     
echo "vectop=${vectop}"
     
# if --enable-vectop then #define CM_VECTOP = 1 in config.h otherwise CM_VECTOP is undefined.
if test x"$vectop" = xtrue; then
AC_DEFINE([CM_VECTOP], 1,[Use Lapack and Atlas.])
fi


AC_ARG_ENABLE([memalign],
     [  --enable-memalign    Turn on memory alignment on dynamic memory allocations. ],
     [case "${enableval}" in
       yes) memalign=true ;;
       no)  memalign=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-memalign]) ;;
     esac],[memalign=true])
     
echo "memalign=${memalign}"

# if --enable-vectop then #define CM_MEMALIGN = 1 in config.h otherwise CM_MEMALIGN is undefined.     
if test x"$memalign" = xtrue; then
AC_DEFINE([CM_MEMALIGN], 1,[Turn on dynamic memory alignment.])
fi

AC_ARG_ENABLE([sonicart],
     [  --enable-sonicart    Enable use of Sonic Arts proprietary code. ],
     [case "${enableval}" in
       yes) sonicart=true ;;
       no)  sonicart=false ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-sonicart]) ;;
     esac],[sonicart=false])
     
echo "sonicart=${sonicart}"
     
# if --enable-sonicart then #define CM_SONICART = 1 in config.h otherwise CM_SONICART is undefined.
if test x"$sonicart" = xtrue; then
AC_DEFINE([CM_SONICART], 1,[Include Sonic Arts proprietry code.])
fi

AM_CONDITIONAL([INC_SONICART], [test x$sonicart = xtrue])

AC_CONFIG_FILES([ Makefile ])

# if local nested libcm then do recursive configure into subdirs
if test x$build_libcm = xtrue -a x$local_libcm = xtrue; then
AC_CONFIG_SUBDIRS([src/libcm])
fi

AC_OUTPUT
