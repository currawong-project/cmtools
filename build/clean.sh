#!/bin/bash
#
# Run 'make distclean' to clean many of the temporary make files.
# then use this script run from cm/build to clean the remaining files
#



function clean_dir {

    make -C $1 uninstall
    make -C $1 distclean
    
    rm -f  $1/bin/kc.app/Contents/MacOS/kc
    
    
}



clean_dir linux/debug
clean_dir linux/release
clean_dir osx/debug
clean_dir osx/release

rm -rf osx/debug/a.out.dSYM

# delete everything created by 'autoreconf'.
rm -rf ../build-aux
rm -rf ../autom4te.cache
rm -f  ../config.h.in ../config.h.in~ ../configure ../libtool.m4
rm -f  ../Makefile.in ../aclocal.m4
rm -f  ../m4/libtool.m4 ../m4/ltoptions.m4 ../m4/ltsugar.m4 ../m4/ltversion.m4 ../m4/lt~obsolete.m4




