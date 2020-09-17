/*
           pmfloppy.c
           
           main program

           PM based disk utility.  Use separate threads for disk actions to
           prevent slowing up PM too much.  Use huge memory allocation for
           single pass reads.

           Copyright G. Bryant, 1990

   
  Change Log
   8-Jun-90   Correct array bounds violation in FreeThread
   9-Jun-90   Correct the EXPORTS statement in pmfloppy.def

*/

#define INCL_PM
#define INCL_BASE
#define INCL_DOSERRORS
#define INCL_DOSPROCESS
#define INCL_DOSDEVIOCTL
#define INCL_DOSSESMGR
#define INCL_GPILCIDS
#define INCL_WINSWITCHLIST
#include <os2.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include "pmfloppy.h"
#include "DskIm.h"

// prototypes for current file
MRESULT  EXPENTRY ClientWndProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT  Panic(PCH, USHORT);
MRESULT  DisplayStatus(HWND, HPS, USHORT);
MRESULT  DisplayDone(HWND , HPS, USHORT);
VOID     FreeThread(USHORT);
VOID     DisplayImageStat(HWND, HPS);
VOID     PutBox(PSZ, PSZ);

// prototypes from copydlgs.c
extern MRESULT EXPENTRY ReadDlgProc(HWND, USHORT, MPARAM, MPARAM);
extern MRESULT EXPENTRY WriteDlgProc(HWND, USHORT, MPARAM, MPARAM);
extern MRESULT EXPENTRY AboutDlgProc(HWND, USHORT, MPARAM, MPARAM);
extern MRESULT EXPENTRY DeleteDlgProc(HWND, USHORT, MPARAM, MPARAM);
extern MRESULT EXPENTRY LoadDlgProc(HWND, USHORT, MPARAM, MPARAM);
extern MRESULT EXPENTRY SaveDlgProc(HWND, USHORT, MPARAM, MPARAM);
extern MRESULT EXPENTRY CompDlgProc(HWND, USHORT, MPARAM, MPARAM);

// prototypes from dskcpy.c
extern VOID FAR readsource(USHORT);
extern VOID FAR writetarget(USHORT);
extern VOID FAR LoadImage(USHORT);
extern VOID FAR SaveImage(USHORT);
extern VOID FAR CompImages(USHORT);

/* GLOBAL VARIABLES */

// PM vbls
HAB    hab;
HMQ    hmq;
HWND   hWndFrame ;
static CHAR szClientClass[]="PMFloppy";
ULONG  ctldata = FCF_STANDARD & ~FCF_TASKLIST;
HWND   hWndClient;
HWND   hwndDeskTop;
QMSG   qmsg;
LONG   CharHeight;
LONG   CharWidth;

// User response vbls
DskImage ImageBuffers[NUM_IMAGES];
USHORT   BufferNum;                  // only use in foreground thread
USHORT   CompNum;                    // only use in foreground thread

// Thread variables
ThreadContext tcThBufs[NUM_THREADS];


int cdecl main(void)
{
SWCNTRL swctl;
HSWITCH hswitch;
PID pid;

  hab = WinInitialize(0);
  hmq = WinCreateMsgQueue(hab, 0);
  hwndDeskTop = WinQueryDesktopWindow(hab, NULL);

  if (!WinRegisterClass(hab, szClientClass, ClientWndProc, CS_SIZEREDRAW, 0))
    return(0);

  hWndFrame = WinCreateStdWindow(HWND_DESKTOP,
                                 WS_VISIBLE,
                                 &ctldata,
                                 szClientClass,
                                 "Floppy Disk Utility",
                                 0L,
                                 0,
                                 IDR_PMFLOPPY,
                                 &hWndClient);

  if (hWndFrame != NULL)
  {

    WinQueryWindowProcess(hWndFrame, &pid, NULL);

    swctl.hwnd = hWndFrame;
    swctl.hwndIcon = NULL;
    swctl.hprog = NULL;
    swctl.idProcess = pid;
    swctl.idSession = 0;
    swctl.uchVisibility = SWL_VISIBLE;
    swctl.fbJump = SWL_JUMPABLE;
    swctl.szSwtitle[0] = '\0';

    hswitch = WinAddSwitchEntry(&swctl);

    while ( WinGetMsg(hab, &qmsg, NULL, 0, 0) )
	    WinDispatchMsg(hab, &qmsg);

    WinDestroyWindow(hWndFrame);
  }

  WinDestroyMsgQueue(hmq);
  WinTerminate(hab);

  DosExit(EXIT_PROCESS, 0);
} /* main */


