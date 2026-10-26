#ifndef WIN32_MACOS_SHIM_H
#define WIN32_MACOS_SHIM_H

#if defined(__APPLE__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. Win32 Primitive Types & Calling Conventions
 * ========================================================================= */
#ifndef WINAPI
#define WINAPI
#define CALLBACK
#define APIENTRY
#define PASCAL
#endif

typedef int            BOOL;
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

typedef uint8_t        BYTE;
typedef BYTE*          LPBYTE;
typedef uint16_t       WORD;
typedef uint32_t       DWORD;
typedef DWORD*         LPDWORD;
typedef unsigned int   UINT;
typedef int            INT;
typedef long           LONG;
typedef unsigned long  ULONG;
typedef int64_t        LONGLONG;
typedef uint64_t       ULONGLONG;
typedef intptr_t       LONG_PTR;
typedef uintptr_t      UINT_PTR;
typedef uintptr_t      ULONG_PTR;
typedef intptr_t       DWORD_PTR;
typedef intptr_t       LPARAM;
typedef uintptr_t      WPARAM;
typedef intptr_t       LRESULT;
typedef LONG           HRESULT;
#ifndef S_OK
#define S_OK           ((HRESULT)0L)
#define S_FALSE        ((HRESULT)1L)
#define SUCCEEDED(hr)  (((HRESULT)(hr)) >= 0)
#define FAILED(hr)     (((HRESULT)(hr)) < 0)
#endif
typedef intptr_t (*FARPROC)(void);
typedef void*          HKEY;
#define HKEY_CURRENT_USER ((HKEY)0x80000001)
#define KEY_READ       0x20019
#define ERROR_SUCCESS  0L
typedef WORD           ATOM;
typedef WORD*          LPWORD;
typedef short          SHORT;
typedef unsigned short USHORT;

typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
typedef GUID CLSID;
typedef GUID IID;

typedef char           CHAR;
typedef char*          LPSTR;
typedef const char*    LPCSTR;
typedef wchar_t*       LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef const wchar_t* LPCWCH;
typedef void*          LPVOID;
typedef const void*    LPCVOID;
typedef void*          PVOID;
typedef const char*    LPCCH;
typedef BOOL*          LPBOOL;
typedef void*          HDWP;

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

#define CP_ACP   0
#define CP_OEMCP 1
#define CP_UTF8  65001

/* Case-insensitive string functions for POSIX / macOS */
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

#ifndef _strdup
#define _strdup strdup
#endif

/* Generic Opaque Handles */
typedef void*          HANDLE;
typedef void*          HINSTANCE;
typedef void*          HMODULE;
typedef void*          HKEY;
typedef void*          HRSRC;
typedef void*          HGLOBAL;
typedef void*          HRGN;
typedef void*          HCURSOR;
typedef void*          HICON;
typedef void*          HACCEL;
typedef void*          HHOOK;

/* Specific Subsystem Handles */
typedef struct ZP_HWND__*    HWND;
typedef struct ZP_HDC__*     HDC;
typedef struct ZP_HBITMAP__* HBITMAP;
typedef struct ZP_HFONT__*   HFONT;
typedef struct ZP_HPEN__*    HPEN;
typedef struct ZP_HBRUSH__*  HBRUSH;
typedef struct ZP_HMENU__*   HMENU;
typedef void*                HGDIOBJ;

typedef uint32_t       COLORREF;

/* Macros for Colors and Coordinates */
#define RGB(r,g,b)          ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(rgb)      ((BYTE)(rgb))
#define GetGValue(rgb)      ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)      ((BYTE)((rgb)>>16))

#define LOWORD(l)           ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l)           ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define GET_X_LPARAM(lp)    ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)    ((int)(short)HIWORD(lp))
#define MAKELONG(a, b)      ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define MAKEWPARAM(l, h)    ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h)    ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE(i)  MAKEINTRESOURCEA(i)

#define RT_CURSOR           MAKEINTRESOURCE(1)
#define RT_BITMAP           MAKEINTRESOURCE(2)
#define RT_ICON             MAKEINTRESOURCE(3)
#define RT_MENU             MAKEINTRESOURCE(4)
#define RT_DIALOG           MAKEINTRESOURCE(5)
#define RT_STRING           MAKEINTRESOURCE(6)
#define RT_FONTDIR          MAKEINTRESOURCE(7)
#define RT_FONT             MAKEINTRESOURCE(8)
#define RT_ACCELERATOR      MAKEINTRESOURCE(9)
#define RT_RCDATA           MAKEINTRESOURCE(10)
#define RT_MESSAGETABLE     MAKEINTRESOURCE(11)
#define RT_GROUP_CURSOR     MAKEINTRESOURCE(12)
#define RT_GROUP_ICON       MAKEINTRESOURCE(14)
#define RT_VERSION          MAKEINTRESOURCE(16)
#define RT_DLGINCLUDE       MAKEINTRESOURCE(17)
#define RT_PLUGPLAY         MAKEINTRESOURCE(19)
#define RT_VXD              MAKEINTRESOURCE(20)
#define RT_ANICURSOR        MAKEINTRESOURCE(21)
#define RT_ANIICON          MAKEINTRESOURCE(22)
#define RT_HTML             MAKEINTRESOURCE(23)
#define RT_MANIFEST         MAKEINTRESOURCE(24)

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define CW_USEDEFAULT ((int)0x80000000)

/* =========================================================================
 * 2. Structures
 * ========================================================================= */
typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef void*          HMONITOR;
#define MONITOR_DEFAULTTONULL    0
#define MONITOR_DEFAULTTOPRIMARY  1
#define MONITOR_DEFAULTTONEAREST  2

typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT  rcMonitor;
    RECT  rcWork;
    DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;

