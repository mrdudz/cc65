#! /bin/bash

SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

VERBOSE=0

# assume this script lives in BASE_DIR/util
BASE_DIR=$SCRIPT_DIR/..

#echo SCRIPT_DIR: $SCRIPT_DIR
#echo BASE_DIR: $BASE_DIR

INCLUDEDIR=$BASE_DIR/include/

CL65=$BASE_DIR/bin/cl65
CC65=$BASE_DIR/bin/cc65

TARGET=c64

#HEADERFILE=conio.h

# todo: use proper temp files and clean up later

TMPFILE=$SCRIPT_DIR/test.tmp
FUNCFILE=$SCRIPT_DIR/test2.tmp
CALLFILE=$SCRIPT_DIR/test3.tmp

ERRFILE=$SCRIPT_DIR/out.err
TESTFILE=$SCRIPT_DIR/tmp.c
TESTPRG=$SCRIPT_DIR/tmp.prg

function cleanup
{
    rm -f $TMPFILE
    rm -f $FUNCFILE
    rm -f $CALLFILE
    rm -f $ERRFILE
    rm -f $TESTFILE
    rm -f $TESTPRG
}

function checkheader
{
    HEADERFILE=$1

    CL65OPT="-t "$TARGET

    rm -f $TMPFILE

    #echo gcc $CDEFS -I $INCLUDEDIR -E -o $TMPFILE $INCLUDEDIR/$HEADERFILE
    #gcc $CDEFS -I $INCLUDEDIR -E -o $TMPFILE $INCLUDEDIR/$HEADERFILE
    #echo $CC65 $CL65OPT -I $INCLUDEDIR -E -o $TMPFILE $INCLUDEDIR/$HEADERFILE
    $CC65 $CL65OPT -I $INCLUDEDIR -E -o $TMPFILE $INCLUDEDIR/$HEADERFILE

    cat $TMPFILE \
        | grep -v '^#' \
        | grep -v '^typedef' \
        | grep -v '^$' \
        | sed -e "s/^ *//g" \
        | tr --delete '\n' \
        | tr ';' '\n' \
        | sed -e "s/\(signed *\)\([,)]\)/\1 FOO\2/g" \
        | sed -e "s/\(char\* *\)\([,)]\)/\1 FOO\2/g" \
        | sed -e "s/char\* *[,)]/char\* FOO/g" \
        | grep '(.*)' > $FUNCFILE
    
cat $FUNCFILE \
    | sed -e "s/__attribute__ *((noreturn))//g" \
    | sed -e "s/__fastcall__ //g" \
    | sed -e "s/__cdecl__ //g" \
    | sed -e "s/const //g" \
    | sed -e "s/const\* //g" \
    | sed -e "s/extern //g" \
    | sed -e "s/register //g" \
    | sed -e "s/struct //g" \
    \
    | sed -e "s/unsigned/signed/g" \
    | sed -e "s/signed char/char/g" \
    | sed -e "s/signed int/int/g" \
    | sed -e "s/signed long/long/g" \
    | sed -e "s/^signed[ \*]//g" \
    | sed -e "s/^void[ \*]//g" \
    | sed -e "s/^char[ \*]//g" \
    | sed -e "s/^int[ \*]//g" \
    | sed -e "s/^long[ \*]//g" \
    | sed -e "s/^FILE[ \*]//g" \
    | sed -e "s/^DIR[ \*]//g" \
    | sed -e "s/^size_t[ \*]//g" \
    | sed -e "s/^fpos_t[ \*]//g" \
    | sed -e "s/^div_t[ \*]//g" \
    | sed -e "s/^off_t[ \*]//g" \
    | sed -e "s/^time_t[ \*]//g" \
    | sed -e "s/^clockid_t[ \*]//g" \
    | sed -e "s/^clock_t[ \*]//g" \
    | sed -e "s/^intmax_t[ \*]//g" \
    | sed -e "s/^uintmax_t[ \*]//g" \
    | sed -e "s/^dhandle_t[ \*]//g" \
    | sed -e "s/^__va_list[ \*]//g" \
    | sed -e "s/^va_list[ \*]//g" \
    | sed -e "s/^cbm_dirent[ \*]//g" \
    | sed -e "s/^dirent[ \*]//g" \
    | sed -e "s/^__sigfunc[ \*]//g" \
    | sed -e "s/^jmp_buf[ \*]//g" \
    | sed -e "s/^lconv[ \*]//g" \
    | sed -e "s/^timespec[ \*]//g" \
    | sed -e "s/^tm[ \*]//g" \
    | sed -e "s/^tgi_vectorfont[ \*]//g" \
    \
    | sed -e "s/ *(\(\* \)\([^ ),]*) *([^)]*)\)/\1x/g" \
    | sed -e "s/\([ (,]char \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]int \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]long \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]signed \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]FILE \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]DIR \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]size_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]fpos_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]div_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]off_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]time_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]clockid_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]clock_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]intmax_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]uintmax_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]dhandle_t \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]__va_list \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]va_list \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]cbm_dirent \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]dirent \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]__sigfunc \)\([^ ),]*\)/\1 NULL/g" \
    | sed -e "s/\([ (,]jmp_buf \)\([^ ),]*\)/\1 NULL/g" \
    | sed -e "s/\([ (,]lconv \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]timespec \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]tm \)\([^ ),]*\)/\1 0/g" \
    | sed -e "s/\([ (,]tgi_vectorfont \)\([^ ),]*\)/\1 0/g" \
    \
    | sed -e "s/\(\* \)\([^ ),]*\)/\1 NULL/g" \
    | sed -e "s/\*//g" \
    \
    | sed -e "s/(int)/(0)/g" \
    \
    | sed -e "s/\([ (,]\)const /\1/g" \
    | sed -e "s/\([ (,]\)void/\1/g" \
    | sed -e "s/\([ (,]\)unsigned /\1/g" \
    | sed -e "s/\([ (,]\)signed /\1/g" \
    | sed -e "s/\([ (,]\)char /\1/g" \
    | sed -e "s/\([ (,]\)int /\1/g" \
    | sed -e "s/\([ (,]\)long /\1/g" \
    | sed -e "s/\([ (,]\)FILE /\1/g" \
    | sed -e "s/\([ (,]\)DIR /\1/g" \
    | sed -e "s/\([ (,]\)size_t /\1/g" \
    | sed -e "s/\([ (,]\)fpos_t /\1/g" \
    | sed -e "s/\([ (,]\)div_t /\1/g" \
    | sed -e "s/\([ (,]\)off_t /\1/g" \
    | sed -e "s/\([ (,]\)time_t /\1/g" \
    | sed -e "s/\([ (,]\)clockid_t /\1/g" \
    | sed -e "s/\([ (,]\)clock_t /\1/g" \
    | sed -e "s/\([ (,]\)intmax_t /\1/g" \
    | sed -e "s/\([ (,]\)uintmax_t /\1/g" \
    | sed -e "s/\([ (,]\)dhandle_t /\1/g" \
    | sed -e "s/\([ (,]\)__va_list /\1/g" \
    | sed -e "s/\([ (,]\)va_list /\1/g" \
    | sed -e "s/\([ (,]\)cbm_dirent /\1/g" \
    | sed -e "s/\([ (,]\)dirent /\1/g" \
    | sed -e "s/\([ (,]\)__sigfunc /\1/g" \
    | sed -e "s/\([ (,]\)jmp_buf /\1/g" \
    | sed -e "s/\([ (,]\)lconv /\1/g" \
    | sed -e "s/\([ (,]\)timespec /\1/g" \
    | sed -e "s/\([ (,]\)tm /\1/g" \
    | sed -e "s/\([ (,]\)tgi_vectorfont /\1/g" \
    \
    | sed -e "s/\.\.\./0/g" \
    | sed -e "s/ //g" \
    > $CALLFILE

    if [ $VERBOSE != 0 ]; then
        echo "checking API of header" $HEADERFILE "for" $TARGET
    fi

lines=$(cat $CALLFILE)
for Line in $lines
do
#        echo testing: $Line

    echo "#include <stdlib.h>"  > $TESTFILE
    echo "#include <stdio.h>"  >> $TESTFILE
    echo "#include "'<'$HEADERFILE'>'  >> $TESTFILE
    echo "int main(void) {"  >> $TESTFILE
    echo "$Line"';' >> $TESTFILE
    echo "return EXIT_SUCCESS;"  >> $TESTFILE
    echo "}"  >> $TESTFILE

#        cat $TESTFILE
    $CL65 $CL65OPT $TESTFILE -o $TESTPRG 2> $ERRFILE > /dev/null
    ERR=$?
#        echo err:$ERR
    if [ $ERR != 0 ]; then
        echo $HEADERFILE":" $Line;
        if [ $VERBOSE != 0 ]; then
            cat $ERRFILE
            cat $TESTFILE
        fi
#    else
#        echo "OK: " $Line;
    fi
done

}