// ClientWndProc - main window processing
//
//  note: UM messages are User-defined (in pmfloppy.h).
//        all UM messages use mp2 to indicate the drive the message concerns,
//        and mp1 is data specific to that message.  Then UM_xxxMSG is a 
//        generic msg.  It is currently only used for DONE messages.  If I
//        need more, set up some magic numbers and pass them through mp1. 
//
MRESULT EXPENTRY ClientWndProc(HWND hwnd
			      , USHORT id
			      , MPARAM mp1
			      , MPARAM mp2) {

HPS         hPS;
HWND        hMenu;
CHAR        szTxtBuf[MAXMSGLEN];
int far     *stkptr;
FONTMETRICS fm;
USHORT      curBuff;
USHORT      gotSource;
BOOL        DriveActive;
USHORT      curTh;
RECTL       rctPaint;

  switch (id) {
    case WM_CREATE:
      for (curBuff=0;curBuff < NUM_IMAGES;curBuff++)
        ImageBuffers[curBuff].BufferName[0] = '\0';
      for (curTh=0;curTh < NUM_THREADS;curTh++) {
        tcThBufs[curTh].ThID = 0;
        tcThBufs[curTh].ImageNumber = NUM_IMAGES+1;
        tcThBufs[curTh].CompNumber = NUM_IMAGES+1;
      }
      hPS = WinGetPS(hwnd);
      GpiQueryFontMetrics(hPS, (long)sizeof(fm), &fm);
      CharHeight = fm.lMaxBaselineExt;
      CharWidth = fm.lMaxCharInc;
      WinReleasePS(hPS);
      break;

    case WM_PAINT:
      hPS = WinGetPS(hwnd);
      GpiErase(hPS);
      WinQueryUpdateRect(hwnd,&rctPaint);
      DisplayImageStat(hwnd, hPS);
      WinValidateRect(hwnd, &rctPaint, FALSE);
      WinReleasePS(hPS);
      break;

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd) {
        case IDM_READ:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,ReadDlgProc,0,READ_DLG,NULL)) {
            // send off thread to read
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;
              DosAllocSeg(STACK_SIZE, &(tcThBufs[curTh].selStk), 0);
              stkptr = (int far *)MAKEP(tcThBufs[curTh].selStk, STACK_SIZE);
              *(--stkptr) = curTh;

              if (DosCreateThread(readsource,
                                  &(tcThBufs[curTh].ThID),
                                  (PBYTE)stkptr)) {
                PutBox("Drive Read","DosCreateThread failed.");

  	  	  	    DosFreeSeg(tcThBufs[curTh].selStk);
	  	    	    return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Drive Read","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_WRITE:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,WriteDlgProc,0,WRITE_DLG,NULL)) {
            // send off thread to write
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;
              DosAllocSeg(STACK_SIZE, &(tcThBufs[curTh].selStk), 0);
              stkptr = (int far *)MAKEP(tcThBufs[curTh].selStk, STACK_SIZE);
              *(--stkptr) = curTh;

              if (DosCreateThread(writetarget,
                                  &(tcThBufs[curTh].ThID),
                                  (PBYTE)stkptr)) {
                PutBox("Drive Write","DosCreateThread failed.");
  	  	  	    DosFreeSeg(tcThBufs[curTh].selStk);
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Drive Write","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_ABOUT:
          WinDlgBox(HWND_DESKTOP,hWndFrame,AboutDlgProc,0,ABOUT_DLG,NULL);
          break;

        case IDM_DELETE:
          WinDlgBox(HWND_DESKTOP,hWndFrame,DeleteDlgProc,0,DELETE_DLG,NULL);
          hPS = WinGetPS(hwnd);
          GpiErase(hPS);
          DisplayImageStat(hwnd, hPS);
          WinReleasePS(hPS);
          break;

        case IDM_LOAD:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,LoadDlgProc,0,LOAD_DLG,NULL)) {
            // send off thread to load
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;
              DosAllocSeg(STACK_SIZE, &(tcThBufs[curTh].selStk), 0);
              stkptr = (int far *)MAKEP(tcThBufs[curTh].selStk, STACK_SIZE);
              *(--stkptr) = curTh;

              if (DosCreateThread(LoadImage,
                                  &(tcThBufs[curTh].ThID),
                                  (PBYTE)stkptr)) {
                PutBox("Image Load","DosCreateThread failed.");
  	  	  	    DosFreeSeg(tcThBufs[curTh].selStk);
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Image Load","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_SAVE:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,SaveDlgProc,0,SAVE_DLG,NULL)) {
            // send off thread to save
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;
              DosAllocSeg(STACK_SIZE, &(tcThBufs[curTh].selStk), 0);
              stkptr = (int far *)MAKEP(tcThBufs[curTh].selStk, STACK_SIZE);
              *(--stkptr) = curTh;

              if (DosCreateThread(SaveImage,
                                  &(tcThBufs[curTh].ThID),
                                  (PBYTE)stkptr)) {
                PutBox("Image Save","DosCreateThread failed.");
  	  	  	    DosFreeSeg(tcThBufs[curTh].selStk);
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Image Save","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_COMP:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,CompDlgProc,0,COMP_DLG,NULL)) {
            // send off thread to compare
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;
              tcThBufs[curTh].CompNumber  = CompNum;
              DosAllocSeg(STACK_SIZE, &(tcThBufs[curTh].selStk), 0);
              stkptr = (int far *)MAKEP(tcThBufs[curTh].selStk, STACK_SIZE);
              *(--stkptr) = curTh;

              if (DosCreateThread(CompImages,
                                  &(tcThBufs[curTh].ThID),
                                  (PBYTE)stkptr)) {
                PutBox("Image Compare","DosCreateThread failed.");
  	  	  	    DosFreeSeg(tcThBufs[curTh].selStk);
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Image Compare","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_EXIT:
          WinPostMsg(hWndFrame, WM_CLOSE, NULL, NULL);
          break;
      }
      break;

    case WM_INITMENU:
      //set the allowable menu choices.
      hMenu = WinWindowFromID(hWndFrame,FID_MENU);

      // If we don't have anything to write, disable the write, save, and 
      // delete menus
      gotSource = 0;
      for (curBuff=0;curBuff < NUM_IMAGES;curBuff++) {
        if (ImageBuffers[curBuff].Percent == 100) gotSource++;
      }

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_WRITE,TRUE),
        MPFROM2SHORT(MIA_DISABLED,gotSource ? 0 : MIA_DISABLED));

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_SAVE,TRUE),
        MPFROM2SHORT(MIA_DISABLED,gotSource ? 0 : MIA_DISABLED));

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_DELETE,TRUE),
        MPFROM2SHORT(MIA_DISABLED,gotSource ? 0 : MIA_DISABLED));

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_COMP,TRUE),
        MPFROM2SHORT(MIA_DISABLED,(gotSource > 1) ? 0 : MIA_DISABLED));
      break;


    case UM_STATUS:
      curTh = SHORT1FROMMP(mp1);
      hPS = WinGetPS(hwnd);
      DisplayStatus(hwnd, hPS, curTh);
      WinReleasePS(hPS);
      break;

    case UM_DONE:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      hPS = WinGetPS(hwnd);
      WinAlarm(HWND_DESKTOP,WA_NOTE);
      DisplayDone(hwnd, hPS,curBuff);
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;

    case UM_ERROR:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      switch (ImageBuffers[curBuff].Busy) {
        case BUSY_READ:
          sprintf(szTxtBuf,"Read Error on drive %c", ImageBuffers[curBuff].DriveID[0]);
          break;
        case BUSY_WRITE:
          sprintf(szTxtBuf,"Write Error on drive %c", ImageBuffers[curBuff].DriveID[0]);
          break;
        case BUSY_LOAD:
          sprintf(szTxtBuf,"Load Error on drive %s", ImageBuffers[curBuff].FileName);
          break;
        case BUSY_SAVE:
          sprintf(szTxtBuf,"Save Error on file %s", ImageBuffers[curBuff].FileName);
          break;
        }
      Panic(szTxtBuf, tcThBufs[curTh].ErrorCode);
      hPS = WinGetPS(hwnd);
      DisplayStatus(hwnd, hPS, curBuff);
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;

    case UM_COMPOK:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      WinAlarm(HWND_DESKTOP,WA_NOTE);
      PutBox("Image Compare","Images are identical!");
      hPS = WinGetPS(hwnd);
      DisplayDone(hwnd, hPS,curBuff);
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;

    case UM_COMPERR:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      WinAlarm(HWND_DESKTOP,WA_WARNING);
      PutBox("Image Compare","Images are different");
      hPS = WinGetPS(hwnd);
      DisplayDone(hwnd, hPS,curBuff);
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;

    case WM_CLOSE:
      DriveActive = FALSE;
      for (curBuff=0;curBuff < NUM_IMAGES;curBuff++)
        if (ImageBuffers[curBuff].Busy) DriveActive = TRUE;

      if (DriveActive) {
	  	  if (MBID_OK != WinMessageBox(HWND_DESKTOP,
                                     hWndFrame,
                                     "are you sure you want to quit?",
                                     "A Drive is still running - ",
                                     0,
                                     MB_OKCANCEL | MB_QUERY))
        break;
      }
      // else just fall through to default to get to the default proc
    default:
      return(WinDefWindowProc(hwnd, id, mp1, mp2));

  } /* switch id */

  return 0L;

} /* clientwndproc */