typedef struct tagPAINTSTRUCT {
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT, *PPAINTSTRUCT, *LPPAINTSTRUCT;

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *PMSG, *LPMSG;

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT (CALLBACK *HOOKPROC)(int code, WPARAM wParam, LPARAM lParam);

typedef struct tagWNDCLASSEXA {
    UINT      cbSize;
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXA, *PWNDCLASSEXA, *LPWNDCLASSEXA;

typedef struct tagWNDCLASSA {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
} WNDCLASSA, *PWNDCLASSA, *LPWNDCLASSA;
typedef WNDCLASSA WNDCLASS;

typedef struct tagCREATESTRUCTA {
    LPVOID    lpCreateParams;
    HINSTANCE hInstance;
    HMENU     hMenu;
    HWND      hwndParent;
    int       cy;
    int       cx;
    int       y;
    int       x;
    LONG      style;
    LPCSTR    lpszName;
    LPCSTR    lpszClass;
    DWORD     dwExStyle;
} CREATESTRUCTA, *LPCREATESTRUCTA;
typedef CREATESTRUCTA CREATESTRUCT;

typedef struct tagCBT_CREATEWNDA {
    LPCREATESTRUCTA lpcs;
    HWND            hwndInsertAfter;
} CBT_CREATEWNDA, *LPCBT_CREATEWNDA;

typedef struct tagMENUBARINFO {
    DWORD cbSize;
    RECT  rcBar;
    HMENU hMenu;
    HWND  hwndMenu;
    BOOL  fBarFocused:1;
    BOOL  fFocused:1;
} MENUBARINFO, *PMENUBARINFO, *LPMENUBARINFO;

typedef struct tagMENUITEMINFOA {
    UINT      cbSize;
    UINT      fMask;
    UINT      fType;
    UINT      fState;
    UINT      wID;
    HMENU     hSubMenu;
    HBITMAP   hbmpChecked;
    HBITMAP   hbmpUnchecked;
    ULONG_PTR dwItemData;
    LPSTR     dwTypeData;
    UINT      cch;
    HBITMAP   hbmpItem;
} MENUITEMINFOA, *LPMENUITEMINFOA;
typedef MENUITEMINFOA MENUITEMINFO;
typedef const MENUITEMINFOA *LPCMENUITEMINFOA;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _WIN32_FIND_DATAA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    CHAR     cFileName[MAX_PATH];
    CHAR     cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

typedef struct tagTOOLINFOA {
    UINT      cbSize;
    UINT      uFlags;
    HWND      hwnd;
    UINT_PTR  uId;
    RECT      rect;
    HINSTANCE hinst;
    LPSTR     lpszText;
    LPARAM    lParam;
    void      *lpReserved;
} TOOLINFOA, *PTOOLINFOA, *LPTOOLINFOA;
typedef TOOLINFOA TOOLINFO;

typedef void* PIDLIST_ABSOLUTE;
typedef int (CALLBACK *BFFCALLBACK)(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);

typedef struct _browseinfoa {
    HWND        hwndOwner;
    void*       pidlRoot;
    LPSTR       pszDisplayName;
    LPCSTR      lpszTitle;
    UINT        ulFlags;
    BFFCALLBACK lpfn;
    LPARAM      lParam;
    int         iImage;
} BROWSEINFOA, *PBROWSEINFOA, *LPBROWSEINFOA;

#define CS_DBLCLKS               0x0008
#define CS_DROPSHADOW            0x00020000

#define TOOLTIPS_CLASS           "tooltips_class32"
#define TOOLTIPS_CLASSA          TOOLTIPS_CLASS
#define TTS_ALWAYSTIP            0x01
#define TTS_NOPREFIX             0x02
#define TTS_NOANIMATE            0x10
#define TTS_NOFADE               0x20
#define TTS_BALLOON              0x40
#define TTS_CLOSE                0x80

#define TTF_IDISHWND             0x0001
#define TTF_CENTERTIP            0x0002
#define TTF_RTLREADING           0x0004
#define TTF_SUBCLASS             0x0010
#define TTF_TRACK                0x0020
#define TTF_ABSOLUTE             0x0080
#define TTF_TRANSPARENT          0x0100
#define TTF_PARSELINKS           0x1000

#define TTM_ACTIVATE             (WM_USER + 1)
#define TTM_SETDELAYTIME         (WM_USER + 3)
#define TTM_ADDTOOLA             (WM_USER + 4)
#define TTM_ADDTOOLW             (WM_USER + 50)
#define TTM_DELTOOLA             (WM_USER + 5)
#define TTM_NEWTOOLRECTA         (WM_USER + 6)
#define TTM_RELAYEVENT           (WM_USER + 7)
#define TTM_GETTOOLINFOA         (WM_USER + 8)
#define TTM_SETTOOLINFOA         (WM_USER + 9)
#define TTM_HITTESTA             (WM_USER + 10)
#define TTM_UPDATETIPTEXTA       (WM_USER + 12)
#define TTM_GETTOOLCOUNT         (WM_USER + 13)
#define TTM_ENUMTOOLSA           (WM_USER + 14)
#define TTM_GETCURRENTTOOLA      (WM_USER + 15)
#define TTM_WINDOWFROMPOINT      (WM_USER + 16)
#define TTM_TRACKACTIVATE        (WM_USER + 17)
#define TTM_TRACKPOSITION        (WM_USER + 18)
#define TTM_SETTIPBKCOLOR        (WM_USER + 19)
#define TTM_SETTIPTEXTCOLOR      (WM_USER + 20)
#define TTM_GETDELAYTIME         (WM_USER + 21)
#define TTM_GETTIPBKCOLOR        (WM_USER + 22)
#define TTM_GETTIPTEXTCOLOR      (WM_USER + 23)
#define TTM_SETMAXTIPWIDTH       (WM_USER + 24)
#define TTM_GETMAXTIPWIDTH       (WM_USER + 25)
#define TTM_SETMARGIN            (WM_USER + 26)
#define TTM_GETMARGIN            (WM_USER + 27)
#define TTM_POP                  (WM_USER + 28)
#define TTM_UPDATE               (WM_USER + 29)
#define TTM_POPUP                (WM_USER + 34)

#define TTDT_AUTOMATIC           0
#define TTDT_RESHOW              1
#define TTDT_AUTOPOP             2
#define TTDT_INITIAL             3

#define BIF_RETURNONLYFSDIRS     0x00000001
#define BIF_DONTGOBELOWDOMAIN    0x00000002
#define BIF_STATUSTEXT           0x00000004
#define BIF_RETURNFSANCESTORS    0x00000008
#define BIF_EDITBOX              0x00000010
#define BIF_VALIDATE             0x00000020
#define BIF_NEWDIALOGSTYLE       0x00000040

typedef struct _SHFILEOPSTRUCTA {
    HWND         hwnd;
    UINT         wFunc;
    LPCSTR       pFrom;
    LPCSTR       pTo;
    UINT         fFlags;
    BOOL         fAnyOperationsAborted;
    LPVOID       hNameMappings;
    LPCSTR       lpszProgressTitle;
} SHFILEOPSTRUCTA, *LPSHFILEOPSTRUCTA;

#define FO_MOVE                  0x0001
#define FO_COPY                  0x0002
#define FO_DELETE                0x0003
#define FO_RENAME                0x0004

#define FOF_MULTIDESTFILES        0x0001
#define FOF_CONFIRMMOUSE          0x0002
#define FOF_SILENT                0x0004
#define FOF_RENAMEONCOLLISION     0x0008
#define FOF_NOCONFIRMATION        0x0010
#define FOF_WANTMAPPINGHANDLE     0x0020
#define FOF_ALLOWUNDO             0x0040
#define FOF_FILESONLY             0x0080
#define FOF_SIMPLEPROGRESS        0x0100
#define FOF_NOCONFIRMMKDIR        0x0200
#define FOF_NOERRORUI             0x0400
#define FOF_NOCOPYSECURITYATTRIBS 0x0800
#define FOF_NORECURSION           0x1000

typedef struct tagCOMBOBOXINFO {
    DWORD cbSize;
    RECT  rcItem;
    RECT  rcButton;
    DWORD stateButton;
    HWND  hwndCombo;
    HWND  hwndItem;
    HWND  hwndList;
} COMBOBOXINFO, *PCOMBOBOXINFO, *LPCOMBOBOXINFO;

typedef struct tagDRAWITEMSTRUCT {
    UINT      CtlType;
    UINT      CtlID;
    UINT      itemID;
    UINT      itemAction;
    UINT      itemState;
    HWND      hwndItem;
    HDC       hDC;
    RECT      rcItem;
    ULONG_PTR itemData;
} DRAWITEMSTRUCT, *PDRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

#define ODT_MENU     1
#define ODT_LISTBOX  2
#define ODT_COMBOBOX 3
#define ODT_BUTTON   4
#define ODT_STATIC   5

#define ODS_SELECTED     0x0001
#define ODS_GRAYED       0x0002
#define ODS_DISABLED     0x0004
#define ODS_CHECKED      0x0008
#define ODS_FOCUS        0x0010
#define ODS_DEFAULT      0x0020
#define ODS_HOTLIGHT     0x0040
#define ODS_INACTIVE     0x0080
#define ODS_NOACCEL      0x0100
#define ODS_COMBOBOXEDIT 0x1000

typedef struct tagMEASUREITEMSTRUCT {
    UINT      CtlType;
    UINT      CtlID;
    UINT      itemID;
    UINT      itemWidth;
    UINT      itemHeight;
    ULONG_PTR itemData;
} MEASUREITEMSTRUCT, *PMEASUREITEMSTRUCT, *LPMEASUREITEMSTRUCT;

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

#define BI_RGB 0L
#define DIB_RGB_COLORS 0

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

typedef struct tagBITMAP {
    LONG   bmType;
    LONG   bmWidth;
    LONG   bmHeight;
    LONG   bmWidthBytes;
    WORD   bmPlanes;
    WORD   bmBitsPixel;
    LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

typedef struct tagLOGPEN {
    UINT     lopnStyle;
    POINT    lopnWidth;
    COLORREF lopnColor;
} LOGPEN, *PLOGPEN, *LPLOGPEN;

typedef struct tagLOGBRUSH {
    UINT     lbStyle;
    COLORREF lbColor;
    ULONG_PTR lbHatch;
} LOGBRUSH, *PLOGBRUSH, *LPLOGBRUSH;

typedef struct tagLOGFONTA {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    char lfFaceName[32];
} LOGFONTA, *PLOGFONTA, *LPLOGFONTA;

typedef struct tagTEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA, *PTEXTMETRICA, *LPTEXTMETRICA;

typedef struct _BLENDFUNCTION {
    BYTE BlendOp;
    BYTE BlendFlags;
    BYTE SourceConstantAlpha;
    BYTE AlphaFormat;
} BLENDFUNCTION;

#define AC_SRC_OVER  0x00
#define AC_SRC_ALPHA 0x01

typedef struct tagTRACKMOUSEEVENT {
    DWORD cbSize;
    DWORD dwFlags;
    HWND  hwndTrack;
    DWORD dwHoverTime;
} TRACKMOUSEEVENT, *LPTRACKMOUSEEVENT;

#define TME_LEAVE 0x00000002

typedef struct tagACCEL {
    BYTE fVirt;
    WORD key;
    WORD cmd;
} ACCEL, *LPACCEL;

#define FVIRTKEY 0x01
#define FCONTROL 0x08
#define FSHIFT   0x04
#define FALT     0x10

typedef struct tagOPENFILENAMEA {
    DWORD         lStructSize;
    HWND          hwndOwner;
    HINSTANCE     hInstance;
    LPCSTR        lpstrFilter;
    LPSTR         lpstrCustomFilter;
    DWORD         nMaxCustFilter;
    DWORD         nFilterIndex;
    LPSTR         lpstrFile;
    DWORD         nMaxFile;
    LPSTR         lpstrFileTitle;
    DWORD         nMaxFileTitle;
    LPCSTR        lpstrInitialDir;
    LPCSTR        lpstrTitle;
    DWORD         Flags;
    WORD          nFileOffset;
    WORD          nFileExtension;
    LPCSTR        lpstrDefExt;
    LPARAM        lCustData;
    void*         lpfnHook;
    LPCSTR        lpTemplateName;
    void*         pvReserved;
    DWORD         dwReserved;
    DWORD         FlagsEx;
} OPENFILENAMEA, *LPOPENFILENAMEA;

#define OFN_EXPLORER             0x00080000
#define OFN_FILEMUSTEXIST        0x00001000
#define OFN_HIDEREADONLY         0x00000004
#define OFN_OVERWRITEPROMPT      0x00000002
#define OFN_PATHMUSTEXIST        0x00000800

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

/* =========================================================================
 * 3. Window Messages (WM_*)
 * ========================================================================= */
#define WM_NULL                  0x0000
#define WM_CREATE                0x0001
#define WM_DESTROY               0x0002
#define WM_MOVE                  0x0003
#define WM_SIZE                  0x0005
#define WM_ACTIVATE              0x0006
#define WM_SETFOCUS              0x0007
#define WM_KILLFOCUS             0x0008
#define WM_ENABLE                0x000A
#define WM_SETREDRAW             0x000B
#define WM_SETTEXT               0x000C
#define WM_GETTEXT               0x000D
#define WM_GETTEXTLENGTH         0x000E
#define WM_PAINT                 0x000F
#define WM_CLOSE                 0x0010
#define WM_QUIT                  0x0012
#define WM_ERASEBKGND            0x0014
#define WM_SHOWWINDOW            0x0018
#define WM_CANCELMODE            0x001F
#define WM_SETCURSOR             0x0020
#define WM_DRAWITEM              0x002B
#define WM_MEASUREITEM           0x002C
#define WM_SETFONT               0x0030
#define WM_GETFONT               0x0031
#define WM_SETICON               0x0080
#define WM_NCDESTROY             0x0082
#define WM_NCCALCSIZE            0x0083
#define WM_NCHITTEST             0x0084
#define WM_NCPAINT               0x0085
#define WM_NCACTIVATE            0x0086
#define WM_KEYDOWN               0x0100
#define WM_KEYUP                 0x0101
#define WM_CHAR                  0x0102
#define WM_SYSKEYDOWN            0x0104
#define WM_SYSKEYUP              0x0105
#define WM_COMMAND               0x0111
#define WM_SYSCOMMAND            0x0112
#define WM_TIMER                 0x0113
#define WM_HSCROLL               0x0114
#define WM_VSCROLL               0x0115
#define WM_INITMENU              0x0116
#define WM_INITMENUPOPUP         0x0117
#define WM_MENUCHAR              0x0120
#define WM_ENTERMENULOOP         0x0121
#define WM_EXITMENULOOP          0x0122
#define WM_CTLCOLORMSGBOX        0x0132
#define WM_CTLCOLOREDIT          0x0133
#define WM_CTLCOLORLISTBOX       0x0134
#define WM_CTLCOLORBTN           0x0135
#define WM_CTLCOLORDLG           0x0136
#define WM_CTLCOLORSTATIC        0x0138
#define WM_MOUSEMOVE             0x0200
#define WM_LBUTTONDOWN           0x0201
#define WM_LBUTTONUP             0x0202
#define WM_LBUTTONDBLCLK         0x0203
#define WM_RBUTTONDOWN           0x0204
#define WM_RBUTTONUP             0x0205
#define WM_MBUTTONDOWN           0x0207
#define WM_MBUTTONUP             0x0208
#define WM_MOUSEWHEEL            0x020A
#define WM_CAPTURECHANGED        0x0215
#define WM_MOUSELEAVE            0x02A3
#define WM_PASTE                 0x0302
#define WM_PRINT                 0x0317
#define WM_PRINTCLIENT           0x0318
#define WM_THEMECHANGED          0x031A
#define WM_USER                  0x0400

#define PRF_CHECKVISIBLE         0x00000001L
#define PRF_NONCLIENT            0x00000002L
#define PRF_CLIENT               0x00000004L
#define PRF_ERASEBKGND           0x00000008L
#define PRF_CHILDREN             0x00000010L
#define PRF_OWNED                0x00000020L

/* Dark Menu Hooks for Windows compatibility */
#define WM_UAHDRAWMENU           0x0091
#define WM_UAHDRAWMENUITEM       0x0092
#define WM_UAHMEASUREMENUITEM    0x0093

#define WA_INACTIVE              0
#define WA_ACTIVE                1

/* Icon constants */
#define ICON_SMALL 0
#define ICON_BIG   1
#define IMAGE_ICON 1
#define LR_DEFAULTCOLOR 0x0000
#define LR_LOADFROMFILE 0x0010

/* =========================================================================
 * 4. Window Styles, Control Styles & Virtual Keys
 * ========================================================================= */
#define WS_OVERLAPPED            0x00000000L
#define WS_POPUP                 0x80000000L
#define WS_CHILD                 0x40000000L
#define WS_MINIMIZE              0x20000000L
#define WS_VISIBLE               0x10000000L
#define WS_DISABLED              0x08000000L
#define WS_CLIPSIBLINGS          0x04000000L
#define WS_CLIPCHILDREN          0x02000000L
#define WS_MAXIMIZE              0x01000000L
#define WS_CAPTION               0x00C00000L
#define WS_BORDER                0x00800000L
#define WS_DLGFRAME              0x00400000L
#define WS_VSCROLL               0x00200000L
#define WS_HSCROLL               0x00100000L
#define WS_SYSMENU               0x00080000L
#define WS_THICKFRAME            0x00040000L
#define WS_TABSTOP               0x00010000L
#define WS_OVERLAPPEDWINDOW      (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_MINIMIZEBOX           0x00020000L
#define WS_MAXIMIZEBOX           0x00010000L

#define WS_EX_DLGMODALFRAME      0x00000001L
#define WS_EX_CLIENTEDGE         0x00000200L
#define WS_EX_TOOLWINDOW         0x00000080L
#define WS_EX_TOPMOST            0x00000008L
#define WS_EX_NOACTIVATE         0x08000000L

/* Button Styles */
#define BS_PUSHBUTTON            0x00000000L
#define BS_DEFPUSHBUTTON         0x00000001L
#define BS_CHECKBOX              0x00000002L
#define BS_AUTOCHECKBOX          0x00000003L
#define BS_RADIOBUTTON           0x00000004L
#define BS_AUTORADIOBUTTON       0x00000009L
#define BS_OWNERDRAW             0x0000000BL

#define BM_GETCHECK              0x00F0
#define BM_SETCHECK              0x00F1
#define BST_UNCHECKED            0x0000
#define BST_CHECKED              0x0001
#define BN_CLICKED               0

/* Edit Styles */
#define ES_LEFT                  0x0000L
#define ES_CENTER                0x0001L
#define ES_RIGHT                 0x0002L
#define ES_AUTOHSCROLL           0x0080L
#define ES_READONLY              0x0800L
#define ES_NUMBER                0x2000L

#define EM_SETSEL                0x00B1
#define EM_GETSEL                0x00B0
#define EM_POSFROMCHAR           0x00D6
#define EM_CHARFROMPOS           0x00D7

#define EN_CHANGE                0x0300
#define EM_SETSEL                0x00B1
#define EM_REPLACESEL            0x00C2

/* Static Styles */
#define SS_LEFT                  0x00000000L
#define SS_CENTER                0x00010000L
#define SS_RIGHT                 0x00020000L
#define SS_OWNERDRAW             0x0000000DL
#define SS_CENTERIMAGE           0x00000200L

/* Listbox Styles & Messages */
#define LBS_NOTIFY               0x0001L
#define LBS_OWNERDRAWFIXED       0x0010L
#define LBS_HASSTRINGS           0x0040L
#define LBS_NOINTEGRALHEIGHT     0x0100L

#define LB_ADDSTRING             0x0180
#define LB_RESETCONTENT          0x0184
#define LB_SETCURSEL             0x0186
#define LB_GETCURSEL             0x0188
#define LB_GETTEXT               0x0189
#define LB_GETTEXTLEN            0x018A
#define LB_GETCOUNT              0x018B
#define LB_GETITEMDATA           0x0199
#define LB_SETITEMDATA           0x019A
#define LB_SETITEMHEIGHT         0x01A0
#define LB_FINDSTRINGEXACT       0x01A2
#define LB_ITEMFROMPOINT         0x01A9
#define LB_ERR                   (-1)

#define LBN_SELCHANGE            1
#define LBN_DBLCLK               2

/* Combobox Styles & Messages */
#define CBS_DROPDOWNLIST         0x0003L
#define CBS_OWNERDRAWFIXED       0x0010L
#define CBS_HASSTRINGS           0x0020L

#define CB_ADDSTRING             0x0143
#define CB_SETCURSEL             0x014E
#define CB_GETCURSEL             0x0147
#define CB_GETLBTEXT             0x0148
#define CB_GETCOUNT              0x0146
#define CB_RESETCONTENT          0x014B
#define CB_SETDROPPEDWIDTH       0x0160
#define CB_SHOWDROPDOWN          0x014F
#define CB_ERR                   (-1)

#define CBN_SELCHANGE            1

/* Virtual Keys (VK_*) */
#define VK_BACK                  0x08
#define VK_TAB                   0x09
#define VK_RETURN                0x0D
#define VK_SHIFT                 0x10
#define VK_CONTROL               0x11
#define VK_MENU                  0x12  /* Alt */
#define VK_ESCAPE                0x1B
#define VK_SPACE                 0x20
#define VK_LEFT                  0x25
#define VK_UP                    0x26
#define VK_RIGHT                 0x27
#define VK_DOWN                  0x28
#define VK_INSERT                0x2D
#define VK_DELETE                0x2E
#define VK_ADD                   0x6B
#define VK_SUBTRACT              0x6D
#define VK_F1                    0x70
#define VK_F2                    0x71
#define VK_OEM_PLUS              0xBB
#define VK_OEM_COMMA             0xBC
#define VK_OEM_MINUS             0xBD
#define VK_OEM_4                 0xDB  /* '[' */
#define VK_OEM_6                 0xDD  /* ']' */

/* DrawText (DT_*) */
#define DT_TOP                   0x00000000
#define DT_LEFT                  0x00000000
#define DT_CENTER                0x00000001
#define DT_RIGHT                 0x00000002
#define DT_VCENTER               0x00000004
#define DT_BOTTOM                0x00000008
#define DT_WORDBREAK             0x00000010
#define DT_SINGLELINE            0x00000020
#define DT_CALCRECT              0x00000400
#define DT_NOPREFIX              0x00000800
#define DT_END_ELLIPSIS          0x00008000
#define DT_PATH_ELLIPSIS         0x00004000
#define DT_HIDEPREFIX            0x00100000

/* Cursor Identifiers (IDC_*) */
#define IDC_ARROW                ((LPCSTR)32512)
#define IDC_IBEAM                ((LPCSTR)32513)
#define IDC_CROSS                ((LPCSTR)32515)
#define IDC_SIZEALL              ((LPCSTR)32646)
#define IDC_SIZENWSE             ((LPCSTR)32642)
#define IDC_SIZENESW             ((LPCSTR)32643)
#define IDC_SIZEWE               ((LPCSTR)32644)
#define IDC_SIZENS               ((LPCSTR)32645)
#define IDC_HAND                 ((LPCSTR)32649)

/* ShowWindow (SW_*) */
#define SW_HIDE                  0
#define SW_SHOWNORMAL            1
#define SW_SHOW                  5
#define SW_SHOWNOACTIVATE        4

/* SetWindowPos Flags (SWP_*) */
#define SWP_NOSIZE               0x0001
#define SWP_NOMOVE               0x0002
#define SWP_NOZORDER             0x0004
#define SWP_NOREDRAW             0x0008
#define SWP_NOACTIVATE           0x0010
#define SWP_FRAMECHANGED         0x0020
#define SWP_SHOWWINDOW           0x0040
#define SWP_HIDEWINDOW           0x0080
#define SWP_NOCOPYBITS           0x0100
#define SWP_NOOWNERZORDER        0x0200
#define SWP_NOSENDCHANGING       0x0400

/* Window Z-Order Handles */
#define HWND_TOP                 ((HWND)0)
#define HWND_BOTTOM              ((HWND)1)
#define HWND_TOPMOST             ((HWND)-1)
#define HWND_NOTOPMOST           ((HWND)-2)

/* RedrawWindow Flags (RDW_*) */
#define RDW_INVALIDATE           0x0001
#define RDW_INTERNALPAINT        0x0002
#define RDW_ERASE                0x0004
#define RDW_VALIDATE             0x0008
#define RDW_NOINTERNALPAINT      0x0010
#define RDW_NOERASE              0x0020
#define RDW_NOCHILDREN           0x0040
#define RDW_ALLCHILDREN          0x0080
#define RDW_UPDATENOW            0x0100
#define RDW_ERASENOW             0x0200
#define RDW_FRAME                0x0400
#define RDW_NOFRAME              0x0800

/* MessageBox (MB_*) */
#define MB_OK                    0x00000000L
#define MB_ICONERROR             0x00000010L
#define MB_ICONWARNING           0x00000030L
#define MB_ICONINFORMATION       0x00000040L
#define MB_TASKMODAL             0x00002000L

/* Dialog Return Codes (ID*) */
#define IDOK                     1
#define IDCANCEL                 2
#define IDABORT                  3
#define IDRETRY                  4
#define IDIGNORE                 5
#define IDYES                    6
#define IDNO                     7

/* System Metrics (SM_*) */
#define SM_CXSCREEN              0
#define SM_CYSCREEN              1

/* Mouse / Keyboard WPARAM unpackers */
#define WHEEL_DELTA              120
#define GET_WHEEL_DELTA_WPARAM(wParam)  ((short)HIWORD(wParam))
#define GET_KEYSTATE_WPARAM(wParam)     (LOWORD(wParam))

/* Mouse Key State Flags (MK_*) */
#define MK_LBUTTON               0x0001
#define MK_RBUTTON               0x0002
#define MK_SHIFT                 0x0004
#define MK_CONTROL               0x0008
#define MK_MBUTTON               0x0010
#define MK_XBUTTON1              0x0020
#define MK_XBUTTON2              0x0040

/* System Commands (SC_*) */
#define SC_SIZE                  0xF000
#define SC_MOVE                  0xF010
#define SC_MINIMIZE              0xF020
#define SC_MAXIMIZE              0xF030
#define SC_NEXTWINDOW            0xF040
#define SC_PREVWINDOW            0xF050
#define SC_CLOSE                 0xF060
#define SC_RESTORE               0xF120

/* Menu Flags (MF_*) */
#define MF_STRING                0x00000000L
#define MF_ENABLED               0x00000000L
#define MF_GRAYED                0x00000001L
#define MF_UNCHECKED             0x00000000L
#define MF_CHECKED               0x00000008L
#define MF_POPUP                 0x00000010L
#define MF_SEPARATOR             0x00000800L
#define MF_OWNERDRAW             0x00000100L
#define MF_BYCOMMAND             0x00000000L
#define MF_BYPOSITION            0x00000400L

#define TPM_RIGHTBUTTON          0x0002L
#define TPM_RETURNCMD            0x0100L
#define TPM_NONOTIFY             0x0080L

#define OBJID_MENU               ((LONG)0xFFFFFFFD)

/* Menu Item Flags & IDs */
#define MIIM_STATE               0x00000001
#define MIIM_ID                  0x00000002
#define MIIM_SUBMENU             0x00000004
#define MIIM_CHECKMARKS          0x00000008
#define MIIM_TYPE                0x00000010
#define MIIM_DATA                0x00000020
#define MIIM_STRING              0x00000040
#define MIIM_BITMAP              0x00000080
#define MIIM_FTYPE               0x00000100

#define MFT_STRING               0x00000000
#define MFT_BITMAP               0x00000004
#define MFT_MENUBARBREAK         0x00000020
#define MFT_MENUBREAK            0x00000040
#define MFT_OWNERDRAW            0x00000100
#define MFT_RADIOCHECK           0x00000200
#define MFT_SEPARATOR            0x00000800
#define MFT_RIGHTORDER           0x00002000
#define MFT_RIGHTJUSTIFY         0x00004000

#define MAKELRESULT(l, h)        ((LRESULT)(DWORD)MAKELONG(l, h))
#define MNC_IGNORE               0
#define MNC_CLOSE                1
#define MNC_EXECUTE              2
#define MNC_SELECT               3

/* GDI Pen Styles (PS_*) */
#define PS_SOLID                 0
#define PS_DASH                  1
#define PS_DOT                   2
#define PS_DASHDOT               3
#define PS_DASHDOTDOT            4
#define PS_NULL                  5
#define PS_INSIDEFRAME           6
#define PS_USERSTYLE             7
#define PS_ALTERNATE             8
#define PS_GEOMETRIC             0x00010000
#define PS_ENDCAP_ROUND          0x00000000
#define PS_ENDCAP_SQUARE         0x00000100
#define PS_ENDCAP_FLAT           0x00000200
#define PS_JOIN_ROUND            0x00000000
#define PS_JOIN_BEVEL            0x00001000
#define PS_JOIN_MITER            0x00002000

/* ROP2 Modes (R2_*) */
#define R2_NOTXORPEN             10
#define R2_COPYPEN               13

/* GDI Modes & Stock Objects */
#define TRANSPARENT              1
#define OPAQUE                   2
#define SRCCOPY                  (DWORD)0x00CC0020
#define SRCPAINT                 (DWORD)0x00EE0086
#define SRCAND                   (DWORD)0x008800C6
#define SRCINVERT                (DWORD)0x00660046
#define WHITENESS                (DWORD)0x00FF0062
#define BLACKNESS                (DWORD)0x00000042

#define WHITE_BRUSH              0
#define BLACK_BRUSH              4
#define NULL_BRUSH               5
#define HOLLOW_BRUSH             NULL_BRUSH
#define NULL_PEN                 8
#define DEFAULT_GUI_FONT         17

#define OBJ_PEN                  1
#define OBJ_BRUSH                2
#define OBJ_FONT                 6
#define OBJ_BITMAP               7

#define BLACKONWHITE             1
#define WHITEONBLACK             2
#define COLORONCOLOR             3
#define HALFTONE                 4
#define STRETCH_ANDSCANS         BLACKONWHITE
#define STRETCH_ORSCANS          WHITEONBLACK
#define STRETCH_DELETESCANS      COLORONCOLOR
#define STRETCH_HALFTONE         HALFTONE

#define RGN_AND                  1
#define RGN_OR                   2
#define RGN_XOR                  3
#define RGN_DIFF                 4
#define RGN_COPY                 5

#define FW_NORMAL                400
#define FW_BOLD                  700
#define DEFAULT_CHARSET          1
#define OUT_DEFAULT_PRECIS       0
#define CLIP_DEFAULT_PRECIS      0
#define DEFAULT_QUALITY          0
#define DEFAULT_PITCH            0

#define GWLP_WNDPROC             (-4)
#define GWLP_USERDATA            (-21)
#define HTCLIENT                 1
#define GA_ROOT                  2

/* Status Bar Messages */
#define STATUSCLASSNAME          "msctls_statusbar32"
#define SB_SETPARTS              (WM_USER + 4)
#define SB_SETTEXTA              (WM_USER + 1)
#define SB_SETTEXTW              (WM_USER + 11)
#define SB_GETTEXTA              (WM_USER + 2)
#define SB_GETTEXTLENGTHA        (WM_USER + 3)

#define SBT_OWNERDRAW            0x1000
#define SBT_NOBORDERS            0x0100
#define SBT_POPOUT               0x0200
#define SBT_RTLREADING           0x0400

/* CBT Hook constants */
#define WH_CBT                   5
#define HCBT_CREATEWND           3
#define HCBT_ACTIVATE            5
#define HCBT_SETFOCUS            9

/* =========================================================================
 * 5. Transparent Path Normalization & File I/O
 * ========================================================================= */
FILE* zp_macos_fopen(const char *path, const char *mode);
#define fopen(path, mode) zp_macos_fopen(path, mode)

DWORD zp_macos_get_file_attributes_a(LPCSTR path);
#define GetFileAttributesA(p) zp_macos_get_file_attributes_a(p)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010

BOOL zp_macos_create_directory_a(LPCSTR path, void *sa);
#define CreateDirectoryA(p, sa) zp_macos_create_directory_a(p, sa)

BOOL zp_macos_delete_file_a(LPCSTR path);
#define DeleteFileA(p) zp_macos_delete_file_a(p)

BOOL zp_macos_copy_file_a(LPCSTR src, LPCSTR dst, BOOL fail_if_exists);
#define CopyFileA(s, d, f) zp_macos_copy_file_a(s, d, f)

BOOL zp_macos_move_file_ex_a(LPCSTR src, LPCSTR dst, DWORD flags);
#define MoveFileExA(s, d, f) zp_macos_move_file_ex_a(s, d, f)
#define MoveFileA(s, d)      zp_macos_move_file_ex_a(s, d, 0)
#define MoveFile             MoveFileA
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED     0x00000002

/* Geometry / RECT Helper Functions */
BOOL SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom);
BOOL OffsetRect(LPRECT lprc, int dx, int dy);
BOOL InflateRect(LPRECT lprc, int dx, int dy);
BOOL PtInRect(const RECT *lprc, POINT pt);
BOOL IntersectRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2);
BOOL UnionRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2);
BOOL EqualRect(const RECT *lprc1, const RECT *lprc2);
BOOL IsRectEmpty(const RECT *lprc);
int  MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints);

