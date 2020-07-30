AC_DEFUN([AX_FUNC_OS_64],
[AC_CACHE_CHECK([operating system address width],
[ax_cv_os_64],
[ax_cv_os_64=`uname -m`])
if test x"$ax_cv_os_64" = xx86_64; then
AC_DEFINE([OS_64], 1,[Operating system is 64 bits.])
fi
]) # AX_FUNC_OS_TYPE