//  Panic  --  Put up a message box with an error message.
//
//  Inputs:   pszCaption  --  Caption text for message box
//
//  Returns:  1L, for error signalling from window procedures.
MRESULT Panic(PCH pszCaption,USHORT ErrorNum)
{
HWND hwndCurFocus;
CHAR buf[1024];
USHORT cbBuf;

  if (ErrorNum == DSKIM_ERROR_WRONG_FORMAT)
    sprintf(buf, "Error - target disk has incorrect format");
  else if (ErrorNum == DSKIM_ERROR_WRONG_FILE)
    sprintf(buf, "Error - file is not in correct format");
  else if (DosGetMessage(NULL, 0, buf, 1024, ErrorNum, "oso001.msg", &cbBuf))
  {
    sprintf(buf, "SYS%04d: error text unavailable", ErrorNum);
    cbBuf = 31;
  }
  buf[cbBuf] = (char)0;


  hwndCurFocus = WinQueryFocus(HWND_DESKTOP, FALSE);
  WinAlarm(HWND_DESKTOP, WA_ERROR);

  PutBox(pszCaption,buf);

  return(1L);
} // panic


// DisplayStatus - Display the status for the drive in the PS
//
//  in:  usPct   Percent done
//       Drive   drive letter
//       hwnd    window handle to display on
//       op      operation (eg read, write)
//
MRESULT DisplayStatus(HWND hwnd, HPS hPS, USHORT curTh) {

RECTL  rctStart;
CHAR   szTxtBuf[MAXMSGLEN];
CHAR   op[10];
CHAR   Object[6];
CHAR   Device[80];
USHORT curBuff;
USHORT compBuff;

  curBuff = tcThBufs[curTh].ImageNumber;
  compBuff = tcThBufs[curTh].CompNumber;

  switch (ImageBuffers[curBuff].Busy) {
    case BUSY_READ:
      strcpy(op,"reading");
      strcpy(Object,"Drive");
      Device[0] = ImageBuffers[curBuff].DriveID[0];
      Device[1] = '\0';
      break;
    case BUSY_WRITE:
      strcpy(op,"writing");
      strcpy(Object,"Drive");
      Device[0] = ImageBuffers[curBuff].DriveID[0];
      Device[1] = '\0';
      break;
    case BUSY_LOAD:
      strcpy(op,"loading");
      strcpy(Object,"file");
      strcpy(Device,ImageBuffers[curBuff].FileName);
      break;
    case BUSY_SAVE:
      strcpy(op,"saving");
      strcpy(Object,"file");
      strcpy(Device,ImageBuffers[curBuff].FileName);
      break;
    case BUSY_COMP:
      strcpy(op,"comparing");
      strcpy(Object,"image");
      strcpy(Device,ImageBuffers[compBuff].BufferName);
      break;
  }

  rctStart.xLeft = 5L;
  rctStart.xRight = 5L + (CharWidth * MAXMSGLEN);
  rctStart.yBottom = (curBuff * CharHeight) + 5;
  rctStart.yTop = rctStart.yBottom + CharHeight;
  sprintf(szTxtBuf,
          "Image %s %s %s %s (%d%% complete)",
          ImageBuffers[curBuff].BufferName,
          op,
          Object,
          Device,
          ImageBuffers[curBuff].Percent);
  WinDrawText(hPS, -1, szTxtBuf, &rctStart, CLR_NEUTRAL, CLR_BACKGROUND,
              DT_LEFT | DT_ERASERECT);
  WinValidateRect(hwnd, &rctStart, FALSE);
  return 0;
}