/* =========================================================================
 * 6. GDI Functions Prototypes
 * ========================================================================= */
HDC     CreateCompatibleDC(HDC hdc);
BOOL    DeleteDC(HDC hdc);
HBITMAP CreateCompatibleBitmap(HDC hdc, int cx, int cy);
HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, void **ppvBits, HANDLE hSection, DWORD offset);
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h);
BOOL    DeleteObject(HGDIOBJ ho);
HGDIOBJ GetStockObject(int i);
HGDIOBJ GetCurrentObject(HDC hdc, UINT type);
int     GetObjectA(HGDIOBJ hgdiobj, int cbBuffer, LPVOID lpvObject);
#define GetObject GetObjectA

HPEN    CreatePen(int iStyle, int cWidth, COLORREF color);
HPEN    CreatePenIndirect(const LOGPEN *plp);
HPEN    ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH *plbrush, DWORD cStyle, const DWORD *pstyle);
HBRUSH  CreateSolidBrush(COLORREF color);
HFONT   CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
                    DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
                    DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
                    DWORD iPitchAndFamily, LPCSTR pszFaceName);

COLORREF SetTextColor(HDC hdc, COLORREF color);
COLORREF SetBkColor(HDC hdc, COLORREF color);
int      SetBkMode(HDC hdc, int mode);
BOOL     SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt);
int      SetROP2(HDC hdc, int rop2);

