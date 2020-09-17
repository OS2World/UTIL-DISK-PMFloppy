/*
           copydlgs.c
           
           dialog box routines for pmfloppy

           Copyright G. Bryant, 1990

   
  Change Log

   6-Jun-90   Change ParseFileName to return different value for empty
              file Name -vs- invalid file name.  change SELECT to ENTER
              for IDD_DI_DIRS field in load and save.
   8-Jun-90   Correct the busy check for Write
*/




#define INCL_PM
#define INCL_DOSERRORS
#define INCL_BASE
#include <os2.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pmfloppy.h"
#include "DskIm.h"

// external function prototypes
extern VOID   PutBox(PSZ, PSZ);


// global function prototypes
MRESULT EXPENTRY ReadDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY WriteDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY DeleteDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY LoadDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY SaveDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY CompDlgProc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY AboutDlgProc(HWND, USHORT, MPARAM, MPARAM);

// local function prototypes
VOID   GetImageList(HWND, USHORT, USHORT);
VOID   GetDirList(HWND, CHAR *);
VOID   GetFileList(HWND);
USHORT SetBox(HWND, MPARAM);
USHORT SetEdit(HWND, USHORT);
BOOL   SetDrive(HWND, USHORT);
BOOL   ParseFileName (CHAR *, CHAR *);
BOOL   CheckLSParms(HWND, USHORT);

// PM vbls
extern HWND   hWndFrame ;

// User response vbls
extern DskImage ImageBuffers[NUM_IMAGES];
extern USHORT   BufferNum;
extern USHORT   CompNum;


// set up image list and edit boxes
VOID GetImageList(HWND hwnd, USHORT EditB, USHORT ListB) {

USHORT curBuff;

  for (curBuff=0;
       (curBuff < NUM_IMAGES) &&
        (ImageBuffers[curBuff].BufferName[0] != '\0');
       curBuff++) {
    WinSendDlgItemMsg(hwnd,
                      ListB,
                      LM_INSERTITEM,
                      MPFROM2SHORT(LIT_END,0),
                      MPFROMP(ImageBuffers[curBuff].BufferName));
  }
  curBuff = 0;

  WinSendDlgItemMsg(hwnd,
                    EditB,
                    EM_SETTEXTLIMIT,
                    MPFROM2SHORT(BUFFERNMSZ,0),
                    NULL);

  WinSetDlgItemText(hwnd, EditB, ImageBuffers[curBuff].BufferName);
  return;
} // GetImageList


