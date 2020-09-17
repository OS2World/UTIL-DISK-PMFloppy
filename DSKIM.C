/*
           dskim.c
           
           disk handling routines for pmfloppy.c

           Copyright G. Bryant, 1990

   
  Change Log

*/

#define INCL_DOSERRORS
#define INCL_DOSPROCESS
#define INCL_BASE
#define INCL_DOSDEVIOCTL
#define INCL_DOSSESMGR
#define INCL_DOSMISC
#include <os2.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "pmfloppy.h"
#include "DskIm.h"

/* Global variables ------------------------------------------------------ */

// PM vbls
extern HWND   hWndFrame ;

// user response vbls
extern DskImage ImageBuffers[NUM_IMAGES];

// Thread variables
extern ThreadContext tcThBufs[NUM_THREADS];

// "local" global vbls
BYTE         _lockCmd;            /* Used with [un]lockdrive macros     */
ULONG        _fmtData;            /* Used with DSK_FORMATVERIFY         */

// Global function prototypes
VOID FAR readsource(USHORT);
VOID FAR writetarget(USHORT);
VOID FAR LoadImage(USHORT);
VOID FAR SaveImage(USHORT);
VOID FAR CompImages(USHORT);

// local function prototypes
SHORT    fmttbl_bytessec(USHORT, USHORT *);
SHORT    bspblkcmp(BSPBLK *, BSPBLK *);
VOID     ThreadErrorHandler(USHORT, USHORT, HFILE);
USHORT   SetBufferSel(USHORT);
PBYTE    MakeHugeP(USHORT, USHORT);




/* Code ------------------------------------------------------------------ */


//   Read source disk into memory
//
//  when done track data is in huge space starting with selector
//  stored in the global
//  sets global variables:
//     tcThBufs
//     ImageBuffers
//
VOID FAR readsource(USHORT curTh) {

BYTE         parmCmd = 1;
USHORT       trk, hd, cyl;
HFILE        dHandle;
USHORT       result;
static CHAR  szdrive[] = "A:";
ULONG        sourceBytes;         /* # bytes on source disk             */
USHORT       sourceTracks;        /* # tracks on source disk            */
USHORT       curBuff;

PBYTE        DskBuf;              // pointer to track data

  curBuff = tcThBufs[curTh].ImageNumber;

  tcThBufs[curTh].ErrorCode = 0;
  /* If this isn't the first time here, free memory from last time first */
  if (ImageBuffers[curBuff].Percent == 100) {
    DosFreeSeg(ImageBuffers[curBuff].DskSel);
    free(ImageBuffers[curBuff].DskLayout);
  }

  /* Get source disk parameters */
  DosError(HARDERROR_DISABLE);
  szdrive[0] = ImageBuffers[curBuff].DriveID[0];
  tcThBufs[curTh].ErrorCode = DosOpen(szdrive,
                                      &dHandle,
                                      &result,
                                      0L,
                                      0,
                                      FILE_OPEN,
                                      OPENFLAGS,
                                      0L);

  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, dHandle);

  lockdrive(dHandle);
  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, dHandle);

  tcThBufs[curTh].ErrorCode = DosDevIOCtl(&ImageBuffers[curBuff].DskParms,
                                          &parmCmd,
                                          DSK_GETDEVICEPARAMS,
                                          IOCTL_DISK,
                                          dHandle);

  if (!tcThBufs[curTh].ErrorCode) {
    /* Set all the informational variables and build a track layout table
    **  for use with the following sector reads.
    */
    sourceBytes   = (ULONG)(ImageBuffers[curBuff].DskParms.usBytesPerSector) *
                    (ULONG)(ImageBuffers[curBuff].DskParms.cSectors);
    sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                    ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

    ImageBuffers[curBuff].usLayoutSize = sizeof(TRACKLAYOUT)   +
                          ((2 * sizeof(USHORT)) *
                          (ImageBuffers[curBuff].DskParms.usSectorsPerTrack - 1));

    if (ImageBuffers[curBuff].DskLayout =
        (PTRACKLAYOUT)malloc(ImageBuffers[curBuff].usLayoutSize * sizeof(BYTE))) {
      ImageBuffers[curBuff].DskLayout->bCommand = 1;
      ImageBuffers[curBuff].DskLayout->usFirstSector = 0;
      ImageBuffers[curBuff].DskLayout->cSectors = ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
      for (trk = 0; trk < ImageBuffers[curBuff].DskParms.usSectorsPerTrack; trk++) {
        ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorNumber = trk+1;
        ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorSize = 
                                                                      ImageBuffers[curBuff].DskParms.usBytesPerSector;
      }
    }
    else
      ThreadErrorHandler(UM_ERROR, curTh, dHandle);



    // Allocate huge memory to hold the track data
    if (tcThBufs[curTh].ErrorCode = SetBufferSel(curBuff))
      ThreadErrorHandler(UM_ERROR, curTh, dHandle);

    // For each track, set the pointer and read the sector into it
    for (trk = 0, cyl = 0;
         trk < sourceTracks;
         trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
      ImageBuffers[curBuff].DskLayout->usCylinder = cyl;

      for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {

        ImageBuffers[curBuff].Percent =
          (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
        WinPostMsg(hWndFrame,UM_STATUS,MPFROMSHORT(curTh),0);

        ImageBuffers[curBuff].DskLayout->usHead = hd;

        DskBuf = MakeHugeP(curBuff,trk+hd);
        if (tcThBufs[curTh].ErrorCode = DosDevIOCtl(DskBuf,
                                    ImageBuffers[curBuff].DskLayout,
                                    DSK_READTRACK,
                                    IOCTL_DISK,
                                    dHandle))
          ThreadErrorHandler(UM_ERROR, curTh, dHandle);
      }
    }
    ImageBuffers[curBuff].Percent = 100;
    WinPostMsg(hWndFrame,UM_DONE,MPFROMSHORT(curTh),0);
  }
  else {
    WinPostMsg(hWndFrame,UM_ERROR,MPFROMSHORT(curTh),0);
  }
  if (dHandle) DosClose(dHandle);
  unlockdrive(dHandle);
  DosError(HARDERROR_ENABLE);
  DosExit(EXIT_THREAD,0);
}  // readsource