HRGN     CreateRectRgn(int x1, int y1, int x2, int y2);
int      SelectClipRgn(HDC hdc, HRGN hrgn);
int      CombineRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, int iMode);

BOOL    BitBlt(HDC hdcDest, int xDest, int yDest, int w, int h, HDC hdcSrc, int xSrc, int ySrc, DWORD rop);
BOOL    StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                   HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop);
BOOL    AlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest,
                   HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc,
                   BLENDFUNCTION ftn);
#define GdiAlphaBlend AlphaBlend
int     SetStretchBltMode(HDC hdc, int mode);

BOOL    MoveToEx(HDC hdc, int x, int y, LPPOINT lppt);
BOOL    LineTo(HDC hdc, int x, int y);
BOOL    Polyline(HDC hdc, const POINT *apt, int cpt);
BOOL    Rectangle(HDC hdc, int left, int top, int right, int bottom);
BOOL    RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height);
BOOL    Ellipse(HDC hdc, int left, int top, int right, int bottom);
BOOL    Polygon(HDC hdc, const POINT *apt, int cpt);
BOOL    PolyBezier(HDC hdc, const POINT *apt, DWORD cpt);
BOOL    PolyBezierTo(HDC hdc, const POINT *apt, DWORD cpt);
COLORREF SetPixel(HDC hdc, int x, int y, COLORREF color);
COLORREF GetPixel(HDC hdc, int x, int y);
int     GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage);
int     FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr);
int     FrameRect(HDC hDC, const RECT *lprc, HBRUSH hbr);
int     ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom);