ALLTARGETS+="c64 "
ALLTARGETS+="c128 "
ALLTARGETS+="vic20 "
ALLTARGETS+="plus4 "
ALLTARGETS+="pet "
ALLTARGETS+="atari "
ALLTARGETS+="apple2 "
ALLTARGETS+="apple2enh "
ALLTARGETS+="supervision "
ALLTARGETS+="telestrat "
ALLTARGETS+="pce "
ALLTARGETS+="osic1p "
ALLTARGETS+="nes "
ALLTARGETS+="lynx "
ALLTARGETS+="geos "
ALLTARGETS+="gamate "
ALLTARGETS+="creativision "
ALLTARGETS+="atmos "
ALLTARGETS+="atari7800 "
ALLTARGETS+="atari5200 "
ALLTARGETS+="atari2600 "

STDHEADER+="ctype.h "
STDHEADER+="dirent.h "
STDHEADER+="errno.h "
STDHEADER+="fcntl.h "
STDHEADER+="inttypes.h "
STDHEADER+="limits.h "
STDHEADER+="locale.h "
STDHEADER+="setjmp.h "
STDHEADER+="signal.h "
STDHEADER+="stdarg.h "
STDHEADER+="stdbool.h "
STDHEADER+="stddef.h "
STDHEADER+="stdint.h "
STDHEADER+="stdio.h "
STDHEADER+="stdlib.h "
STDHEADER+="string.h "
STDHEADER+="time.h "
STDHEADER+="unistd.h "
STDHEADER+="assert.h "
STDHEADER+="iso646.h "