/* --- Translate bytes per sector into 0-3 code ---
**       the four sector sizes listed below are alluded to in the OS/2
**        docs however only 512 byte sectors are allowed under OS/2 1.x
**       returns the code or -1 and sets DiskError
*/
SHORT fmttbl_bytessec(USHORT bytesPerSec, USHORT *DiskError)
{

  *DiskError = NO_ERROR;
  switch (bytesPerSec)  {
    case 128:  return 0;
    case 256:  return 1;
    case 512:  return 2;
    case 1024: return 3;
  }
  *DiskError = ERROR_BAD_FORMAT;
  return -1;
}



/* --- write information read by readsource() onto target disk ---
**       parameter is drive handle as returned by opendrive()
**       checks the target disk, if it's the same format as the source
**        or not formatted at all, write the information contained in
**        DskBuffer formatting if neccessary.
**       returns 0 if successful else errorcode
**
*/
VOID FAR writetarget(USHORT curTh) {

BYTE         _parmCmd = 1;
PTRACKFORMAT trkfmt;
USHORT       sizeofTrkfmt;
USHORT       i, trk, hd, cyl, needFormat = FALSE;
HFILE        hf;
USHORT       result;
static CHAR  szdrive[] = "A:";
BSPBLK       targetParms;
USHORT       sourceTracks;        /* # tracks on source disk            */
USHORT       curBuff;

PBYTE        DskBuf;              // huge pointer to track data

  curBuff = tcThBufs[curTh].ImageNumber;

  tcThBufs[curTh].ErrorCode = 0;
  /* Get source disk parameters */
  DosError(HARDERROR_DISABLE);
  szdrive[0] = ImageBuffers[curBuff].DriveID[0];
  tcThBufs[curTh].ErrorCode = DosOpen(szdrive,
                                      &hf,
                                      &result,
                                      0L,
                                      0,
                                      FILE_OPEN,
                                      OPENFLAGS,
                                      0L);

  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  lockdrive(hf);
  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  /* Get target disk parameters */
  tcThBufs[curTh].ErrorCode = DosDevIOCtl(&targetParms,
                                          &_parmCmd,
                                          DSK_GETDEVICEPARAMS,
                                          IOCTL_DISK,
                                          hf);

  if ((tcThBufs[curTh].ErrorCode == ERROR_READ_FAULT) &&
      (ImageBuffers[curBuff].FormatOptions == IDD_WRF_NEVER))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  if (((tcThBufs[curTh].ErrorCode == ERROR_READ_FAULT) &&
       (ImageBuffers[curBuff].FormatOptions == IDD_WRF_MAYBE)) ||
      (ImageBuffers[curBuff].FormatOptions == IDD_WRF_ALWAYS))  {
    /* If the disk needs formatting we build a format table for it based
    **  on the source disk.
    */
    needFormat = TRUE;
    tcThBufs[curTh].ErrorCode = 0;
    // Set all the informational variables needed for formatting

    sizeofTrkfmt = sizeof(TRACKFORMAT) +
                    ((4 * sizeof(BYTE)) *
                    (ImageBuffers[curBuff].DskParms.usSectorsPerTrack - 1));
    if ((trkfmt = (PTRACKFORMAT)malloc(sizeofTrkfmt * sizeof(BYTE))) == NULL)
      ThreadErrorHandler(UM_ERROR, curTh, hf);

    trkfmt->bCommand = 1;
    trkfmt->cSectors = ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
    for (trk = 0; trk < trkfmt->cSectors; trk++) {
      trkfmt->FormatTable[trk].idSector = (BYTE)(trk+1);
      trkfmt->FormatTable[trk].bBytesSector =
             fmttbl_bytessec(ImageBuffers[curBuff].DskParms.usBytesPerSector,
                             &(tcThBufs[curTh].ErrorCode));
    }
  }
  else if (!tcThBufs[curTh].ErrorCode)
    /* Else if no other error, make sure that the target disk is the same
    **  format as the source.
    */
    if (bspblkcmp(&(ImageBuffers[curBuff].DskParms), &targetParms))
      tcThBufs[curTh].ErrorCode = DSKIM_ERROR_WRONG_FORMAT;

  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  if (!tcThBufs[curTh].ErrorCode) {
    for (trk = 0, cyl = 0; trk < sourceTracks; trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
      ImageBuffers[curBuff].DskLayout->usCylinder = cyl;
      for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
        ImageBuffers[curBuff].Percent =
          (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
        WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);
        ImageBuffers[curBuff].DskLayout->usHead = hd;
        if (needFormat)  {
          trkfmt->usHead = hd;
          trkfmt->usCylinder = cyl;
          for (i = 0; i < trkfmt->cSectors; i++) {
            trkfmt->FormatTable[i].bHead = (BYTE)hd;
            trkfmt->FormatTable[i].bCylinder = (BYTE)cyl;
          }

          if (tcThBufs[curTh].ErrorCode = DosDevIOCtl(&_fmtData,
                                                      trkfmt,
                                                      DSK_FORMATVERIFY,
                                                      IOCTL_DISK,
                                                      hf))
            ThreadErrorHandler(UM_ERROR, curTh, hf);
        }
        DskBuf = MakeHugeP(curBuff,trk+hd);
        if (tcThBufs[curTh].ErrorCode = DosDevIOCtl(DskBuf,
                                    ImageBuffers[curBuff].DskLayout,
                                    DSK_WRITETRACK,
                                    IOCTL_DISK,
                                    hf))
          ThreadErrorHandler(UM_ERROR, curTh, hf);
      }
    }
    ImageBuffers[curBuff].Percent = 100;
    if (needFormat) free(trkfmt);
    WinPostMsg(hWndFrame,UM_DONE,MPFROMSHORT(curTh),0);
  }
  else {
    WinPostMsg(hWndFrame,UM_ERROR,MPFROMSHORT(curTh),0);
  }
  if (hf) DosClose(hf);
  unlockdrive(hf);
  DosError(HARDERROR_ENABLE);
  DosExit(EXIT_THREAD,0);
} //writetarget


  /* --- compare two BSPBLK structures ---
  **       returns 0 if both are the same except for possibly the
  **        abReserved field, else returns non-zero.
  */