int     DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format);
int     DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format);
BOOL    GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE lpsz);
BOOL    GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE lpsz);
BOOL    GetTextMetricsA(HDC hdc, LPTEXTMETRICA lptm);

HANDLE  AddFontMemResourceEx(void *pFileView, DWORD cjSize, void *pvResrved, DWORD *pNumFonts);
BOOL    RemoveFontMemResourceEx(HANDLE h);

#if !defined(__OBJC__)
#ifndef __CGIMAGE_H__
typedef struct CGImage *CGImageRef;
#endif
#ifndef __CGCONTEXT_H__
typedef struct CGContext *CGContextRef;
#endif
#endif
CGImageRef   zp_macos_create_hdc_image(HDC hdc);
CGContextRef zp_macos_get_hdc_cgcontext(HDC hdc);

/* =========================================================================
 * 7. User32 Windowing & Control Functions Prototypes
 * ========================================================================= */
ATOM      RegisterClassExA(const WNDCLASSEXA *unnamedParam1);
ATOM      RegisterClassA(const WNDCLASSA *lpWndClass);
#define   RegisterClass RegisterClassA
HWND      CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
                          DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
                          HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
BOOL      DestroyWindow(HWND hWnd);
BOOL      ShowWindow(HWND hWnd, int nCmdShow);
BOOL      UpdateWindow(HWND hWnd);
BOOL      InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
BOOL      RedrawWindow(HWND hWnd, const RECT *lprcUpdate, HRGN hrgnUpdate, UINT flags);
BOOL      GetClientRect(HWND hWnd, LPRECT lpRect);
BOOL      GetWindowRect(HWND hWnd, LPRECT lpRect);
BOOL      ScreenToClient(HWND hWnd, LPPOINT lpPoint);
BOOL      ClientToScreen(HWND hWnd, LPPOINT lpPoint);
BOOL      MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
BOOL      SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
HDWP      BeginDeferWindowPos(int nNumWindows);
HDWP      DeferWindowPos(HDWP hWinPosInfo, HWND hWnd, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT uFlags);
BOOL      EndDeferWindowPos(HDWP hWinPosInfo);
BOOL      IsWindow(HWND hWnd);
BOOL      IsZoomed(HWND hWnd);
BOOL      IsIconic(HWND hWnd);
HWND      WindowFromPoint(POINT Point);
HWND      GetDesktopWindow(void);

