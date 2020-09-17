// DskImage 1.0, Copyright 1990, Greg Bryant
//

#define BSPBLK BIOSPARAMETERBLOCK

#define BUFFERNMSZ  8
#define VOLUMENMSZ  11
#define FILENMSZ    80

#define BUSY        0xFFFF
#define BUSY_READ   0x0001
#define BUSY_WRITE  0x0002
#define BUSY_SAVE   0x0004
#define BUSY_LOAD   0x0008
#define BUSY_COMP   0x0010

// Note, for the lockdrive/unlockdrive macros, the global variable _lockCmd
// must be accessable and set to zero!

#define lockdrive(hf)   (tcThBufs[curTh].ErrorCode = DosDevIOCtl(0L, &_lockCmd, DSK_LOCKDRIVE,   IOCTL_DISK, hf))
#define unlockdrive(hf) (tcThBufs[curTh].ErrorCode = DosDevIOCtl(0L, &_lockCmd, DSK_UNLOCKDRIVE, IOCTL_DISK, hf))

typedef struct _DskImage {
  USHORT       Percent;      // percent completion
  BOOL         Busy;         // Busy flag
  CHAR         DriveID[1];   // Drive letter
  USHORT       FormatOptions;// Bit map indicating formatting choice
  CHAR         Volume[VOLUMENMSZ];    // Volume Name
  CHAR         BufferName[BUFFERNMSZ];// Image Buffer Name
  CHAR         FileName[FILENMSZ];    // Image file Name

  BSPBLK       DskParms;     // from sourceParms
  SEL          DskSel;       // selector for huge pointer to track buffer
  USHORT       SelOff;       // huge selector offset
  PTRACKLAYOUT DskLayout;    // from sourceLayout
  USHORT       usLayoutSize; // from sizeofLayoutElement
} DskImage;