CC65HEADER+="accelerator.h "
CC65HEADER+="cc65.h "
CC65HEADER+="conio.h "
CC65HEADER+="mouse.h "
CC65HEADER+="serial.h "
CC65HEADER+="tgi.h "
CC65HEADER+="em.h "
CC65HEADER+="joystick.h "
CC65HEADER+="lz4.h "
CC65HEADER+="modload.h "
CC65HEADER+="peekpoke.h "
CC65HEADER+="6502.h "
CC65HEADER+="dbg.h "
CC65HEADER+="device.h "
CC65HEADER+="dio.h "
CC65HEADER+="target.h "
CC65HEADER+="zlib.h "
CC65HEADER+="pen.h "
CC65HEADER+="o65.h "

if [ x$1 == x ]; then
CHECKHEADER=$STDHEADER
CHECKHEADER+=$CC65HEADER
else
CHECKHEADER=$1
fi

for target in $ALLTARGETS
do
    TARGET=$target

    echo "Listing missing API functions for target:" $TARGET
    for hdr in $CHECKHEADER
    do
        checkheader $hdr
    done
    echo -ne "\n"
done

cleanup

exit

# TODO: make lists/options for the rest of the header files

apple2enh.h
apple2_filetype.h
apple2.h
ascii_charmap.h
atari2600.h
atari5200.h
atari7800.h
atari_atascii_charmap.h
atari.h
atari_screen_charmap.h
atmos.h
c128.h
c16.h
c64.h
cbm264.h
cbm510.h
cbm610.h

cbm_filetype.h
cbm.h
cbm_petscii_charmap.h
cbm_screen_charmap.h


creativision.h

cx16.h


gamate.h

geos.h

lynx.h

nes.h
osic1p.h

pce.h
pet.h

plus4.h

supervision.h

sym1.h


telestrat.h


vic20.h