LRESULT   DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT   CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT   SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL      PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void      PostQuitMessage(int nExitCode);

BOOL      GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
BOOL      PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001
BOOL      TranslateMessage(const MSG *lpMsg);
LRESULT   DispatchMessageA(const MSG *lpMsg);

HDC       GetDC(HWND hWnd);
HDC       GetWindowDC(HWND hWnd);
int       ReleaseDC(HWND hWnd, HDC hDC);
HDC       BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint);
BOOL      EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint);

LONG_PTR  SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
LONG_PTR  GetWindowLongPtrA(HWND hWnd, int nIndex);
BOOL      SetWindowTextA(HWND hWnd, LPCSTR lpString);
int       GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount);
int       GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount);
int       GetWindowTextLengthA(HWND hWnd);
int       GetClassNameA(HWND hWnd, LPSTR lpClassName, int nMaxCount);
DWORD     GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId);
DWORD     GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
HMODULE   GetModuleHandleA(LPCSTR lpModuleName);
#define   GetModuleHandle GetModuleHandleA
HMODULE   LoadLibraryA(LPCSTR lpLibFileName);
#define   LoadLibrary LoadLibraryA
FARPROC   GetProcAddress(HMODULE hModule, LPCSTR lpProcName);

BOOL      SetPropA(HWND hWnd, LPCSTR lpString, HANDLE hData);
HANDLE    GetPropA(HWND hWnd, LPCSTR lpString);
HANDLE    RemovePropA(HWND hWnd, LPCSTR lpString);
#define   SetProp SetPropA
#define   GetProp GetPropA
#define   RemoveProp RemovePropA