// DisplayDone - Display an operation complete
//
//  in:  Drive   drive letter
//       hwnd    window handle to display on
//       op      operation (eg read, write)
//
MRESULT DisplayDone(HWND hwnd, HPS hPS, USHORT curBuff) {

RECTL   rctStart;
CHAR    szTxtBuf[MAXMSGLEN];

  rctStart.xLeft = 5L;
  rctStart.xRight = 5L + (CharWidth * MAXMSGLEN);
  rctStart.yBottom = (curBuff * CharHeight) + 5;
  rctStart.yTop = rctStart.yBottom + CharHeight;
  sprintf(szTxtBuf, "Image %s is full", ImageBuffers[curBuff].BufferName);
  WinDrawText(hPS, -1, szTxtBuf, &rctStart, CLR_NEUTRAL, CLR_BACKGROUND,
              DT_LEFT | DT_ERASERECT);
  WinValidateRect(hwnd, &rctStart, FALSE);
  return 0;
}

// FreeThread - clean up after a child thread
//
//   in: curTh  - Thread to clean up
//
// Note: there doesn't seem to be an easy way to determine when a thread is
//       gone.  all the wait routines just work on processes.  So we cheat and
//       check the priority until we get a failure.  Like all busy waits,
//       this is incredibly idiotic, but until we get a "twait" function, I
//       can't think of a better way.
//
VOID FreeThread(USHORT curTh) {

USHORT curBuff;
USHORT compBuff;
USHORT Prty;

// sit here until the thread is gone
  while (DosGetPrty(PRTYS_THREAD,
                    &Prty,
                    tcThBufs[curTh].ThID) != ERROR_INVALID_THREADID) ;
  curBuff  = tcThBufs[curTh].ImageNumber;
  compBuff = tcThBufs[curTh].CompNumber;

  tcThBufs[curTh].ThID = 0;
  tcThBufs[curTh].ImageNumber = NUM_IMAGES+1;
  tcThBufs[curTh].CompNumber = NUM_IMAGES+1;
  tcThBufs[curTh].ErrorCode = 0;
  DosFreeSeg(tcThBufs[curTh].selStk);
  ImageBuffers[curBuff].Busy = FALSE;
  if (compBuff < NUM_IMAGES) ImageBuffers[compBuff].Busy = FALSE;

}