//
// return the buffer number of the selected image
//
USHORT SetBox(HWND hwnd, MPARAM mp1) {

USHORT curBuff;
SHORT  Select;
CHAR   curBuffName[BUFFERNMSZ];

  // get the selection number
  Select = (USHORT) WinSendDlgItemMsg(hwnd,
                                      SHORT1FROMMP(mp1),
                                      LM_QUERYSELECTION,
                                      0L,
                                      0L);
  // get the selection text
  WinSendDlgItemMsg(hwnd,
                    SHORT1FROMMP(mp1),
                    LM_QUERYITEMTEXT,
                    MPFROM2SHORT(Select,BUFFERNMSZ),
                    MPFROMP(curBuffName));
  // get the buffer number
  for (curBuff=0;
       (curBuff < NUM_IMAGES) &&
       (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
       curBuff++) ;

  return(curBuff);
} // SetBox


// Set the drive letter - returns true if illegal value
BOOL SetDrive(HWND hwnd, USHORT curBuff) {

  WinQueryDlgItemText(hwnd,
                      IDD_DI_DRV,
                      2,
                      ImageBuffers[curBuff].DriveID);
  ImageBuffers[curBuff].DriveID[0] = (CHAR) toupper(ImageBuffers[curBuff].DriveID[0]);
  if (!isupper((USHORT) ImageBuffers[curBuff].DriveID[0])) {
    PutBox("Destination Drive must be a drive letter","e.g. A");
    return(TRUE);
  }
  return(FALSE);
} // SetDrive



//  ReadDlgProc  --  Disk Read dialog procedure.
//
//  Output:  ReadDrive  --  Global name of selected drive
MRESULT CALLBACK ReadDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2) {

USHORT curChar;
USHORT curBuff;
CHAR   curBuffName[BUFFERNMSZ];
USHORT NameLen;

  switch (id)	{
    case WM_INITDLG:
      GetImageList(hwnd, IDD_DI_FLD, IDD_DI_BOX);
      WinSendDlgItemMsg(hwnd,
                        IDD_DI_DRV,
                        EM_SETTEXTLIMIT,
                        MPFROM2SHORT(1,0),
                        NULL);
      return(0);

    case WM_CONTROL:  // get the button that was clicked
      if (SHORT1FROMMP(mp1) == IDD_DI_BOX) {
        curBuff = SetBox(hwnd, mp1);
        switch (SHORT2FROMMP(mp1)) {
          case LN_SELECT:
            WinSetDlgItemText(hwnd,
                              IDD_DI_FLD,
                              ImageBuffers[curBuff].BufferName);
            return(0);
          case LN_ENTER:
            if (SetDrive(hwnd, curBuff)) return(0);
            if (ImageBuffers[curBuff].Busy) {
              PutBox("That image is busy -","please select another");
            }
            else {
              ImageBuffers[curBuff].Busy = BUSY_READ;
              BufferNum = curBuff;
              WinDismissDlg(hwnd, TRUE);
            }
            return(0);
        }

      }
	    return(0);

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
          NameLen = WinQueryDlgItemText(hwnd,
                                        IDD_DI_FLD,
                                        BUFFERNMSZ,
                                        curBuffName);

          if (NameLen == 0) {
            PutBox("You must enter a buffer name,","or select 'Cancel'");
            return(0);
          }

          for (curChar=0;curChar < BUFFERNMSZ;curChar++)
            curBuffName[curChar] = (CHAR) toupper(curBuffName[curChar]);

          for (curBuff=0;
               (curBuff < NUM_IMAGES) &&
               (ImageBuffers[curBuff].BufferName[0] != '\0') &&
               (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
               curBuff++) ;

          if (curBuff < NUM_IMAGES) {
            if (SetDrive(hwnd, curBuff)) return(0);
            if (ImageBuffers[curBuff].Busy) {
              PutBox("That image is busy -","please select another");
            }
            else {
              ImageBuffers[curBuff].Busy = BUSY_READ;
              strncpy(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ);
              BufferNum = curBuff;
              WinDismissDlg(hwnd, TRUE);
            }

          }
          else {
            PutBox("No image buffers left","(try deleting some)");
          }
          return(0);


        case DID_CANCEL:
	        WinDismissDlg(hwnd, FALSE);
          return(0);
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // ReadDlgProc


//  WriteDlgProc  --  Disk Write dialog procedure.
//
//  Output:  WriteDrive  --  Global name of selected drive
MRESULT CALLBACK WriteDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2) {

USHORT NameLen;
USHORT curChar;
USHORT curBuff;
static CHAR   curBuffName[BUFFERNMSZ];
static USHORT FormOpt;

  switch (id)
	{
    case WM_INITDLG:
      GetImageList(hwnd, IDD_DI_FLD, IDD_DI_BOX);
      WinSendDlgItemMsg(hwnd,
                        IDD_DI_DRV,
                        EM_SETTEXTLIMIT,
                        MPFROM2SHORT(1,0),
                        NULL);
      WinSendDlgItemMsg(hwnd,
                        IDD_WRF_MAYBE,
                        BM_SETCHECK,
                        MPFROMSHORT(1),
                        NULL);
      FormOpt = IDD_WRF_MAYBE;
      return(0);

    case WM_CONTROL:  // get the button that was clicked
      if (SHORT1FROMMP(mp1) == IDD_DI_BOX) {
        curBuff = SetBox(hwnd, mp1);
        switch (SHORT2FROMMP(mp1)) {
          case LN_SELECT:
            WinSetDlgItemText(hwnd,
                              IDD_DI_FLD,
                              ImageBuffers[curBuff].BufferName);
            return(0);
          case LN_ENTER:
            if (SetDrive(hwnd, curBuff)) return(0);
            if (ImageBuffers[curBuff].Busy) {
              PutBox("That image is busy -","please select another");
            }
            else {
              ImageBuffers[curBuff].Busy = BUSY_WRITE;
              ImageBuffers[curBuff].FormatOptions = FormOpt;
              BufferNum = curBuff;
              WinDismissDlg(hwnd, TRUE);
            }
            return(0);
        }

      }
      else if ((SHORT1FROMMP(mp1) == IDD_WRF_NEVER) ||
               (SHORT1FROMMP(mp1) == IDD_WRF_MAYBE) ||
               (SHORT1FROMMP(mp1) == IDD_WRF_ALWAYS))
        FormOpt = SHORT1FROMMP(mp1);
	    return(0);

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
          NameLen = WinQueryDlgItemText(hwnd,
                                        IDD_DI_FLD,
                                        BUFFERNMSZ,
                                        curBuffName);

          for (curChar=0;curChar < BUFFERNMSZ;curChar++)
            curBuffName[curChar] = (CHAR) toupper(curBuffName[curChar]);

          for (curBuff=0;
               (curBuff < NUM_IMAGES) &&
               (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
               curBuff++) ;

          if ((curBuff < NUM_IMAGES) && (NameLen > 0)) {
            if (SetDrive(hwnd, curBuff)) return(0);
            if (ImageBuffers[curBuff].Busy) {
              PutBox("That image is busy -","please select another");
            }
            else {
              ImageBuffers[curBuff].Busy = BUSY_WRITE;
              strncpy(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ);
              ImageBuffers[curBuff].FormatOptions = FormOpt;
              BufferNum = curBuff;
              WinDismissDlg(hwnd, TRUE);
            }
          }
          else {
            PutBox("You must select an existing","image buffer");

          }
          return(0);

        case DID_CANCEL:
	        WinDismissDlg(hwnd, FALSE);
          return(0);
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // WriteDlgProc



//  DeleteDlgProc  --  Image Buffer Delete dialog procedure.
//
MRESULT CALLBACK DeleteDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2) {

USHORT NameLen;
USHORT curChar;
USHORT curBuff;
CHAR   curBuffName[BUFFERNMSZ];

  switch (id)
	{
    case WM_INITDLG:
      GetImageList(hwnd, IDD_DI_FLD, IDD_DI_BOX);
      return(0);

    case WM_CONTROL:  // get the button that was clicked
      if (SHORT1FROMMP(mp1) == IDD_DI_BOX) {
        curBuff = SetBox(hwnd, mp1);
        switch (SHORT2FROMMP(mp1)) {
          case LN_SELECT:
            WinSetDlgItemText(hwnd,
                              IDD_DI_FLD,
                              ImageBuffers[curBuff].BufferName);
            return(0);
          case LN_ENTER:
            if (ImageBuffers[curBuff].Busy)
              PutBox("That buffer is in use.","");
            else {
              // free the memory used by that buffer and coalesce the list
              DosFreeSeg(ImageBuffers[curBuff].DskSel);
              free(ImageBuffers[curBuff].DskLayout);
              while (((curBuff+1) < NUM_IMAGES) &&
                     (ImageBuffers[curBuff].BufferName[0] != '\0')) {
                ImageBuffers[curBuff]  = ImageBuffers[curBuff+1];
                curBuff++;
              }
              WinDismissDlg(hwnd, TRUE);
            }
        }

      }
	    return(0);

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
          NameLen = WinQueryDlgItemText(hwnd,
                                        IDD_DI_FLD,
                                        BUFFERNMSZ,
                                        curBuffName);

          for (curChar=0;curChar < BUFFERNMSZ;curChar++)
            curBuffName[curChar] = (CHAR) toupper(curBuffName[curChar]);

          for (curBuff=0;
               (curBuff < NUM_IMAGES) &&
               (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
               curBuff++) ;

          if ((curBuff < NUM_IMAGES) && (NameLen > 0)) {
            if (ImageBuffers[curBuff].Busy)
              PutBox("That buffer is in use.","");
            else {
              // free the memory used by that buffer and coalesce the list
              DosFreeSeg(ImageBuffers[curBuff].DskSel);
              free(ImageBuffers[curBuff].DskLayout);
              while (((curBuff+1) < NUM_IMAGES) &&
                     (ImageBuffers[curBuff].BufferName[0] != '\0')) {
                ImageBuffers[curBuff]  = ImageBuffers[curBuff+1];
                curBuff++;
              }
              WinDismissDlg(hwnd, TRUE);
            }
          }
          else {
            PutBox("You must select an existing","image buffer");
          }
          return(0);

        case DID_CANCEL:
	        WinDismissDlg(hwnd, FALSE);
          return(0);
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // DeleteDlgProc

VOID GetDirList(HWND hwnd, CHAR *pcCurrentPath) {

static CHAR szDrive [] = "  :" ;
FILEFINDBUF findbuf ;
HDIR        hDir = 1 ;
SHORT       sDrive ;
USHORT      usDriveNum, usCurPathLen, usSearchCount = 1 ;
ULONG       ulDriveMap ;

  DosQCurDisk (&usDriveNum, &ulDriveMap) ;
  pcCurrentPath[0] = (CHAR) usDriveNum + '@' ;
  pcCurrentPath[1] = ':' ;
  pcCurrentPath[2] = '\\' ;
  usCurPathLen = 64 ;
  DosQCurDir(0, pcCurrentPath + 3, &usCurPathLen) ;

  WinSetDlgItemText (hwnd, IDD_DI_PATH, pcCurrentPath) ;
  WinSendDlgItemMsg (hwnd, IDD_DI_DIRS, LM_DELETEALL, NULL, NULL) ;

  for (sDrive = 0 ; sDrive < 26 ; sDrive++)
    if (ulDriveMap & 1L << sDrive) {
      szDrive [1] = (CHAR) sDrive + 'A' ;

      WinSendDlgItemMsg(hwnd,
                        IDD_DI_DIRS,
                        LM_INSERTITEM,
                        MPFROM2SHORT (LIT_END, 0),
                        MPFROMP (szDrive)) ;
    }

  DosFindFirst("*.*",&hDir,0x0017,&findbuf,sizeof findbuf,&usSearchCount,0L) ;
  while (usSearchCount) {
    if ((findbuf.attrFile & 0x0010) &&
        (findbuf.achName [0] != '.' || findbuf.achName [1]))
               
    WinSendDlgItemMsg (hwnd, IDD_DI_DIRS, LM_INSERTITEM,
                                  MPFROM2SHORT (LIT_SORTASCENDING, 0),
                                  MPFROMP (findbuf.achName)) ;

    DosFindNext (hDir, &findbuf, sizeof findbuf, &usSearchCount) ;
  }
} // GetDirList

VOID GetFileList(HWND hwnd) {

FILEFINDBUF findbuf ;
HDIR        hDir = 1 ;
USHORT      usSearchCount = 1 ;

  WinSendDlgItemMsg (hwnd, IDD_DI_FILES, LM_DELETEALL, NULL, NULL) ;

  DosFindFirst("*.*",&hDir,0x0007,&findbuf,sizeof findbuf,&usSearchCount,0L);
  while (usSearchCount) {
    WinSendDlgItemMsg(hwnd,
                      IDD_DI_FILES,
                      LM_INSERTITEM,
                      MPFROM2SHORT (LIT_SORTASCENDING, 0),
                      MPFROMP (findbuf.achName)) ;

    DosFindNext(hDir, &findbuf, sizeof findbuf, &usSearchCount) ;
  }
} // GetFileList


//  In:    pcOut -- Pointer to parsed file spec
//         pcIn  -- Pointer to raw file spec
//  Returns 0 if bad file name
//          1 if nothing
//          else ok
//
//  Note:  Changes current drive and directory to pcIn
//
BOOL ParseFileName (CHAR *pcOut, CHAR *pcIn) {
                       
CHAR   *pcLastSlash;
CHAR   *pcFileOnly;
ULONG  ulDriveMap ;
USHORT usDriveNum;
USHORT usDirLen = 64;

  strupr(pcIn);
  if (pcIn[0] == '\0') return(1);

  // Get drive from input string or current drive
  if (pcIn[1] == ':') {
    if (DosSelectDisk(pcIn[0] - '@')) return(0);
    pcIn += 2 ;
  }
  DosQCurDisk(&usDriveNum, &ulDriveMap) ;

  *pcOut++ = (CHAR) usDriveNum + '@' ;
  *pcOut++ = ':' ;
  *pcOut++ = '\\' ;

  // If rest of string is empty, return error
  if (pcIn[0] == '\0') return(1);

  // Search for last backslash.  If none, could be directory.
  if (NULL == (pcLastSlash = strrchr(pcIn, '\\'))) {
	  if (!DosChDir(pcIn, 0L)) return(1);

    // Otherwise, get current dir & attach input filename
    DosQCurDir(0, pcOut, &usDirLen);
    if (*(pcOut + strlen(pcOut) - 1) != '\\') strcat(pcOut++, "\\");
    strcat(pcOut, pcIn) ;
    return(2);
  }
  // If the only backslash is at beginning, change to root
  if (pcIn == pcLastSlash) {
	  DosChDir("\\", 0L) ;
    if (pcIn[1] == '\0') return(1);
    strcpy(pcOut, pcIn + 1);
    return(2);
  }

  // Attempt to change directory -- Get current dir if OK
  *pcLastSlash = '\0';
  if (DosChDir(pcIn, 0L)) return(0);
  DosQCurDir(0, pcOut, &usDirLen);

  // Append input filename, if any
  pcFileOnly = pcLastSlash + 1;
  if (*pcFileOnly == '\0') return(1);
  if (*(pcOut + strlen(pcOut) - 1) != '\\') strcat(pcOut++, "\\");
  strcat(pcOut, pcFileOnly);
  return(2);
} // ParseFileName


//  return true if everything is okay
BOOL CheckLSParms(HWND hwnd, USHORT Mode) {

CHAR   szBuffer[FILENMSZ];
USHORT curBuff;
CHAR   curBuffName[BUFFERNMSZ];
USHORT NameLen;
USHORT curChar;
CHAR   szFileName[FILENMSZ];

  NameLen = WinQueryDlgItemText(hwnd,
                                IDD_DI_FNAME,
                                FILENMSZ,
                                szBuffer);

  switch (ParseFileName(szFileName, szBuffer)) {
    case 0:
      PutBox("Illegal file name.","");
      return(FALSE);
    case 1: // no error, just don't do anything
      return(FALSE);
  }
  NameLen = WinQueryDlgItemText(hwnd,
                                IDD_DI_FLD,
                                BUFFERNMSZ,
                                curBuffName);

  if (NameLen == 0) {
    PutBox("You must enter a buffer name,","or select 'Cancel'");
    return(FALSE);
  }

  for (curChar=0;curChar < BUFFERNMSZ;curChar++)
    curBuffName[curChar] = (CHAR) toupper(curBuffName[curChar]);

// set the current buffer - must base on mode 'cause save can only use
// existing buffers while load can create new ones
  if (Mode == BUSY_LOAD)
    for (curBuff=0;
         (curBuff < NUM_IMAGES) &&
         (ImageBuffers[curBuff].BufferName[0] != '\0') &&
         (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
         curBuff++);
  else
    for (curBuff=0;
         (curBuff < NUM_IMAGES) &&
         (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
         curBuff++);


  if (NameLen < 1) {
    PutBox("You must select an","image buffer");
    return(FALSE);
  }

  if (curBuff < NUM_IMAGES) {
    if (ImageBuffers[curBuff].Busy)
      PutBox("That buffer is in use.","");
    else {
      strcpy(ImageBuffers[curBuff].FileName,szFileName);
      strncpy(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ);
      ImageBuffers[curBuff].DriveID[0] = ' ';
      ImageBuffers[curBuff].Busy = Mode;
      BufferNum = curBuff;
      return(TRUE);
    }
  }
  else
    PutBox("You must select an existing","image buffer");
  return(FALSE);
} // CheckLSParms


//  LoadDlgProc  --  Image Buffer Load dialog procedure.
//
MRESULT CALLBACK LoadDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2) {

USHORT curBuff;
static CHAR szCurrentPath[FILENMSZ];
static CHAR szBuffer[FILENMSZ];
SHORT  Select;

  switch (id)
	{
    case WM_INITDLG:
      GetImageList(hwnd, IDD_DI_FLD, IDD_DI_BOX);
      GetDirList(hwnd, szCurrentPath) ;
      GetFileList(hwnd) ;

      WinSendDlgItemMsg(hwnd,
                        IDD_DI_FNAME,
                        EM_SETTEXTLIMIT,
                        MPFROM2SHORT (FILENMSZ, 0),
                        NULL) ;
      WinSetFocus(HWND_DESKTOP,WinWindowFromID(hwnd,IDD_DI_FNAME));
      return(0);

    case WM_CONTROL:  // get the button that was clicked
      switch (SHORT1FROMMP(mp1)) {
        case IDD_DI_DIRS:
          Select = (USHORT) WinSendDlgItemMsg(hwnd,
                                              SHORT1FROMMP(mp1),
                                              LM_QUERYSELECTION,
                                              0L,
                                              0L) ;

          WinSendDlgItemMsg(hwnd,
                            SHORT1FROMMP(mp1),
                            LM_QUERYITEMTEXT,
                            MPFROM2SHORT(Select, sizeof szBuffer),
                            MPFROMP(szBuffer)) ;
          switch (SHORT2FROMMP(mp1)) {
            case LN_ENTER:
              if (szBuffer[0] == ' ') DosSelectDisk(szBuffer[1] - '@');
              else DosChDir (szBuffer, 0L);

              GetDirList(hwnd, szCurrentPath);
              GetFileList(hwnd);

              WinSetDlgItemText(hwnd, IDD_DI_FNAME, "");
              return(0);
          }
          break;

        case IDD_DI_FILES:
          Select = (USHORT) WinSendDlgItemMsg(hwnd,
                                              SHORT1FROMMP(mp1),
                                              LM_QUERYSELECTION,
                                              0L,
                                              0L) ;

          WinSendDlgItemMsg(hwnd,
                            SHORT1FROMMP(mp1),
                            LM_QUERYITEMTEXT,
                            MPFROM2SHORT(Select, sizeof szBuffer),
                            MPFROMP(szBuffer)) ;
          switch (SHORT2FROMMP(mp1)) {
            case LN_SELECT:
              WinSetDlgItemText(hwnd, IDD_DI_FNAME, szBuffer);
              return(0);

            case LN_ENTER:
              if (CheckLSParms(hwnd, BUSY_LOAD)) {
                WinDismissDlg(hwnd, TRUE);
              }
              return(0);
          }
          break ;

        case IDD_DI_BOX:
          curBuff = SetBox(hwnd, mp1);
          switch (SHORT2FROMMP(mp1)) {
            case LN_SELECT:
              WinSetDlgItemText(hwnd,
                                IDD_DI_FLD,
                                ImageBuffers[curBuff].BufferName);
              return(0);
            case LN_ENTER:
              if (ImageBuffers[curBuff].Busy)
                PutBox("That buffer is in use.","");
              else {
                if (CheckLSParms(hwnd, BUSY_LOAD)) {
                  WinDismissDlg(hwnd, TRUE);
                }
              }
          }
      }
      return(0);

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
          if (CheckLSParms(hwnd, BUSY_LOAD)) {
            WinDismissDlg(hwnd, TRUE);
          }
          return(0);

        case DID_CANCEL:
          WinDismissDlg(hwnd, FALSE);
          return(0);
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // LoadDlgProc


//  SaveDlgProc  --  Image Buffer Save dialog procedure.
//
MRESULT CALLBACK SaveDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2) {

USHORT curBuff;
static CHAR szCurrentPath [FILENMSZ];
static CHAR szBuffer [FILENMSZ];
SHORT  Select;

  switch (id)
	{
    case WM_INITDLG:
      GetImageList(hwnd, IDD_DI_FLD, IDD_DI_BOX);
      GetDirList(hwnd, szCurrentPath) ;
      GetFileList(hwnd) ;

      WinSendDlgItemMsg(hwnd,
                        IDD_DI_FNAME,
                        EM_SETTEXTLIMIT,
                        MPFROM2SHORT (FILENMSZ, 0),
                        NULL) ;
      return(0);

    case WM_CONTROL:  // get the button that was clicked
      switch (SHORT1FROMMP(mp1)) {
        case IDD_DI_DIRS:
          Select = (USHORT) WinSendDlgItemMsg(hwnd,
                                              SHORT1FROMMP(mp1),
                                              LM_QUERYSELECTION,
                                              0L,
                                              0L) ;

          WinSendDlgItemMsg(hwnd,
                            SHORT1FROMMP(mp1),
                            LM_QUERYITEMTEXT,
                            MPFROM2SHORT(Select, sizeof szBuffer),
                            MPFROMP(szBuffer)) ;
          switch (SHORT2FROMMP(mp1)) {
            case LN_ENTER:
              if (szBuffer[0] == ' ') DosSelectDisk(szBuffer[1] - '@');
              else DosChDir (szBuffer, 0L);

              GetDirList(hwnd, szCurrentPath);
              GetFileList(hwnd);

              WinSetDlgItemText(hwnd, IDD_DI_FNAME, "");
              return(0);
          }
          break;

        case IDD_DI_FILES:
          Select = (USHORT) WinSendDlgItemMsg(hwnd,
                                              SHORT1FROMMP(mp1),
                                              LM_QUERYSELECTION,
                                              0L,
                                              0L) ;

          WinSendDlgItemMsg(hwnd,
                            SHORT1FROMMP(mp1),
                            LM_QUERYITEMTEXT,
                            MPFROM2SHORT(Select, sizeof szBuffer),
                            MPFROMP(szBuffer)) ;
          switch (SHORT2FROMMP(mp1)) {
            case LN_SELECT:
              WinSetDlgItemText(hwnd, IDD_DI_FNAME, szBuffer);
              return(0);

            case LN_ENTER:
              if (CheckLSParms(hwnd, BUSY_SAVE)) {
                WinDismissDlg(hwnd, TRUE);
              }
              return(0);
          }
          break ;

        case IDD_DI_BOX:
          curBuff = SetBox(hwnd, mp1);
          switch (SHORT2FROMMP(mp1)) {
            case LN_SELECT:
              WinSetDlgItemText(hwnd,
                                IDD_DI_FLD,
                                ImageBuffers[curBuff].BufferName);
              return(0);
            case LN_ENTER:
              if (ImageBuffers[curBuff].Busy)
                PutBox("That buffer is in use.","");
              else {
                if (CheckLSParms(hwnd, BUSY_SAVE)) {
                  WinDismissDlg(hwnd, TRUE);
                }
              }
          }
      } // switch for CONTROL
	    return(0);

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
          if (CheckLSParms(hwnd, BUSY_SAVE)) {
            WinDismissDlg(hwnd, TRUE);
          }
          return(0);

        case DID_CANCEL:
	        WinDismissDlg(hwnd, FALSE);
          return(0);
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // SaveDlgProc



//
// return the buffer number of the field in the edit box
//
USHORT SetEdit(HWND hwnd, USHORT EditB) {

USHORT curBuff;
USHORT curChar;
CHAR   curBuffName[BUFFERNMSZ];
USHORT NameLen;

  NameLen = WinQueryDlgItemText(hwnd,
                                EditB,
                                BUFFERNMSZ,
                                curBuffName);

  if (NameLen == 0) {
    PutBox("You must enter an image name,","  or select 'Cancel'");
    return(NUM_IMAGES+1);
  }

  for (curChar=0;curChar < BUFFERNMSZ;curChar++)
    curBuffName[curChar] = (CHAR) toupper(curBuffName[curChar]);

  for (curBuff=0;
       (curBuff < NUM_IMAGES) &&
       (ImageBuffers[curBuff].BufferName[0] != '\0') &&
       (strncmp(ImageBuffers[curBuff].BufferName,curBuffName,BUFFERNMSZ));
       curBuff++) ;

  if (curBuff == NUM_IMAGES) {
    PutBox("You must select an existing","image buffer");
    return(NUM_IMAGES+1);
  }

  if (ImageBuffers[curBuff].Busy) {
    PutBox("The image is busy -","please select another");
    return(NUM_IMAGES+1);
  }

  return(curBuff);
}

//  CompDlgProc  --  Image Buffer Compare dialog procedure.
//
MRESULT CALLBACK CompDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2) {

USHORT curBuff;

  switch (id)
	{
    case WM_INITDLG:
      GetImageList(hwnd, IDD_DI_FLD, IDD_DI_BOX);
      GetImageList(hwnd, IDD_CM_FLD, IDD_CM_BOX);
      BufferNum = NUM_IMAGES+1;
      CompNum = NUM_IMAGES+1;
      return(0);

    case WM_CONTROL:  // get the button that was clicked
      curBuff = SetBox(hwnd, mp1);
      switch (SHORT1FROMMP(mp1)) {

        case IDD_DI_BOX:
          switch (SHORT2FROMMP(mp1)) {
            case LN_SELECT:
              WinSetDlgItemText(hwnd,
                                IDD_DI_FLD,
                                ImageBuffers[curBuff].BufferName);
              return(0);
            case LN_ENTER:
              BufferNum = SetEdit(hwnd, IDD_DI_FLD);
              CompNum   = SetEdit(hwnd, IDD_CM_FLD);
              if ((CompNum == NUM_IMAGES+1) ||
                  (CompNum == BufferNum))
                PutBox("You must select two","different (and existing) images");
              else {
                ImageBuffers[BufferNum].Busy = BUSY_COMP;
                ImageBuffers[CompNum].Busy = BUSY_COMP;
                WinDismissDlg(hwnd, TRUE);
              }
          }
          break;
        case IDD_CM_BOX:
          switch (SHORT2FROMMP(mp1)) {
            case LN_SELECT:
              WinSetDlgItemText(hwnd,
                                IDD_CM_FLD,
                                ImageBuffers[curBuff].BufferName);
              CompNum = curBuff;
              return(0);
            case LN_ENTER:
              BufferNum = SetEdit(hwnd, IDD_DI_FLD);
              CompNum   = SetEdit(hwnd, IDD_CM_FLD);
              if ((BufferNum == NUM_IMAGES+1) ||
                  (BufferNum == CompNum))
                PutBox("You must select two","different (and existing) images");
              else {
                ImageBuffers[BufferNum].Busy = BUSY_COMP;
                ImageBuffers[CompNum].Busy = BUSY_COMP;
                WinDismissDlg(hwnd, TRUE);
              }
          }
          break;
      } // switch for CONTROL
	    return(0);

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
          BufferNum = SetEdit(hwnd, IDD_DI_FLD);
          CompNum   = SetEdit(hwnd, IDD_CM_FLD);
          ImageBuffers[BufferNum].Busy = BUSY_COMP;
          ImageBuffers[CompNum].Busy = BUSY_COMP;
          WinDismissDlg(hwnd, TRUE);
          return(0);

        case DID_CANCEL:
	        WinDismissDlg(hwnd, FALSE);
          return(0);
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // CompDlgProc


//  AboutDlgProc  --  About box dialog procedure.
//
MRESULT CALLBACK AboutDlgProc(HWND hwnd, USHORT id, MPARAM mp1, MPARAM mp2)
{

  switch (id)
	{
    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd)
  		{
  	    case DID_OK:
        case DID_CANCEL:
	        WinDismissDlg(hwnd, TRUE);
          return 0;
      } // switch on commandID
	    break;
	} // switch on msgID

  return(WinDefDlgProc(hwnd, id, mp1, mp2));
} // AboutDlgProc


