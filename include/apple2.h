/*****************************************************************************/
/*                                                                           */
/*                                 apple2.h                                  */
/*                                                                           */
/*                   Apple ][ system specific definitions                    */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 2000  Kevin Ruland, <kevin@rodin.wustl.edu>                           */
/* (C) 2003  Ullrich von Bassewitz, <uz@cc65.org>                            */
/*                                                                           */
/*                                                                           */
/* This software is provided 'as-is', without any expressed or implied       */
/* warranty.  In no event will the authors be held liable for any damages    */
/* arising from the use of this software.                                    */
/*                                                                           */
/* Permission is granted to anyone to use this software for any purpose,     */
/* including commercial applications, and to alter it and redistribute it    */
/* freely, subject to the following restrictions:                            */
/*                                                                           */
/* 1. The origin of this software must not be misrepresented; you must not   */
/*    claim that you wrote the original software. If you use this software   */
/*    in a product, an acknowledgment in the product documentation would be  */
/*    appreciated but is not required.                                       */
/* 2. Altered source versions must be plainly marked as such, and must not   */
/*    be misrepresented as being the original software.                      */
/* 3. This notice may not be removed or altered from any source              */
/*    distribution.                                                          */
/*                                                                           */
/*****************************************************************************/



#ifndef _APPLE2_H
#define _APPLE2_H



/* Check for errors */
#if !defined(__APPLE2__)
#  error This module may only be used when compiling for the Apple ][!
#endif

#include <time.h>
#include <apple2_filetype.h>



/*****************************************************************************/
/*                                   Data                                    */
/*****************************************************************************/



/* Color defines */
#define COLOR_BLACK     0x00
#define COLOR_WHITE     0x01

/* TGI color defines */
#define TGI_COLOR_BLACK         0x00
#define TGI_COLOR_GREEN         0x01
#define TGI_COLOR_PURPLE        0x02
#define TGI_COLOR_WHITE         0x03
#define TGI_COLOR_BLACK2        0x04
#define TGI_COLOR_ORANGE        0x05
#define TGI_COLOR_BLUE          0x06
#define TGI_COLOR_WHITE2        0x07

#define TGI_COLOR_MAGENTA       TGI_COLOR_BLACK2
#define TGI_COLOR_DARKBLUE      TGI_COLOR_WHITE2
#define TGI_COLOR_DARKGREEN     0x08
#define TGI_COLOR_GRAY          0x09
#define TGI_COLOR_CYAN          0x0A
#define TGI_COLOR_BROWN         0x0B
#define TGI_COLOR_GRAY2         0x0C
#define TGI_COLOR_PINK          0x0D
#define TGI_COLOR_YELLOW        0x0E
#define TGI_COLOR_AQUA          0x0F

/* Characters codes */
#define CH_ENTER        0x0D
#define CH_ESC          0x1B
#define CH_CURS_LEFT    0x08
#define CH_CURS_RIGHT   0x15

#if !defined(__APPLE2ENH__)
#define CH_HLINE        '-'
#define CH_VLINE        '!'
#define CH_ULCORNER     '+'
#define CH_URCORNER     '+'
#define CH_LLCORNER     '+'
#define CH_LRCORNER     '+'
#define CH_TTEE         '+'
#define CH_BTEE         '+'
#define CH_LTEE         '+'
#define CH_RTEE         '+'
#define CH_CROSS        '+'
#endif

/* Masks for joy_read */
#define JOY_UP_MASK     0x10
#define JOY_DOWN_MASK   0x20
#define JOY_LEFT_MASK   0x04
#define JOY_RIGHT_MASK  0x08
#define JOY_BTN_1_MASK  0x40
#define JOY_BTN_2_MASK  0x80

/* Return codes for get_ostype */
#define APPLE_UNKNOWN   0x00
#define APPLE_II        0x10  /* Apple ][                    */
#define APPLE_IIPLUS    0x11  /* Apple ][+                   */
#define APPLE_IIIEM     0x20  /* Apple /// (emulation)       */
#define APPLE_IIE       0x30  /* Apple //e                   */
#define APPLE_IIEENH    0x31  /* Apple //e (enhanced)        */
#define APPLE_IIECARD   0x32  /* Apple //e Option Card       */
#define APPLE_IIC       0x40  /* Apple //c                   */
#define APPLE_IIC35     0x41  /* Apple //c (3.5 ROM)         */
#define APPLE_IICEXP    0x43  /* Apple //c (Mem. Exp.)       */
#define APPLE_IICREV    0x44  /* Apple //c (Rev. Mem. Exp.)  */
#define APPLE_IICPLUS   0x45  /* Apple //c Plus              */
#define APPLE_IIGS      0x80  /* Apple IIgs                  */
#define APPLE_IIGS1     0x81  /* Apple IIgs (ROM 1)          */
#define APPLE_IIGS3     0x83  /* Apple IIgs (ROM 3)          */