VOID DisplayImageStat(HWND hwnd, HPS hPS) {
RECTL  rctStart;
RECTL  rctPaint;
USHORT curBuff;

  WinQueryWindowRect(hwnd,&rctPaint);
  if (ImageBuffers[0].BufferName[0] == '\0') {
    rctStart.xLeft = 5L;
    rctStart.xRight = 5L + (CharWidth * 16);
    rctStart.yBottom = 5L;
    rctStart.yTop = rctStart.yBottom + CharHeight;
    WinDrawText(hPS,
                -1,
                "No images in use",
                &rctStart,
                CLR_NEUTRAL,
                CLR_BACKGROUND,
                DT_LEFT | DT_ERASERECT);
  }
  else {
    for (curBuff=0;curBuff < NUM_IMAGES;curBuff++)
      if (ImageBuffers[curBuff].BufferName[0] != '\0') {
        if (ImageBuffers[curBuff].Busy) DisplayStatus(hwnd, hPS, curBuff);
        else DisplayDone(hwnd,hPS, curBuff);

      }
  }
}


//
// Put the messages in an application modal message box
//
VOID PutBox(PSZ msg1, PSZ msg2) {

  WinMessageBox(HWND_DESKTOP,
                hWndFrame,
                msg2,
                msg1,
                0,
                MB_OK | MB_ICONEXCLAMATION);
  return;
} // PutBox