HWND      FindWindowExA(HWND hWndParent, HWND hWndChildAfter, LPCSTR lpszClass, LPCSTR lpszWindow);
#define   FindWindowEx FindWindowExA

HWND      SetFocus(HWND hWnd);
HWND      GetFocus(void);
HWND      SetCapture(HWND hWnd);
BOOL      ReleaseCapture(void);
HWND      GetCapture(void);
BOOL      EnableWindow(HWND hWnd, BOOL bEnable);
BOOL      IsWindowEnabled(HWND hWnd);
BOOL      IsWindowVisible(HWND hWnd);
HWND      GetParent(HWND hWnd);
HWND      GetAncestor(HWND hWnd, UINT gaFlags);
HWND      GetDlgItem(HWND hDlg, int nIDDlgItem);
int       GetDlgCtrlID(HWND hWnd);
UINT      IsDlgButtonChecked(HWND hDlg, int nIDButton);
BOOL      CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck);
BOOL      GetComboBoxInfo(HWND hwndCombo, PCOMBOBOXINFO pcbi);

UINT_PTR  SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, void *lpTimerFunc);
BOOL      KillTimer(HWND hWnd, UINT_PTR uIDEvent);

BOOL      TrackMouseEvent(LPTRACKMOUSEEVENT lpEventTrack);
HCURSOR   SetCursor(HCURSOR hCursor);
HCURSOR   LoadCursor(HINSTANCE hInstance, LPCSTR lpCursorName);
HICON     LoadImageA(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad);
BOOL      GetCursorPos(LPPOINT lpPoint);
SHORT     GetAsyncKeyState(int vKey);
SHORT     GetKeyState(int vKey);