SHORT bspblkcmp(BSPBLK *blk1, BSPBLK *blk2)  {

BSPBLK tmp1, tmp2;

  tmp1 = *blk1;
  tmp2 = *blk2;
  memset(tmp1.abReserved, 0, 6);
  memset(tmp2.abReserved, 0, 6);
  return memcmp(&tmp1, &tmp2, sizeof(BSPBLK));
}



VOID ThreadErrorHandler(USHORT Msg, USHORT curTh, HFILE dHandle) {

  WinPostMsg(hWndFrame,Msg,MPFROMSHORT(curTh),0);

  if (dHandle) DosClose(dHandle);
  unlockdrive(dHandle);
  DosError(HARDERROR_ENABLE);
  DosExit(EXIT_THREAD, 0);
}


// Set the selectors for the buffers to hold to disk data
//
// returns errnum if an error occurred, else 0
//
// sets DskSel & SelOff in ImageBuffers[curBuff]
//
USHORT SetBufferSel(USHORT curBuff) {

USHORT TpSeg;     // Tracks per Segment
USHORT RemT;      // Remaining tracks
USHORT tSeg;      // total segments
USHORT srcT;      // # tracks on disk
USHORT bpT;       // Bytes per track
USHORT sCnt;      // shift count
USHORT rc;        // return code from AllocHuge

  srcT  = ImageBuffers[curBuff].DskParms.cSectors         /
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  TpSeg = (USHORT) (MAX_SEG / bpT);
  tSeg  = srcT / TpSeg;
  RemT  = srcT % TpSeg;
  if (rc = DosAllocHuge(tSeg,
                        RemT*bpT,
                        &(ImageBuffers[curBuff].DskSel),
                        0,
                        SEG_NONSHARED))
    return(rc);
  DosGetHugeShift(&sCnt);
  ImageBuffers[curBuff].SelOff = 1 << sCnt;
  return(FALSE);
}

