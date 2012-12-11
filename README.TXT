 ***************************************************************************

   Canon SELPHY ES/CP series print assister 

   (c) 2007-2012 Solomon Peachy <pizza@shaftnet.org>

   The latest version of this program can be found at
  
   http://git.shaftnet.org/git/gitweb.cgi?p=selphy_print.git

 ***************************************************************************
 
   The SELPHY ES/CP series of dye-sublimation photo printers from Canon 
   generally require intelligent buffering of the raw print data in order 
   to keep the printer from locking up.  This tool accomplishes just that.

   Verified supported printers:
  
     ES1, ES2, ES3, ES30, ES40, C-100, CP-200, CP-300, CP-330, CP400, 
     CP500, CP510, CP710, CP720, CP730, CP740, CP750, CP760, CP770, CP780, 
     CP800, CP900

   Unverified/untested, but should work:

     ES20, CP-10, CP-220, CP600, CP810

   NOT currently supported by libusb backend:  (USB PIDs unknown)  **
	
     CP520, CP530, CP790

    ** Note that a PID can be supplied on the command line, see below.
       Once the PIDs are known, these printers are expected to work.

 ***************************************************************************
 Compilation:

  You will need libusb-1.0 to build this tool.

   Fedora/RedHat:
	yum install libusb1-devel
   Debian/Ubuntu
	apt-get install libusb-1.0-0-dev

  And all you need to do after that is type 'make'

 ***************************************************************************
 CUPS backend usage:

	cp selphy_print /usr/lib/cups/backend/selphy
	chown root:root /usr/lib/cups/backend/selphy
	chmod 700 /usr/lib/cups/backend/selphy

   You may need to restart CUPS, and you will also need gutenprint 5.2.10
   or newer installed.  (This backend is bundled with gutenprint starting with
   v5.2.10)

   Without the rest of gutenprint installed, this is useless as a CUPS backend.

 ***************************************************************************
 Standalone usage:

  	selphy_print filename
	selphy_print - < filename
	somecommand | selphy_print -

   To test out an unknown USB model, specify the PID and its type as 
   environment variables.  For example:

	SELPHY_PID=3ab1 SELPHY_TYPE=3 selphy_print filename

	SELPHY_PID is the USB PID, in hex
	SELPHY_TYPE is: 3 for CP790
			4 for CP520/CP530 (or other CP-series model)

   If you have a model the spooler doesn't recognize but it works with
   a PID/TYPE specification, please let me know via e-mail.

 ***************************************************************************

	Stream formats and readback codes for supported printers

 ***************************************************************************

 Selphy ES1:

   Init func:   40 00 [typeA] [pgcode]  00 00 00 00  00 00 00 00
   Plane func:  40 01 [typeB] [plane]  [length, 32-bit LE]  00 00 00 00 

   TypeA codes are 0x10 for Color papers, 0x20 for B&W papers.
   TypeB codes are 0x01 for Color papers, 0x02 for B&W papers.

   Plane codes are 0x01, 0x03, 0x07 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x11 and a plane length of 2227456 bytes
   'L'        pgcode of 0x12 and a plane length of 1601600 bytes.
   'C'        pgcode of 0x13 and a plane length of  698880 bytes.

   Readback values seen:

   02 00 00 00  02 01 [pg] 01  00 00 00 00   [idle, waiting for init seq]
   04 00 00 00  02 01 [pg] 01  00 00 00 00   [init received, not ready..]
   04 00 01 00  02 01 [pg] 01  00 00 00 00   [waiting for Y data]
   04 00 03 00  02 01 [pg] 01  00 00 00 00   [waiting for M data]
   04 00 07 00  02 01 [pg] 01  00 00 00 00   [waiting for C data]
   04 00 00 00  02 01 [pg] 01  00 00 00 00   [all data sent; not ready..]
   05 00 00 00  02 01 [pg] 01  00 00 00 00   [?? transitions to this]
   06 00 00 00  02 01 [pg] 01  00 00 00 00   [?? transitions to this]
   02 00 00 00  02 01 [pg] 01  00 00 00 00   [..transitions back to idle]

   02 01 00 00  01 ff ff ff  00 80 00 00     [error, no media]
   02 01 00 00  01 ff ff ff  00 00 00 00     [error, cover open]

   Known paper types for all ES printers:  P, Pbw, L, C, Cl
   Additional types for ES3/30/40:         Pg, Ps

   [pg] is:  0x01 for P-papers (only 'P' tested)
   	     0x02 for L-paper  (guessed)
             0x03 for C-papers (only 'Cl' tested)

 ***************************************************************************
 Selphy ES2/20:

   Init func:   40 00 [pgcode] 00  02 00 00 [type]  00 00 00 [pg2] [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes
   'L' 	      pgcode of 0x02 and a plane length of 1601600 bytes.
   'C'	      pgcode of 0x03 and a plane length of  698880 bytes.

   pg2 is 0x00 for all media types except for 'C', which is 0x01.

   Readback values seen on an ES2:

   02 00 00 00  [pg] 00 [pg2] [xx]  00 00 00 00   [idle, waiting for init seq]
   03 00 01 00  [pg] 00 [pg2] [xx]  00 00 00 00   [init complete, ready for Y]
   04 00 01 00  [pg] 00 [pg2] [xx]  00 00 00 00   [? paper loaded]
   05 00 01 00  [pg] 00 [pg2] [xx]  00 00 00 00   [? transitions to this]
   06 00 03 00  [pg] 00 [pg2] [xx]  00 00 00 00   [ready for M]
   08 00 03 00  [pg] 00 [pg2] [xx]  00 00 00 00   [? transitions to this]
   09 00 07 00  [pg] 00 [pg2] [xx]  00 00 00 00   [ready for C]
   09 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   0b 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transisions to this]
   0c 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   0f 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]
   13 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [? transitions to this]

   14 00 00 00  [pg] 00 [pg2] 00  00 00 00 00   [out of paper/ink]
   14 00 01 00  [pg] 00 [pg2] 00  01 00 00 00   [out of paper/ink]

   16 01 00 00  [pg] 00 [pg2] 00  00 00 00 00   [error, cover open]
   02 00 00 00  05 05 02 00  00 00 00 00        [error, no media]

   [xx] can be 0x00 or 0xff, depending on if a previous print job has 
	completed or not.

   [pg] is:  0x01 for P-paper  (only 'P' tested)
   	     0x02 for L-paper  (guessed)
             0x03 for C-paper  (only 'Cl' tested)

   [pg2] is: 0x00 for P-paper  (only 'P' tested)
   	     ???? for L-paper  (unknown)
             0x01 for C-paper  (only 'Cl' tested)

   [pg2] may refer to Label (0x01) vs non-Label (0x00) media.

 ***************************************************************************
 Selphy ES3/30:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes.
   'L' 	      pgcode of 0x02 and a plane length of 1601600 bytes.
   'C' 	      pgcode of 0x03 and a plane length of  698880 bytes.

   Readback values seen on an ES3 & ES30:

   00 ff 00 00  ff ff ff ff  00 00 00 00   [idle, waiting for init seq]
   01 ff 01 00  ff ff ff ff  00 00 00 00   [init complete, ready for Y]
   03 ff 01 00  ff ff ff ff  00 00 00 00   [?]
   03 ff 02 00  ff ff ff ff  00 00 00 00   [ready for M]
   05 ff 02 00  ff ff ff ff  00 00 00 00   [?]
   05 ff 03 00  ff ff ff ff  00 00 00 00   [ready for C]
   07 ff 03 00  ff ff ff ff  00 00 00 00   [?]
   0b ff 03 00  ff ff ff ff  00 00 00 00   [?]
   13 ff 03 00  ff ff ff ff  00 00 00 00   [?]
   00 ff 10 00  ff ff ff ff  00 00 00 00   [ready for footer]

   00 ff 00 00  ff ff ff ff  00 00 00 00   [cover open, no media]

   00 ff 01 00  ff ff ff ff  03 00 02 00   [attempt to print with no media]
   00 ff 01 00  ff ff ff ff  08 00 04 00   [attempt to print with cover open]

   There appears to be no paper code in the readback; codes were identical for
   the standard 'P-Color' and 'Cl' cartridges:

 ***************************************************************************
 Selphy ES40:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'L' 	      pgcode of 0x01 and a plane length of 1601600 bytes.
   'C'	      pgcode of 0x02 and a plane length of  698880 bytes.

   Readback values seen on an ES40:

   00 00 ff 00  00 00 00 00  00 00 00 [pg]
   00 00 00 00  00 00 00 00  00 00 00 [pg]   [idle, ready for header]
   00 01 01 00  00 00 00 00  00 00 00 [pg]   [ready for Y data]
   00 03 01 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 03 02 00  00 00 00 00  00 00 00 [pg]   [ready for M data]
   00 05 02 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 05 03 00  00 00 00 00  00 00 00 [pg]   [ready for C data]
   00 07 03 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 0b ff 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 0e ff 00  00 00 00 00  00 00 00 [pg]   [transitions to this]
   00 00 10 00  00 00 00 00  00 00 00 [pg]   [ready for footer]

   00 ** ** [xx]  00 00 00 00  00 00 00 [pg] [error]

   [xx]:
	01:  Generic communication error
	32:  Cover open / media empty

   [pg] is as follows:

      'P' paper 0x11
      'L' paper 0x22
      'C' paper 0x33
      'W' paper 0x44


 ***************************************************************************
 Selphy CP790:

   Init func:   40 00 [pgcode] 00  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'L' 	      pgcode of 0x01 and a plane length of 1601600 bytes.
   'C'	      pgcode of 0x02 and a plane length of  698880 bytes.
   'W' 	      pgcode of 0x03 and a plane length of 2976512 bytes.

   Readback codes are completely unknown, but are likely to be the same
   as the ES40.

 ***************************************************************************
 Selphy CP-series (except for CP790):
 
    This is known to apply to:
	CP-10, CP-100, CP-200, CP-300, CP-330, CP400, CP500, CP510, CP710,
	CP720, CP730, CP740, CP750, CP760, CP770, CP780, CP800, CP900

   Init func:   40 00 00 [pgcode]  00 00 00 00  00 00 00 00
   Plane func:  40 01 00 [plane]  [length, 32-bit LE]  00 00 00 00 
   End func:    00 00 00 00      # NOTE:  CP900 only, and not necessary!

   plane codes are 0x00, 0x01, 0x02 for Y, M, and C, respectively.

   'P' papers pgcode 0x01   plane length 2227456 bytes.
   'L' 	      pgcode 0x02   plane length 1601600 bytes.
   'C' 	      pgcode 0x03   plane length  698880 bytes.
   'W'	      pgcode 0x04   plane length 2976512 bytes.

   Known readback values:

   01 00 00 00  [ss] 00 [pg] 00  00 00 00 [xx]   [idle, waiting for init]
   02 00 [rr] 00  00 00 [pg] 00  00 00 00 [xx]   [init sent, paper feeding]
   02 00 [rr] 00  10 00 [pg] 00  00 00 00 [xx]   [init sent, paper feeding] 
   02 00 [rr] 00  70 00 [pg] 00  00 00 00 [xx]   [waiting for Y data]
   04 00 00 00  00 00 [pg] 00  00 00 00 [xx]   [waiting for M data]
   08 00 00 00  00 00 [pg] 00  00 00 00 [xx]   [waiting for C data]
   10 00 00 00  00 00 [pg] 00  00 00 00 [xx]   [C done, waiting]
   20 00 00 00  00 00 [pg] 00  00 00 00 [xx]   [All done]

   [xx] is 0x01 on the CP780/CP800/CP900, 0x00 on all others.

   [rr] is error code:
   	0x00 no error
	0x01 paper out
	0x08 when ribbon is depleted

   [ss] is either 0x00 or 0x70.  Unsure as to its significance; perhaps it
	means paper or ribbon is already set to go?

   [pg] is as follows:

      'P' paper 0x11
      'L' paper 0x22
      'C' paper 0x33  (0x00 on the CP-10)
      'W' paper 0x44

      First four bits are paper, second four bits are the ribbon.  They aren't
      necessarily identical.  So it's possible to have a code of, say,
      0x41 if the 'Wide' paper tray is loaded with a 'P' ribbon. A '0' is used
      to signify nothing being loaded.
 
