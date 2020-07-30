AC_DEFUN([AX_FUNC_OS_TYPE],
[AC_CACHE_CHECK([operating system type],
[ax_cv_os_type],
[ax_cv_os_type=`uname`])
if test x"$ax_cv_os_type" = xLinux; then
AC_DEFINE([OS_LINUX], 1,[Operating system is Linux.])
fi
if test x"$ax_cv_os_type" = xDarwin; then
AC_DEFINE([OS_OSX], 1,[Operating system is Darwin.])
fi]) # AX_FUNC_OS_TYPE