PBYTE MakeHugeP(USHORT curBuff, USHORT Trk) {

USHORT TpSeg;     // Tracks per Segment
USHORT Offs;      // offset into segment
USHORT nSel;      // number of selector
USHORT TSel;      // Track selector
USHORT bpT;       // Bytes per track

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  TpSeg = (USHORT) (MAX_SEG / bpT);
  nSel  = Trk / TpSeg;
  Offs  = (Trk % TpSeg) * bpT;

  TSel = ImageBuffers[curBuff].DskSel+(nSel * ImageBuffers[curBuff].SelOff);
  return(MAKEP(TSel, Offs));

}


// Load the disk image.
//
//  Note that although it is not used here, we will create the layout
//  table as it is needed by writetarget
//
VOID FAR LoadImage(USHORT curTh) {

USHORT curBuff;
USHORT bpT;            // Bytes per track
USHORT sourceTracks;   // # tracks on source disk
ULONG  sourceBytes;    // # bytes on source disk
USHORT trk;
USHORT hd;
USHORT cyl;
HFILE  hf;
USHORT result;
CHAR   Header[] = "DskImage";
CHAR   NewHead[9];     // must be size of header
PBYTE  DskBuf;         // huge pointer to track data

  curBuff = tcThBufs[curTh].ImageNumber;
  tcThBufs[curTh].ErrorCode = 0;

// If this isn't the first time here, free memory from last time first 
  if (ImageBuffers[curBuff].Percent == 100) {
    DosFreeSeg(ImageBuffers[curBuff].DskSel);
    free(ImageBuffers[curBuff].DskLayout);
  }

  if (tcThBufs[curTh].ErrorCode = DosOpen2(ImageBuffers[curBuff].FileName,
                                       &hf,
                                       &result,
                                       0L,
                                       FILE_NORMAL,
                                       FILE_OPEN,
                                       OPEN_ACCESS_READONLY | OPEN_SHARE_DENYWRITE,
                                       NULL,
                                       0L))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

// read dskim header
  if (tcThBufs[curTh].ErrorCode = DosRead(hf,&NewHead,sizeof NewHead,&result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  if (strcmp(Header,NewHead)) {
    tcThBufs[curTh].ErrorCode = DSKIM_ERROR_WRONG_FILE;
    ThreadErrorHandler(UM_ERROR, curTh, hf);
  }

// read parameter block
  if (tcThBufs[curTh].ErrorCode = DosRead(hf,
                                          &ImageBuffers[curBuff].DskParms,
                                          sizeof ImageBuffers[curBuff].DskParms,
                                          &result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);


  sourceBytes   = (ULONG)(ImageBuffers[curBuff].DskParms.usBytesPerSector) *
                  (ULONG)(ImageBuffers[curBuff].DskParms.cSectors);
  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  ImageBuffers[curBuff].usLayoutSize = sizeof(TRACKLAYOUT)   +
                        ((2 * sizeof(USHORT)) *
                        (ImageBuffers[curBuff].DskParms.usSectorsPerTrack - 1));

  if (ImageBuffers[curBuff].DskLayout =
      (PTRACKLAYOUT)malloc(ImageBuffers[curBuff].usLayoutSize * sizeof(BYTE))) {
    ImageBuffers[curBuff].DskLayout->bCommand = 1;
    ImageBuffers[curBuff].DskLayout->usFirstSector = 0;
    ImageBuffers[curBuff].DskLayout->cSectors = ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
    for (trk = 0; trk < ImageBuffers[curBuff].DskParms.usSectorsPerTrack; trk++) {
      ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorNumber = trk+1;
      ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorSize =
                                                                    ImageBuffers[curBuff].DskParms.usBytesPerSector;
    }
  }
  else
    ThreadErrorHandler(UM_ERROR, curTh, hf);



  // Allocate huge memory to hold the track data
  if (tcThBufs[curTh].ErrorCode = SetBufferSel(curBuff))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

// read disk data
  for (trk = 0, cyl = 0;
       trk < sourceTracks;
       trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
    for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
      ImageBuffers[curBuff].Percent =
        (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
      WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);

      DskBuf = MakeHugeP(curBuff,trk+hd);

      if (tcThBufs[curTh].ErrorCode = DosRead(hf,DskBuf,bpT,&result))
        ThreadErrorHandler(UM_ERROR, curTh, hf);
    }
  }
  ImageBuffers[curBuff].Percent = 100;
  WinPostMsg(hWndFrame,UM_DONE,MPFROMSHORT(curTh),0);
  
  if (hf) DosClose(hf);

  DosExit(EXIT_THREAD,0);
}

// Save the following fields
//
//  BSPBLK       DskParms
//  PTRACKLAYOUT DskLayout
//  USHORT       usLayoutSize

VOID FAR SaveImage(USHORT curTh) {

USHORT curBuff;
USHORT bpT;            // Bytes per track
USHORT sourceTracks;   // # tracks on source disk
USHORT trk;
USHORT hd;
USHORT cyl;
HFILE  hf;
USHORT result;
ULONG  FileSize;
CHAR   Header[] = "DskImage";
PBYTE  DskBuf;              // huge pointer to track data

  curBuff = tcThBufs[curTh].ImageNumber;
  tcThBufs[curTh].ErrorCode = 0;

// calculate file size
  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  FileSize = (sourceTracks * bpT) +
              sizeof Header +
              sizeof ImageBuffers[curBuff].DskParms;

  if (tcThBufs[curTh].ErrorCode = DosOpen2(ImageBuffers[curBuff].FileName,
                                       &hf,
                                       &result,
                                       FileSize,
                                       FILE_NORMAL,
                                       FILE_OPEN | FILE_CREATE,
                                       OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYREADWRITE,
                                       NULL,
                                       0L))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

// write dskim header
  if (tcThBufs[curTh].ErrorCode = DosWrite(hf,&Header,sizeof Header,&result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

// write parameter block
  if (tcThBufs[curTh].ErrorCode = DosWrite(hf,
                                           &ImageBuffers[curBuff].DskParms,
                                           sizeof ImageBuffers[curBuff].DskParms,
                                           &result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

// write disk data
  for (trk = 0, cyl = 0;
       trk < sourceTracks;
       trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
    for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
      ImageBuffers[curBuff].Percent =
        (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
      WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);

      DskBuf = MakeHugeP(curBuff,trk+hd);

      if (tcThBufs[curTh].ErrorCode = DosWrite(hf,DskBuf,bpT,&result))
        ThreadErrorHandler(UM_ERROR, curTh, hf);
    }
  }
  ImageBuffers[curBuff].Percent = 100;
  WinPostMsg(hWndFrame,UM_DONE,MPFROMSHORT(curTh),0);
  
  if (hf) DosClose(hf);

  DosExit(EXIT_THREAD,0);
}


VOID FAR CompImages(USHORT curTh) {

USHORT curBuff;
USHORT compBuff;
USHORT bpT;            // Bytes per track
USHORT sourceTracks;   // # tracks on source disk
USHORT trk;
USHORT hd;
USHORT cyl;
HFILE  hf;              // only needed for error handling
PBYTE  DskBuf1;         // huge pointer to track data
PBYTE  DskBuf2;         // huge pointer to track data

  curBuff = tcThBufs[curTh].ImageNumber;
  compBuff = tcThBufs[curTh].CompNumber;
  tcThBufs[curTh].ErrorCode = 0;

// compare BPB

  if (memcmp(&(ImageBuffers[curBuff].DskParms),
             &(ImageBuffers[compBuff].DskParms),
             sizeof(BSPBLK))) {
    ThreadErrorHandler(UM_COMPERR, curTh, hf);
    DosExit(EXIT_THREAD,0);
  }

// compare data
  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  for (trk = 0, cyl = 0;
       trk < sourceTracks;
       trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
    for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
      ImageBuffers[curBuff].Percent =
        (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
      WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);

      DskBuf1 = MakeHugeP(curBuff,trk+hd);
      DskBuf2 = MakeHugeP(compBuff,trk+hd);

      if (memcmp(DskBuf1,DskBuf2,bpT)) {
        ThreadErrorHandler(UM_COMPERR, curTh, hf);
        DosExit(EXIT_THREAD,0);
      }
    }
  }
  ImageBuffers[curBuff].Percent = 100;

  WinPostMsg(hWndFrame,UM_COMPOK,MPFROMSHORT(curTh),0);
  DosExit(EXIT_THREAD,0);
} // CompImages