int       GetSystemMetrics(int nIndex);
BOOL      AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
HMONITOR  MonitorFromPoint(POINT pt, DWORD dwFlags);
HMONITOR  MonitorFromWindow(HWND hwnd, DWORD dwFlags);
BOOL      GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi);
#define   GetMonitorInfo GetMonitorInfoA

HMENU     CreateMenu(void);
HMENU     CreatePopupMenu(void);
BOOL      DestroyMenu(HMENU hMenu);
BOOL      IsMenu(HMENU hMenu);
BOOL      AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem);
BOOL      EnableMenuItem(HMENU hMenu, UINT uIDEnableItem, UINT uEnable);
BOOL      CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck);
HMENU     GetSubMenu(HMENU hMenu, int nPos);
HMENU     GetMenu(HWND hWnd);
BOOL      SetMenu(HWND hWnd, HMENU hMenu);
BOOL      DrawMenuBar(HWND hWnd);
BOOL      TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT *prcRect);
int       GetMenuItemCount(HMENU hMenu);
UINT      GetMenuItemID(HMENU hMenu, int nPos);
int       GetMenuStringA(HMENU hMenu, UINT uIDItem, LPSTR lpString, int cchMax, UINT flags);
BOOL      DeleteMenu(HMENU hMenu, UINT uPosition, UINT uFlags);
BOOL      RemoveMenu(HMENU hMenu, UINT uPosition, UINT uFlags);
BOOL      InsertMenuA(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem);
BOOL      ModifyMenuA(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem);
UINT      GetMenuState(HMENU hMenu, UINT uId, UINT uFlags);
BOOL      GetMenuBarInfo(HWND hwnd, LONG idObject, LONG idItem, PMENUBARINFO pmbi);
BOOL      GetMenuItemInfoA(HMENU hMenu, UINT item, BOOL fByPosition, LPMENUITEMINFOA lpmii);
#define   GetMenuItemInfo GetMenuItemInfoA

/* Accelerators */
HACCEL    CreateAcceleratorTableA(LPACCEL paccel, int cAccel);
int       TranslateAcceleratorA(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg);
BOOL      DestroyAcceleratorTable(HACCEL hAccel);

/* Hooks */
HHOOK     SetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId);
BOOL      UnhookWindowsHookEx(HHOOK hhk);
LRESULT   CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);

/* Multimedia timers */
UINT      timeBeginPeriod(UINT uPeriod);
UINT      timeEndPeriod(UINT uPeriod);

/* Common Dialogs */
int       MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
BOOL      GetOpenFileNameA(LPOPENFILENAMEA ofn);
BOOL      GetSaveFileNameA(LPOPENFILENAMEA ofn);
BOOL      MessageBeep(UINT uType);

/* Resources */
HRSRC     FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
HGLOBAL   LoadResource(HMODULE hModule, HRSRC hResInfo);
LPVOID    LockResource(HGLOBAL hResData);
DWORD     SizeofResource(HMODULE hModule, HRSRC hResInfo);
LPVOID    GlobalLock(HGLOBAL hMem);
BOOL      GlobalUnlock(HGLOBAL hMem);

/* Threading, Sync & COM */
#define INFINITE                 ((DWORD)0xFFFFFFFF)
#define COINIT_MULTITHREADED     0
#define COINIT_APARTMENTTHREADED 2

typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);

HANDLE    CreateThread(void *lpThreadAttributes, size_t dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
DWORD     WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
BOOL      CloseHandle(HANDLE hObject);
BOOL      SetForegroundWindow(HWND hWnd);
HRESULT   CoInitialize(LPVOID pvReserved);
HRESULT   CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
void      CoUninitialize(void);
int       SHFileOperationA(LPSHFILEOPSTRUCTA lpFileOp);
#define   SHFileOperation SHFileOperationA
HRESULT   SetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);
DWORD     GetLastError(void);
void      SetLastError(DWORD dwErrCode);

DWORD     GetTickCount(void);
DWORD     timeGetTime(void);
DWORD     GetCurrentThreadId(void);
void      InitCommonControls(void);
HRESULT   DwmSetWindowAttribute(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);
int       WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar);
int       MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
void      GetLocalTime(LPSYSTEMTIME lpSystemTime);
BOOL      OpenClipboard(HWND hWndNewOwner);
BOOL      CloseClipboard(void);
HANDLE    GetClipboardData(UINT uFormat);
#define CF_TEXT 1

BOOL      ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);

/* Directory & File Search */
HANDLE    FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
BOOL      FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
BOOL      FindClose(HANDLE hFindFile);

PIDLIST_ABSOLUTE SHBrowseForFolderA(LPBROWSEINFOA lpbi);
BOOL      SHGetPathFromIDListA(PIDLIST_ABSOLUTE pidl, LPSTR pszPath);
void      CoTaskMemFree(LPVOID pv);

/* macOS Native Image & Folder Operations */
BOOL      zp_macos_decode_image_file(const char *path, int *out_w, int *out_h, uint32_t **out_bgra);
BOOL      zp_macos_decode_image_memory(const void *buf, size_t len, int *out_w, int *out_h, uint32_t **out_bgra);
BOOL      zp_macos_encode_image_file(const char *path, int w, int h, const uint32_t *bgra, int format_type);
BOOL      zp_macos_pick_folder(HWND hwndParent, char *out_path, size_t max_len);
void      zp_macos_install_crash_handler(void);

/* =========================================================================
 * 8. Convenience Macro Aliases
 * ========================================================================= */
#define SendMessage          SendMessageA
#define PostMessage          PostMessageA
#define GetMessage           GetMessageA
#define PeekMessage          PeekMessageA
#define DispatchMessage      DispatchMessageA
#define TranslateMessage     TranslateMessage
#define DefWindowProc        DefWindowProcA
#define CallWindowProc       CallWindowProcA
#define SetWindowLongPtr     SetWindowLongPtrA
#define GetWindowLongPtr     GetWindowLongPtrA
#define SetWindowText        SetWindowTextA
#define GetWindowText        GetWindowTextA
#define GetWindowTextLength  GetWindowTextLengthA
#define DrawText             DrawTextA
#define GetTextExtentPoint32 GetTextExtentPoint32A
#define GetClassName         GetClassNameA
#define LoadImage            LoadImageA
#define RegisterClassEx      RegisterClassExA
#define CreateWindowA(cls, name, style, x, y, w, h, parent, menu, inst, param) \
    CreateWindowExA(0, cls, name, style, x, y, w, h, parent, menu, inst, param)
#define CreateWindow(cls, name, style, x, y, w, h, parent, menu, inst, param) \
    CreateWindowExA(0, cls, name, style, x, y, w, h, parent, menu, inst, param)

#ifdef __cplusplus
}
#endif

#endif /* __APPLE__ */

#endif /* WIN32_MACOS_SHIM_H */
