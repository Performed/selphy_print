 ***************************************************************************

   Canon SELPHY ES series print assister 

   (c) 2007-2012 Solomon Peachy <pizza@shaftnet.org>

   The latest version of this program can be found at
  
   http://git.shaftnet.org/git/gitweb.cgi?p=selphy_print.git

 ***************************************************************************
 
   The SELPHY ES-series printers from Canon requires intelligent buffering
   of the raw print data in order to keep the printer from locking up.  

   Verified supported printers:
  
     SELPHY ES1, ES2, ES30 

   Unverified/untested, but should work:

     SELPHY ES20, ES3
     SELPHY CP10, CP100, CP200, CP220, CP300, CP330, CP400, CP500, CP510,
            CP520, CP530, CP600, CP710, CP720, CP730, CP740, CP750, CP760,
	    CP770, CP780
 
   NOT currently supported:

     SELPHY ES40, CP790
        I need the readback codes for these printers in order to proceed.
     SELPHY CP810, CP900
        Currently unknown stream format (and possibly unknown readback codes)

 ***************************************************************************
 Selphy ES1:

   Init func:   40 00 [typeA] [pgcode] 00 00 00 00 00 00 00 00
   Plane func:  40 01 [typeB] [plane] [length, 32-bit LE] 00 00 00 00 

   TypeA codes are 0x10 for Color papers, 0x20 for B&W papers.
   TypeB codes are 0x01 for Color papers, 0x02 for B&W papers.

   Plane codes are 0x01, 0x03, 0x07 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x11 and a plane length of 2227456 bytes
   'CP_L'     pgcode of 0x12 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x13 and a plane length of  698880 bytes.

   Readback values seen:

   02 00 00 00 02 01 [pg] 01 00 00 00 00   [idle, waiting for init seq]
   04 00 00 00 02 01 [pg] 01 00 00 00 00   [init received, not ready..]
   04 00 01 00 02 01 [pg] 01 00 00 00 00   [waiting for Y data]
   04 00 03 00 02 01 [pg] 01 00 00 00 00   [waiting for M data]
   04 00 07 00 02 01 [pg] 01 00 00 00 00   [waiting for C data]
   04 00 00 00 02 01 [pg] 01 00 00 00 00   [all data sent; not ready..]
   05 00 00 00 02 01 [pg] 01 00 00 00 00   [?? transitions to this]
   06 00 00 00 02 01 [pg] 01 00 00 00 00   [?? transitions to this]
   02 00 00 00 02 01 [pg] 01 00 00 00 00   [..transitions back to idle]

   Known paper types for all ES printers:  P, Pbw, C, Cl
   Additional types for ES3/30/40:         Pg, Ps

   Known pg codes:   0x01   -- P-size
                     0x03   -- C-size

   Readbacks for other paper types are currently unknown.

   P*   sizes are 100x148mm "postcards"
   CP_L sizes are 89x119mm "labels"
   Card sizes are 54x86mm "cards"

 ***************************************************************************
 Selphy ES2/20:

   Init func:   40 00 [pgcode] 00  02 00 00 [type]  00 00 00 [pg2] [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes
   'CP_L'     pgcode of 0x02 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x03 and a plane length of  698880 bytes.

   pg2 is 0x00 for all media types except for 'Card', which is 0x01.

   Readback values seen:

   02 00 00 00 [pg] 00 [pg2] [xx] 00 00 00 00   [idle, waiting for init seq]
   03 00 01 00 [pg] 00 [pg2] [xx] 00 00 00 00   [init complete, ready for Y]
   04 00 01 00 [pg] 00 [pg2] [xx] 00 00 00 00   [? paper loaded]
   05 00 01 00 [pg] 00 [pg2] [xx] 00 00 00 00   [? transitions to this]
   06 00 03 00 [pg] 00 [pg2] [xx] 00 00 00 00   [ready for M]
   08 00 03 00 [pg] 00 [pg2] [xx] 00 00 00 00   [? transitions to this]
   09 00 07 00 [pg] 00 [pg2] [xx] 00 00 00 00   [ready for C]
   09 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   0b 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transisions to this]
   0c 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   0f 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   13 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   02 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [back to idle, waiting for init seq]

   [xx] can be 0x00 or 0xff, depending on if a print job has completed or not.

   14 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [out of paper/ink]
   14 00 01 00 [pg] 00 [pg2] 00 01 00 00 00   [out of paper/ink]

   [pg] is:  0x01 for P-paper
             0x03 for C-paper

   [pg2] is: 0x00 for P-paper
             0x01 for C-paper (label)

 ***************************************************************************
 Selphy ES3/30:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes.
   'CP_L'     pgcode of 0x02 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x03 and a plane length of  698880 bytes.

   Readback values seen with standard 'P-Color' and 'C-Label' cartridges:

   00 ff 00 00 ff ff ff ff 00 00 00 00   [idle, waiting for init seq]
   01 ff 01 00 ff ff ff ff 00 00 00 00   [init complete, ready for Y]
   03 ff 01 00 ff ff ff ff 00 00 00 00   [?]
   03 ff 02 00 ff ff ff ff 00 00 00 00   [ready for M]
   05 ff 02 00 ff ff ff ff 00 00 00 00   [?]
   05 ff 03 00 ff ff ff ff 00 00 00 00   [ready for C]
   07 ff 03 00 ff ff ff ff 00 00 00 00   [?]
   0b ff 03 00 ff ff ff ff 00 00 00 00   [?]
   13 ff 03 00 ff ff ff ff 00 00 00 00   [?]
   00 ff 10 00 ff ff ff ff 00 00 00 00   [done, ready for footer]

 ***************************************************************************
 Selphy ES40:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'CP_L'     pgcode of 0x01 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x02 and a plane length of  698880 bytes.

   Readback codes are unknown.

 ***************************************************************************
 Selphy CP790:

   Init func:   40 00 [pgcode] 00  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'CP_L'     pgcode of 0x01 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x02 and a plane length of  698880 bytes.
   'Wide'     pgcode of 0x03 and a plane length of 2976512 bytes.

   Readback codes are unknown.

 ***************************************************************************
 Selphy CP-series (except for CP790, CP810, CP900):
 
   Init func:   40 00 00 [pgcode] 00 00 00 00 00 00 00 00
   Plane func:  40 01 00 [plane] [length, 32-bit LE] 00 00 00 00 

   plane codes are 0x00, 0x01, 0x02 for Y, M, and C, respectively.

   'P' paper     pgcode 0x01   plane length 2227456 bytes.
   'CP_L'        pgcode 0x02   plane length 1601600 bytes.
   'Card' paper  pgcode 0x03   plane length  698880 bytes.
   'Wide' paper  pgcode 0x04   plane length 2976512 bytes.

   Known readback values (From a SELPHY CP760)

   01 00 00 00 00 00 [pg] 00 00 00 00 00   [idle, waiting for init]
   02 00 00 00 70 00 [pg] 00 00 00 00 00   [waiting for Y data]
   04 00 00 00 00 00 [pg] 00 00 00 00 00   [waiting for M data]
   08 00 00 00 00 00 [pg] 00 00 00 00 00   [waiting for C data]
   10 00 00 00 00 00 [pg] 00 00 00 00 00   [?]
   20 00 00 00 00 00 [pg] 00 00 00 00 00   [?]

   'P'    paper has a code of 0x11
   'CP_L' paper has a code of 0x22

   Readback codes for other paper types are unknown.