/* Return codes for get_tv() */
#define TV_NTSC  0
#define TV_PAL   1
#define TV_OTHER 2

extern unsigned char _dos_type;
/* Valid _dos_type values:
**
** AppleDOS 3.3   - 0x00
** ProDOS 8 1.0.1 - 0x10
** ProDOS 8 1.0.2 - 0x10
** ProDOS 8 1.1.1 - 0x11
** ProDOS 8 1.2   - 0x12
** ProDOS 8 1.3   - 0x13
** ProDOS 8 1.4   - 0x14
** ProDOS 8 1.5   - 0x15
** ProDOS 8 1.6   - 0x16
** ProDOS 8 1.7   - 0x17
** ProDOS 8 1.8   - 0x18
** ProDOS 8 1.9   - 0x18 (!)
** ProDOS 8 2.0.1 - 0x21
** ProDOS 8 2.0.2 - 0x22
** ProDOS 8 2.0.3 - 0x23
** ProDOS 8 2.4.x - 0x24
*/

/* struct stat.st_mode values */
#define S_IFDIR  0x01
#define S_IFREG  0x02
#define S_IFBLK  0xFF
#define S_IFCHR  0xFF
#define S_IFIFO  0xFF
#define S_IFLNK  0xFF
#define S_IFSOCK 0xFF

struct datetime {
    struct {
        unsigned day  :5;
        unsigned mon  :4;
        unsigned year :7;
    }                 date;
    struct {
        unsigned char min;
        unsigned char hour;
    }                 time;
};



/*****************************************************************************/
/*                                 Variables                                 */
/*****************************************************************************/



/* The file stream implementation and the POSIX I/O functions will use the
** following struct to set the date and time stamp on files. This specifically
** applies to the open and fopen functions.
*/
extern struct datetime _datetime;

/* The addresses of the static drivers */
#if !defined(__APPLE2ENH__)
extern void a2_auxmem_emd[];
extern void a2_stdjoy_joy[];     /* Referred to by joy_static_stddrv[]   */
extern void a2_stdmou_mou[];     /* Referred to by mouse_static_stddrv[] */
extern void a2_ssc_ser[];        /* Referred to by ser_static_stddrv[]   */
extern void a2_gs_ser[];         /* IIgs serial driver                   */
extern void a2_hi_tgi[];         /* Referred to by tgi_static_stddrv[]   */
extern void a2_lo_tgi[];
#endif



/*****************************************************************************/
/*                                   Code                                    */
/*****************************************************************************/



void beep (void);
/* Beep beep. */

unsigned char get_tv (void);
/* Get the machine vblank frequency. Returns one of the TV_xxx codes. */

unsigned char get_ostype (void);
/* Get the machine type. Returns one of the APPLE_xxx codes. */

void rebootafterexit (void);
/* Reboot machine after program termination has completed. */

#define ser_apple2_slot(num)  ser_ioctl (0, (void*) (num))
/* Select a slot number from 1 to 7 prior to ser_open.
** The default slot number is 2.
*/

#define tgi_apple2_mix(onoff)  tgi_ioctl (0, (void*) (onoff))
/* If onoff is 1, graphics/text mixed mode is enabled.
** If onoff is 0, graphics/text mixed mode is disabled.
*/

/* The following #defines will cause the matching functions calls in conio.h
** to be overlaid by macros with the same names, saving the function call
** overhead.
*/
#define _textcolor(color)       COLOR_WHITE
#define _bgcolor(color)         COLOR_BLACK
#define _bordercolor(color)     COLOR_BLACK
#define _cpeekcolor()           COLOR_WHITE
#define _cpeekrevers()          0

struct tm* __fastcall__ gmtime_dt (const struct datetime* dt);
/* Converts a ProDOS date/time structure to a struct tm */

time_t __fastcall__ mktime_dt (const struct datetime* dt);
/* Converts a ProDOS date/time structure to a time_t UNIX timestamp */

typedef struct DIR DIR;

unsigned int __fastcall__ dir_entry_count(DIR *dir);
/* Returns the number of active files in a ProDOS directory */

#if !defined(__APPLE2ENH__)
unsigned char __fastcall__ allow_lowercase (unsigned char onoff);
/* If onoff is 0, lowercase characters printed to the screen via STDIO and
** CONIO are forced to uppercase. If onoff is 1, lowercase characters are
** printed to the screen untouched.  By default lowercase characters are
** forced to uppercase because a stock Apple ][+ doesn't support lowercase
** display. The function returns the old lowercase setting.
*/
#endif



/* End of apple2.h */
#endif
