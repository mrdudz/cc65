/*****************************************************************************/
/*                                                                           */
/*                                  input.c                                  */
/*                                                                           */
/*   Input format/file definitions for the sp65 sprite and bitmap utility    */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 2012,      Ullrich von Bassewitz                                      */
/*                Roemerstrasse 52                                           */
/*                D-70794 Filderstadt                                        */
/* EMail:         uz@cc65.org                                                */
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



#include <stdlib.h>

/* common */
#include "fileid.h"

/* sp65 */
#include "error.h"
#include "input.h"
#include "pcx.h"



/*****************************************************************************/
/*                                   Data                                    */
/*****************************************************************************/



typedef struct InputFormatDesc InputFormatDesc;
struct InputFormatDesc {

    /* Read routine */
    Bitmap* (*Read) (const char* Name);

};

/* Table with input formats */
static InputFormatDesc InputFormatTable[ifCount] = {
    {   ReadPCXFile     },
};

/* Table that maps extensions to input formats. Must be sorted alphabetically */
static const FileId FormatTable[] = {
    /* Upper case stuff for obsolete operating systems */
    {   "PCX",  ifPCX           },

    {   "pcx",  ifPCX           },
};



/*****************************************************************************/
/*                                   Code                                    */
/*****************************************************************************/



int FindInputFormat (const char* Name)
/* Find an input format by name. The function returns a value less than zero
 * if Name is not a known input format.
 */
{
    /* Search for the entry in the table. */
    const FileId* F = bsearch (Name,
                               FormatTable,
                               sizeof (FormatTable) / sizeof (FormatTable[0]),
                               sizeof (FormatTable[0]),
                               CompareFileId);

    /* Return the id or an error code */
    return (F == 0)? -1 : F->Id;
}



Bitmap* ReadInputFile (const char* Name, InputFormat Format)
/* Read a bitmap from a file and return it. If Format is ifAuto, the routine
 * tries to determine the format from the file name extension.
 */
{
    /* If the format is Auto, try to determine it from the file name */
    if (Format == ifAuto) {
        /* Search for the entry in the table */
        const FileId* F = GetFileId (Name, FormatTable,
                                     sizeof (FormatTable) / sizeof (FormatTable[0]));
        /* Found? */
        if (F == 0) {
            Error ("Cannot determine file format of input file `%s'", Name);
        }
        Format = F->Id;
    }

    /* Check the format just for safety */
    CHECK (Format >= 0 && Format < ifCount);

    /* Call the format specific read */
    return InputFormatTable[Format].Read (Name);
}


