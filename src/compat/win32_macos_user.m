#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include "win32_macos_shim.h"

/* =========================================================================
 * 1. Window Class Registry & Menu Structure
 * ========================================================================= */
typedef struct {
    char    className[64];
    WNDPROC lpfnWndProc;
    HICON   hIcon;
    HICON   hIconSm;
} ZP_ClassEntry;

static ZP_ClassEntry g_class_table[32];
static int           g_class_count = 0;

typedef struct {
    UINT     flags;
    UINT_PTR id;
    char     text[128];
    HMENU    subMenu;
} ZP_MenuItem;

struct ZP_HMENU__ {
    ZP_MenuItem *items;
    int          count;
    int          capacity;
};

/* =========================================================================
 * 2. Internal HWND Definition
 * ========================================================================= */
struct ZP_HWND__ {
    char        className[64];
    char        windowText[512];
    DWORD       style;
    DWORD       exStyle;
    int         x, y, w, h;
    HWND        parent;
    HWND       *children;
    int         childCount;
    int         childCap;
    WNDPROC     wndProc;
    LONG_PTR    userData;
    HFONT       font;
    BOOL        visible;
    BOOL        enabled;
    HMENU       hMenu;
    int         idCtrl;

    /* Top-level Cocoa binding */
    NSWindow   *nsWindow;
    NSView     *nsView;
    HDC         hdcMem;
    HBITMAP     hbmpMem;

    /* Control specific state */
    int         checkState;
    char      **listItems;
    ULONG_PTR  *itemDatas;
    int         listCount;
    int         listCap;
    int         selIndex;
    int         topIndex;

    /* Status bar parts */
    int         statusPartCount;
    int         statusRightEdges[16];
    char        statusTexts[16][128];

    /* Single-line Edit state */
    int         editCursor;

    /* Window properties (SetPropA / GetPropA / RemovePropA) */
    char        propNames[16][48];
    HANDLE      propValues[16];
    int         propCount;
};

/* Global Window Management */
static HWND g_main_hwnd = NULL;
static HWND g_focus_hwnd = NULL;
static HWND g_capture_hwnd = NULL;
static POINT g_cursor_pos = { 0, 0 };

/* Cocoa Application View Wrapper */
@interface ZPContentView : NSView <NSTextInputClient> {
@public
    HWND hwndOwner;
}
- (void)updateWindowSize;
@end

@implementation ZPContentView

- (BOOL)isFlipped {
    return YES; /* Top-down coordinates */
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)resizeWithOldSuperviewSize:(NSSize)oldSize {
    [super resizeWithOldSuperviewSize:oldSize];
    [self updateWindowSize];
}

- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [self updateWindowSize];
}

- (void)updateWindowSize {
    if (!hwndOwner) return;
    NSSize sz = self.bounds.size;
    int new_w = (int)sz.width;
    int new_h = (int)sz.height;
    if (new_w <= 0 || new_h <= 0) return;
    if (new_w == hwndOwner->w && new_h == hwndOwner->h && hwndOwner->hdcMem) return;

    hwndOwner->w = new_w;
    hwndOwner->h = new_h;

    if (hwndOwner->hbmpMem) { DeleteObject(hwndOwner->hbmpMem); hwndOwner->hbmpMem = NULL; }
    if (hwndOwner->hdcMem)  { DeleteDC(hwndOwner->hdcMem); hwndOwner->hdcMem = NULL; }

    hwndOwner->hdcMem = CreateCompatibleDC(NULL);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = new_w;
    bmi.bmiHeader.biHeight = -new_h; /* Top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    hwndOwner->hbmpMem = CreateDIBSection(hwndOwner->hdcMem, &bmi, 0, &bits, NULL, 0);
    SelectObject(hwndOwner->hdcMem, hwndOwner->hbmpMem);

    if (hwndOwner->wndProc) {
        hwndOwner->wndProc(hwndOwner, WM_SIZE, 0, MAKELPARAM(new_w, new_h));
    }
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    if (!hwndOwner) return;

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwndOwner, &ps);
    if (hdc) {
        if (hwndOwner->wndProc) {
            hwndOwner->wndProc(hwndOwner, WM_PAINT, (WPARAM)hdc, 0);
        }

        /* Render child controls in z-order onto the parent canvas */
        for (int i = 0; i < hwndOwner->childCount; ++i) {
            HWND child = hwndOwner->children[i];
            if (!child || !child->visible) continue;

            RECT rcChild = { child->x, child->y, child->x + child->w, child->y + child->h };
            if (child->style & BS_OWNERDRAW) {
                DRAWITEMSTRUCT dis;
                memset(&dis, 0, sizeof(dis));
                dis.CtlType = ODT_BUTTON;
                dis.CtlID = (UINT)child->idCtrl;
                dis.hwndItem = child;
                dis.hDC = hdc;
                dis.rcItem = rcChild;
                dis.itemState = (child == g_focus_hwnd) ? ODS_FOCUS : 0;
                SendMessageA(hwndOwner, WM_DRAWITEM, (WPARAM)child->idCtrl, (LPARAM)&dis);
            } else if (strcasecmp(child->className, "Button") == 0) {
                /* Built-in pushbutton / checkbox */
                HBRUSH br = CreateSolidBrush(0x00333333);
                FillRect(hdc, &rcChild, br);
                DeleteObject(br);
                SetTextColor(hdc, 0x00FFFFFF);
                SetBkMode(hdc, TRANSPARENT);
                DrawTextA(hdc, child->windowText, -1, &rcChild, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else if (strcasecmp(child->className, "Static") == 0) {
                /* Static text label */
                SetTextColor(hdc, 0x00D0D0D0);
                SetBkMode(hdc, TRANSPARENT);
                DrawTextA(hdc, child->windowText, -1, &rcChild, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else if (strcasecmp(child->className, "Edit") == 0) {
                /* Edit box */
                HBRUSH br = CreateSolidBrush(0x00222222);
                FillRect(hdc, &rcChild, br);
                DeleteObject(br);
                SetTextColor(hdc, 0x0000FF00);
                SetBkMode(hdc, TRANSPARENT);
                RECT rcTxt = rcChild;
                rcTxt.left += 4;
                DrawTextA(hdc, child->windowText, -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else if (strcasecmp(child->className, STATUSCLASSNAME) == 0) {
                /* Status Bar */
                HBRUSH br = CreateSolidBrush(0x00202020);
                FillRect(hdc, &rcChild, br);
                DeleteObject(br);
                SetTextColor(hdc, 0x00B0B0B0);
                SetBkMode(hdc, TRANSPARENT);
                int cur_x = rcChild.left + 8;
                for (int p = 0; p < child->statusPartCount; ++p) {
                    RECT rcPart = rcChild;
                    rcPart.left = cur_x;
                    rcPart.right = child->statusRightEdges[p] > 0 ? (rcChild.left + child->statusRightEdges[p]) : rcChild.right;
                    DrawTextA(hdc, child->statusTexts[p], -1, &rcPart, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    cur_x = rcPart.right + 8;
                }
            } else if (strcasecmp(child->className, "ListBox") == 0) {
                /* ListBox rendering */
                HBRUSH bgBr = CreateSolidBrush(0x001B1B1B);
                FillRect(hdc, &rcChild, bgBr);
                DeleteObject(bgBr);
                HPEN borderPen = CreatePen(PS_SOLID, 1, 0x00444444);
                HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rcChild.left, rcChild.top, rcChild.right, rcChild.bottom);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(borderPen);

                int itemH = 18;
                int curY = rcChild.top + 2;
                int maxItems = (rcChild.bottom - rcChild.top - 4) / itemH;
                for (int p = child->topIndex; p < child->listCount && (p - child->topIndex) < maxItems; ++p) {
                    RECT rcItem = { rcChild.left + 2, curY, rcChild.right - 2, curY + itemH };
                    if (p == child->selIndex) {
                        HBRUSH selBr = CreateSolidBrush(0x00D77800); /* Windows Selection Blue */
                        FillRect(hdc, &rcItem, selBr);
                        DeleteObject(selBr);
                        SetTextColor(hdc, 0x00FFFFFF);
                    } else {
                        SetTextColor(hdc, 0x00DDDDDD);
                    }
                    SetBkMode(hdc, TRANSPARENT);
                    RECT rcText = rcItem;
                    rcText.left += 4;
                    DrawTextA(hdc, child->listItems[p] ? child->listItems[p] : "", -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    curY += itemH;
                }
            } else if (strcasecmp(child->className, "ComboBox") == 0) {
                /* ComboBox rendering */
                HBRUSH bgBr = CreateSolidBrush(0x002B2B2B);
                FillRect(hdc, &rcChild, bgBr);
                DeleteObject(bgBr);
                HPEN borderPen = CreatePen(PS_SOLID, 1, 0x00555555);
                HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rcChild.left, rcChild.top, rcChild.right, rcChild.bottom);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(borderPen);

                SetTextColor(hdc, 0x00FFFFFF);
                SetBkMode(hdc, TRANSPARENT);
                RECT rcText = rcChild;
                rcText.left += 6;
                rcText.right -= 20;
                const char *cbTxt = (child->selIndex >= 0 && child->selIndex < child->listCount && child->listItems[child->selIndex])
                                    ? child->listItems[child->selIndex] : child->windowText;
                DrawTextA(hdc, cbTxt, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                RECT rcArrow = { rcChild.right - 18, rcChild.top + 2, rcChild.right - 2, rcChild.bottom - 2 };
                SetTextColor(hdc, 0x00AAAAAA);
                DrawTextA(hdc, "v", -1, &rcArrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else if (child->wndProc) {
                child->wndProc(child, WM_PAINT, (WPARAM)hdc, 0);
            }
        }

        EndPaint(hwndOwner, &ps);
    }

    /* Blit memDC to Cocoa NSGraphicsContext */
    if (hwndOwner->hdcMem) {
        CGImageRef img = zp_macos_create_hdc_image(hwndOwner->hdcMem);
        if (img) {
            CGContextRef dstCtx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
            CGContextSaveGState(dstCtx);
            CGContextTranslateCTM(dstCtx, 0, self.bounds.size.height);
            CGContextScaleCTM(dstCtx, 1.0f, -1.0f);
            CGContextDrawImage(dstCtx, CGRectMake(0, 0, self.bounds.size.width, self.bounds.size.height), img);
            CGContextRestoreGState(dstCtx);
            CGImageRelease(img);
        }
    }
}

- (void)mouseDown:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_cursor_pos.x = (LONG)p.x;
    g_cursor_pos.y = (LONG)p.y;

    /* Hit-test child controls */
    for (int i = hwndOwner->childCount - 1; i >= 0; --i) {
        HWND child = hwndOwner->children[i];
        if (child && child->visible && child->enabled) {
            if (p.x >= child->x && p.x < child->x + child->w &&
                p.y >= child->y && p.y < child->y + child->h) {
                SetFocus(child);
                if (strcasecmp(child->className, "Button") == 0 || (child->style & BS_OWNERDRAW)) {
                    SendMessageA(hwndOwner, WM_COMMAND, MAKEWPARAM(child->idCtrl, BN_CLICKED), (LPARAM)child);
                } else if (strcasecmp(child->className, "ListBox") == 0) {
                    int itemHeight = 18;
                    int clickedIdx = child->topIndex + (int)(p.y - child->y - 2) / itemHeight;
                    if (clickedIdx >= 0 && clickedIdx < child->listCount) {
                        child->selIndex = clickedIdx;
                        SendMessageA(hwndOwner, WM_COMMAND, MAKEWPARAM(child->idCtrl, LBN_SELCHANGE), (LPARAM)child);
                    }
                } else if (strcasecmp(child->className, "ComboBox") == 0) {
                    NSMenu *cbMenu = [[NSMenu alloc] initWithTitle:@"ComboBoxMenu"];
                    for (int itemIdx = 0; itemIdx < child->listCount; ++itemIdx) {
                        NSString *titleStr = [NSString stringWithUTF8String:child->listItems[itemIdx] ? child->listItems[itemIdx] : ""];
                        NSMenuItem *mi = [[NSMenuItem alloc] initWithTitle:titleStr action:nil keyEquivalent:@""];
                        mi.tag = itemIdx;
                        if (itemIdx == child->selIndex) [mi setState:NSControlStateValueOn];
                        [cbMenu addItem:mi];
                    }
                    NSPoint menuPoint = NSMakePoint(child->x, child->y + child->h);
                    NSMenuItem *selectedItem = [cbMenu popUpMenuPositioningItem:(child->selIndex >= 0 && child->selIndex < child->listCount) ? [cbMenu itemAtIndex:child->selIndex] : nil
                                                                     atLocation:menuPoint
                                                                         inView:self];
                    if (selectedItem) {
                        child->selIndex = (int)selectedItem.tag;
                        if (child->selIndex >= 0 && child->selIndex < child->listCount && child->listItems[child->selIndex]) {
                            strncpy(child->windowText, child->listItems[child->selIndex], sizeof(child->windowText) - 1);
                            child->windowText[sizeof(child->windowText) - 1] = '\0';
                        }
                        SendMessageA(hwndOwner, WM_COMMAND, MAKEWPARAM(child->idCtrl, CBN_SELCHANGE), (LPARAM)child);
                    }
                } else if (child->wndProc) {
                    LPARAM lp = MAKELPARAM((int)(p.x - child->x), (int)(p.y - child->y));
                    child->wndProc(child, WM_LBUTTONDOWN, 0, lp);
                }
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }

    SetFocus(hwndOwner);
    LPARAM lp = MAKELPARAM((int)p.x, (int)p.y);
    SendMessageA(hwndOwner, WM_LBUTTONDOWN, 0, lp);
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_cursor_pos.x = (LONG)p.x;
    g_cursor_pos.y = (LONG)p.y;
    LPARAM lp = MAKELPARAM((int)p.x, (int)p.y);
    SendMessageA(hwndOwner, WM_LBUTTONUP, 0, lp);
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_cursor_pos.x = (LONG)p.x;
    g_cursor_pos.y = (LONG)p.y;
    LPARAM lp = MAKELPARAM((int)p.x, (int)p.y);
    SendMessageA(hwndOwner, WM_RBUTTONDOWN, 0, lp);
    [self setNeedsDisplay:YES];
}

- (void)rightMouseUp:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_cursor_pos.x = (LONG)p.x;
    g_cursor_pos.y = (LONG)p.y;
    LPARAM lp = MAKELPARAM((int)p.x, (int)p.y);
    SendMessageA(hwndOwner, WM_RBUTTONUP, 0, lp);
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_cursor_pos.x = (LONG)p.x;
    g_cursor_pos.y = (LONG)p.y;
    LPARAM lp = MAKELPARAM((int)p.x, (int)p.y);
    SendMessageA(hwndOwner, WM_MOUSEMOVE, 0, lp);
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}

- (void)scrollWheel:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    g_cursor_pos.x = (LONG)p.x;
    g_cursor_pos.y = (LONG)p.y;

    int delta = (int)(event.scrollingDeltaY * 40.0f);
    WPARAM wp = MAKEWPARAM(0, delta);
    LPARAM lp = MAKELPARAM((int)p.x, (int)p.y);
    SendMessageA(hwndOwner, WM_MOUSEWHEEL, wp, lp);
    [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent *)event {
    unsigned short keyCode = event.keyCode;
    int vk = 0;
    switch (keyCode) {
        case 51: vk = VK_BACK; break;
        case 48: vk = VK_TAB; break;
        case 36: vk = VK_RETURN; break;
        case 53: vk = VK_ESCAPE; break;
        case 49: vk = VK_SPACE; break;
        case 123: vk = VK_LEFT; break;
        case 124: vk = VK_RIGHT; break;
        case 126: vk = VK_UP; break;
        case 125: vk = VK_DOWN; break;
        case 117: vk = VK_DELETE; break;
        case 114: vk = VK_INSERT; break;
        case 122: vk = VK_F1; break;
        case 120: vk = VK_F2; break;
        case 24: vk = VK_OEM_PLUS; break;
        case 27: vk = VK_OEM_MINUS; break;
        case 43: vk = VK_OEM_COMMA; break;
        case 33: vk = VK_OEM_4; break;
        case 30: vk = VK_OEM_6; break;
        default: break;
    }

    if (vk != 0) {
        SendMessageA(hwndOwner, WM_KEYDOWN, (WPARAM)vk, 0);
    }

    NSString *chars = event.characters;
    if (chars.length > 0) {
        unichar c = [chars characterAtIndex:0];
        SendMessageA(hwndOwner, WM_CHAR, (WPARAM)c, 0);
    }

    [self setNeedsDisplay:YES];
}

/* NSTextInputClient Stubs */
- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {}
- (void)doCommandBySelector:(SEL)selector {}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {}
- (void)unmarkText {}
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (BOOL)hasMarkedText { return NO; }
- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange { return nil; }
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange { return NSZeroRect; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return 0; }

@end

/* =========================================================================
 * 3. User32 Functions Implementation
 * ========================================================================= */
static void ensure_nsapp(void) {
    static BOOL s_inited = NO;
    if (!s_inited) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        s_inited = YES;
    }
}

ATOM RegisterClassExA(const WNDCLASSEXA *lpwcx) {
    if (!lpwcx || !lpwcx->lpszClassName) return 0;
    for (int i = 0; i < g_class_count; ++i) {
        if (strcasecmp(g_class_table[i].className, lpwcx->lpszClassName) == 0) {
            g_class_table[i].lpfnWndProc = lpwcx->lpfnWndProc;
            return (ATOM)(i + 1);
        }
    }
    if (g_class_count < 32) {
        int idx = g_class_count++;
        strncpy(g_class_table[idx].className, lpwcx->lpszClassName, 63);
        g_class_table[idx].lpfnWndProc = lpwcx->lpfnWndProc;
        g_class_table[idx].hIcon = lpwcx->hIcon;
        g_class_table[idx].hIconSm = lpwcx->hIconSm;
        return (ATOM)(idx + 1);
    }
    return 1;
}

HWND CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
                      DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
                      HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    (void)hInstance;
    ensure_nsapp();

    HWND hwnd = (HWND)calloc(1, sizeof(struct ZP_HWND__));
    if (!hwnd) return NULL;

    strncpy(hwnd->className, lpClassName ? lpClassName : "", sizeof(hwnd->className) - 1);
    strncpy(hwnd->windowText, lpWindowName ? lpWindowName : "", sizeof(hwnd->windowText) - 1);
    hwnd->exStyle = dwExStyle;
    hwnd->style   = dwStyle;
    hwnd->x = X; hwnd->y = Y; hwnd->w = nWidth; hwnd->h = nHeight;
    hwnd->parent  = hWndParent;
    hwnd->hMenu   = hMenu;
    hwnd->idCtrl  = (int)(intptr_t)hMenu;
    hwnd->visible = (dwStyle & WS_VISIBLE) ? TRUE : FALSE;
    hwnd->enabled = (dwStyle & WS_DISABLED) ? FALSE : TRUE;

    /* Lookup registered class procedure */
    for (int i = 0; i < g_class_count; ++i) {
        if (strcasecmp(g_class_table[i].className, hwnd->className) == 0) {
            hwnd->wndProc = g_class_table[i].lpfnWndProc;
            break;
        }
    }

    if (hWndParent && (dwStyle & WS_CHILD)) {
        /* Virtual Child Window */
        if (hWndParent->childCount >= hWndParent->childCap) {
            int newCap = hWndParent->childCap == 0 ? 8 : hWndParent->childCap * 2;
            hWndParent->children = (HWND*)realloc(hWndParent->children, (size_t)newCap * sizeof(HWND));
            hWndParent->childCap = newCap;
        }
        hWndParent->children[hWndParent->childCount++] = hwnd;
        if (hwnd->wndProc) {
            hwnd->wndProc(hwnd, WM_CREATE, 0, (LPARAM)lpParam);
        }
        return hwnd;
    }

    /* Top-Level Window (Main Window or Modal Window) */
    NSRect frame = NSMakeRect(X > 0 ? X : 100, Y > 0 ? Y : 100, nWidth > 0 ? nWidth : 980, nHeight > 0 ? nHeight : 720);
    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    NSWindow *win = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:mask
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [win setTitle:[NSString stringWithUTF8String:hwnd->windowText]];
    [win setAcceptsMouseMovedEvents:YES];

    ZPContentView *view = [[ZPContentView alloc] initWithFrame:frame];
    view->hwndOwner = hwnd;
    [win setContentView:view];

    hwnd->nsWindow = win;
    hwnd->nsView = view;
    if (!g_main_hwnd) g_main_hwnd = hwnd;

    /* Create client memDC */
    [view updateWindowSize];

    /* Trigger WM_CREATE */
    if (hwnd->wndProc) {
        hwnd->wndProc(hwnd, WM_CREATE, 0, (LPARAM)lpParam);
    }

    if (hwnd->visible) {
        [win makeKeyAndOrderFront:nil];
    }

    return hwnd;
}

BOOL DestroyWindow(HWND hWnd) {
    if (!hWnd) return FALSE;
    if (hWnd->wndProc) {
        hWnd->wndProc(hWnd, WM_DESTROY, 0, 0);
    }
    if (hWnd->nsWindow) {
        [hWnd->nsWindow close];
    }
    if (hWnd->hdcMem) DeleteDC(hWnd->hdcMem);
    if (hWnd->hbmpMem) DeleteObject(hWnd->hbmpMem);
    if (hWnd->children) free(hWnd->children);
    if (hWnd->listItems) {
        for (int i = 0; i < hWnd->listCount; ++i) {
            if (hWnd->listItems[i]) free(hWnd->listItems[i]);
        }
        free(hWnd->listItems);
        hWnd->listItems = NULL;
    }
    if (hWnd->itemDatas) {
        free(hWnd->itemDatas);
        hWnd->itemDatas = NULL;
    }
    if (hWnd == g_main_hwnd) g_main_hwnd = NULL;
    free(hWnd);
    return TRUE;
}

BOOL ShowWindow(HWND hWnd, int nCmdShow) {
    if (!hWnd) return FALSE;
    BOOL was = hWnd->visible;
    hWnd->visible = (nCmdShow != SW_HIDE);
    if (hWnd->nsWindow) {
        if (hWnd->visible) {
            [hWnd->nsWindow makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        } else {
            [hWnd->nsWindow orderOut:nil];
        }
    }
    if (hWnd->nsView) {
        [hWnd->nsView setNeedsDisplay:YES];
    }
    return was;
}

BOOL UpdateWindow(HWND hWnd) {
    if (hWnd && hWnd->nsView) {
        [hWnd->nsView setNeedsDisplay:YES];
    }
    return TRUE;
}

BOOL InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase) {
    (void)lpRect; (void)bErase;
    if (!hWnd) return FALSE;
    HWND top = hWnd;
    while (top->parent) top = top->parent;
    if (top->nsView) {
        [top->nsView setNeedsDisplay:YES];
    }
    return TRUE;
}

BOOL GetClientRect(HWND hWnd, LPRECT lpRect) {
    if (!hWnd || !lpRect) return FALSE;
    lpRect->left = 0;
    lpRect->top = 0;
    lpRect->right = hWnd->w;
    lpRect->bottom = hWnd->h;
    return TRUE;
}

BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) {
    if (!hWnd || !lpRect) return FALSE;
    lpRect->left = hWnd->x;
    lpRect->top = hWnd->y;
    lpRect->right = hWnd->x + hWnd->w;
    lpRect->bottom = hWnd->y + hWnd->h;
    return TRUE;
}

BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    if (!hWnd || !lpPoint) return FALSE;
    lpPoint->x -= hWnd->x;
    lpPoint->y -= hWnd->y;
    return TRUE;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint) {
    if (!hWnd || !lpPoint) return FALSE;
    lpPoint->x += hWnd->x;
    lpPoint->y += hWnd->y;
    return TRUE;
}

BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) {
    if (!hWnd) return FALSE;
    hWnd->x = X; hWnd->y = Y; hWnd->w = nWidth; hWnd->h = nHeight;
    if (hWnd->nsWindow) {
        NSRect f = NSMakeRect(X, Y, nWidth, nHeight);
        [hWnd->nsWindow setFrame:f display:bRepaint];
    }
    return TRUE;
}

BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    (void)hWndInsertAfter; (void)uFlags;
    return MoveWindow(hWnd, X, Y, cx, cy, TRUE);
}

BOOL IsWindow(HWND hWnd) {
    return hWnd != NULL;
}

LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam; (void)lParam;
    if (Msg == WM_CLOSE) {
        DestroyWindow(hWnd);
        return 0;
    }
    return 0;
}

LRESULT CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (lpPrevWndFunc) return lpPrevWndFunc(hWnd, Msg, wParam, lParam);
    return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (!hWnd) return 0;

    /* Handle internal control messages */
    if (strcasecmp(hWnd->className, "Button") == 0) {
        if (Msg == BM_SETCHECK) {
            hWnd->checkState = (int)wParam;
            return 0;
        } else if (Msg == BM_GETCHECK) {
            return hWnd->checkState;
        }
    } else if (strcasecmp(hWnd->className, STATUSCLASSNAME) == 0) {
        if (Msg == SB_SETPARTS) {
            int count = (int)wParam;
            if (count > 16) count = 16;
            hWnd->statusPartCount = count;
            int *edges = (int*)lParam;
            if (edges) {
                for (int i = 0; i < count; ++i) hWnd->statusRightEdges[i] = edges[i];
            }
            return TRUE;
        } else if (Msg == SB_SETTEXTA) {
            int part = (int)LOWORD(wParam);
            if (part >= 0 && part < 16 && lParam) {
                strncpy(hWnd->statusTexts[part], (const char*)lParam, 127);
                hWnd->statusTexts[part][127] = '\0';
            }
            return TRUE;
        }
    } else if (strcasecmp(hWnd->className, "ListBox") == 0) {
        if (Msg == LB_ADDSTRING) {
            if (!lParam) return LB_ERR;
            const char *str = (const char*)lParam;
            if (hWnd->listCount >= hWnd->listCap) {
                int newCap = hWnd->listCap == 0 ? 16 : hWnd->listCap * 2;
                hWnd->listItems = (char**)realloc(hWnd->listItems, (size_t)newCap * sizeof(char*));
                hWnd->itemDatas = (ULONG_PTR*)realloc(hWnd->itemDatas, (size_t)newCap * sizeof(ULONG_PTR));
                hWnd->listCap = newCap;
            }
            hWnd->listItems[hWnd->listCount] = strdup(str);
            hWnd->itemDatas[hWnd->listCount] = 0;
            int idx = hWnd->listCount++;
            return idx;
        } else if (Msg == LB_RESETCONTENT) {
            for (int i = 0; i < hWnd->listCount; ++i) {
                if (hWnd->listItems[i]) free(hWnd->listItems[i]);
            }
            hWnd->listCount = 0;
            hWnd->selIndex = LB_ERR;
            return 0;
        } else if (Msg == LB_SETCURSEL) {
            int idx = (int)wParam;
            if (idx < -1 || idx >= hWnd->listCount) idx = LB_ERR;
            hWnd->selIndex = idx;
            return (idx >= 0) ? idx : LB_ERR;
        } else if (Msg == LB_GETCURSEL) {
            return (hWnd->selIndex >= 0 && hWnd->selIndex < hWnd->listCount) ? hWnd->selIndex : LB_ERR;
        } else if (Msg == LB_GETCOUNT) {
            return hWnd->listCount;
        } else if (Msg == LB_GETTEXT) {
            int idx = (int)wParam;
            char *outBuf = (char*)lParam;
            if (idx < 0 || idx >= hWnd->listCount || !outBuf) return LB_ERR;
            strcpy(outBuf, hWnd->listItems[idx] ? hWnd->listItems[idx] : "");
            return (LRESULT)strlen(outBuf);
        } else if (Msg == LB_GETTEXTLEN) {
            int idx = (int)wParam;
            if (idx < 0 || idx >= hWnd->listCount) return LB_ERR;
            return (LRESULT)strlen(hWnd->listItems[idx] ? hWnd->listItems[idx] : "");
        } else if (Msg == LB_SETITEMDATA) {
            int idx = (int)wParam;
            if (idx < 0 || idx >= hWnd->listCount) return LB_ERR;
            hWnd->itemDatas[idx] = (ULONG_PTR)lParam;
            return TRUE;
        } else if (Msg == LB_GETITEMDATA) {
            int idx = (int)wParam;
            if (idx < 0 || idx >= hWnd->listCount) return LB_ERR;
            return (LRESULT)hWnd->itemDatas[idx];
        } else if (Msg == LB_ITEMFROMPOINT) {
            int y = HIWORD(lParam);
            int itemHeight = 18;
            int idx = hWnd->topIndex + (y / itemHeight);
            if (idx < 0 || idx >= hWnd->listCount) return MAKEWPARAM(0, 1);
            return MAKEWPARAM(idx, 0);
        }
    } else if (strcasecmp(hWnd->className, "ComboBox") == 0) {
        if (Msg == CB_ADDSTRING) {
            if (!lParam) return CB_ERR;
            const char *str = (const char*)lParam;
            if (hWnd->listCount >= hWnd->listCap) {
                int newCap = hWnd->listCap == 0 ? 16 : hWnd->listCap * 2;
                hWnd->listItems = (char**)realloc(hWnd->listItems, (size_t)newCap * sizeof(char*));
                hWnd->itemDatas = (ULONG_PTR*)realloc(hWnd->itemDatas, (size_t)newCap * sizeof(ULONG_PTR));
                hWnd->listCap = newCap;
            }
            hWnd->listItems[hWnd->listCount] = strdup(str);
            hWnd->itemDatas[hWnd->listCount] = 0;
            int idx = hWnd->listCount++;
            return idx;
        } else if (Msg == CB_RESETCONTENT) {
            for (int i = 0; i < hWnd->listCount; ++i) {
                if (hWnd->listItems[i]) free(hWnd->listItems[i]);
            }
            hWnd->listCount = 0;
            hWnd->selIndex = CB_ERR;
            return 0;
        } else if (Msg == CB_SETCURSEL) {
            int idx = (int)wParam;
            if (idx < -1 || idx >= hWnd->listCount) idx = CB_ERR;
            hWnd->selIndex = idx;
            if (idx >= 0 && idx < hWnd->listCount && hWnd->listItems[idx]) {
                strncpy(hWnd->windowText, hWnd->listItems[idx], sizeof(hWnd->windowText) - 1);
                hWnd->windowText[sizeof(hWnd->windowText) - 1] = '\0';
            }
            return (idx >= 0) ? idx : CB_ERR;
        } else if (Msg == CB_GETCURSEL) {
            return (hWnd->selIndex >= 0 && hWnd->selIndex < hWnd->listCount) ? hWnd->selIndex : CB_ERR;
        } else if (Msg == CB_GETCOUNT) {
            return hWnd->listCount;
        } else if (Msg == CB_GETLBTEXT) {
            int idx = (int)wParam;
            char *outBuf = (char*)lParam;
            if (idx < 0 || idx >= hWnd->listCount || !outBuf) return CB_ERR;
            strcpy(outBuf, hWnd->listItems[idx] ? hWnd->listItems[idx] : "");
            return (LRESULT)strlen(outBuf);
        } else if (Msg == CB_GETLBTEXTLEN) {
            int idx = (int)wParam;
            if (idx < 0 || idx >= hWnd->listCount) return CB_ERR;
            return (LRESULT)strlen(hWnd->listItems[idx] ? hWnd->listItems[idx] : "");
        } else if (Msg == CB_SETITEMDATA) {
            int idx = (int)wParam;
            if (idx < 0 || idx >= hWnd->listCount) return CB_ERR;
            hWnd->itemDatas[idx] = (ULONG_PTR)lParam;
            return TRUE;
        } else if (Msg == CB_GETITEMDATA) {
            int idx = (int)wParam;
            if (idx < 0 || idx >= hWnd->listCount) return CB_ERR;
            return (LRESULT)hWnd->itemDatas[idx];
        }
    }

    if (hWnd->wndProc) return hWnd->wndProc(hWnd, Msg, wParam, lParam);
    return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    SendMessageA(hWnd, Msg, wParam, lParam);
    return TRUE;
}

void PostQuitMessage(int nExitCode) {
    (void)nExitCode;
    [NSApp stop:nil];
}

BOOL GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax;
    ensure_nsapp();

    NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantFuture]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (!event) return FALSE;

    [NSApp sendEvent:event];

    if (lpMsg) {
        memset(lpMsg, 0, sizeof(MSG));
        lpMsg->time = (DWORD)([event timestamp] * 1000.0);
    }
    return TRUE;
}

BOOL PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax;
    ensure_nsapp();

    NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:(wRemoveMsg & PM_REMOVE) ? YES : NO];
    if (!event) return FALSE;

    if (wRemoveMsg & PM_REMOVE) {
        [NSApp sendEvent:event];
    }

    if (lpMsg) {
        memset(lpMsg, 0, sizeof(MSG));
        lpMsg->time = (DWORD)([event timestamp] * 1000.0);
    }
    return TRUE;
}

BOOL TranslateMessage(const MSG *lpMsg) {
    (void)lpMsg;
    return TRUE;
}

LRESULT DispatchMessageA(const MSG *lpMsg) {
    (void)lpMsg;
    return 0;
}

HDC GetDC(HWND hWnd) {
    if (!hWnd) return CreateCompatibleDC(NULL);
    HWND top = hWnd;
    while (top->parent) top = top->parent;
    return top->hdcMem ? top->hdcMem : CreateCompatibleDC(NULL);
}

HDC GetWindowDC(HWND hWnd) {
    return GetDC(hWnd);
}

int ReleaseDC(HWND hWnd, HDC hDC) {
    (void)hWnd; (void)hDC;
    return 1;
}

HDC BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint) {
    if (!hWnd || !lpPaint) return NULL;
    memset(lpPaint, 0, sizeof(PAINTSTRUCT));
    GetClientRect(hWnd, &lpPaint->rcPaint);
    lpPaint->hdc = GetDC(hWnd);
    return lpPaint->hdc;
}

BOOL EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint) {
    (void)hWnd; (void)lpPaint;
    return TRUE;
}

LONG_PTR SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
    if (!hWnd) return 0;
    if (nIndex == GWLP_USERDATA) {
        LONG_PTR old = hWnd->userData;
        hWnd->userData = dwNewLong;
        return old;
    }
    if (nIndex == GWLP_WNDPROC) {
        WNDPROC old = hWnd->wndProc;
        hWnd->wndProc = (WNDPROC)dwNewLong;
        return (LONG_PTR)old;
    }
    return 0;
}

LONG_PTR GetWindowLongPtrA(HWND hWnd, int nIndex) {
    if (!hWnd) return 0;
    if (nIndex == GWLP_USERDATA) return hWnd->userData;
    if (nIndex == GWLP_WNDPROC) return (LONG_PTR)hWnd->wndProc;
    return 0;
}

BOOL SetWindowTextA(HWND hWnd, LPCSTR lpString) {
    if (!hWnd) return FALSE;
    strncpy(hWnd->windowText, lpString ? lpString : "", sizeof(hWnd->windowText) - 1);
    if (hWnd->nsWindow) {
        [hWnd->nsWindow setTitle:[NSString stringWithUTF8String:hWnd->windowText]];
    }
    return TRUE;
}

int GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount) {
    if (!hWnd || !lpString || nMaxCount <= 0) return 0;
    strncpy(lpString, hWnd->windowText, (size_t)nMaxCount - 1);
    lpString[nMaxCount - 1] = '\0';
    return (int)strlen(lpString);
}

int GetWindowTextLengthA(HWND hWnd) {
    if (!hWnd) return 0;
    return (int)strlen(hWnd->windowText);
}

int GetClassNameA(HWND hWnd, LPSTR lpClassName, int nMaxCount) {
    if (!hWnd || !lpClassName || nMaxCount <= 0) return 0;
    strncpy(lpClassName, hWnd->className, (size_t)nMaxCount - 1);
    lpClassName[nMaxCount - 1] = '\0';
    return (int)strlen(lpClassName);
}

DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) {
    (void)hWnd;
    if (lpdwProcessId) *lpdwProcessId = (DWORD)getpid();
    return (DWORD)getpid();
}

HWND SetFocus(HWND hWnd) {
    HWND old = g_focus_hwnd;
    g_focus_hwnd = hWnd;
    return old;
}

HWND GetFocus(void) {
    return g_focus_hwnd;
}

HWND SetCapture(HWND hWnd) {
    HWND old = g_capture_hwnd;
    g_capture_hwnd = hWnd;
    return old;
}

BOOL ReleaseCapture(void) {
    g_capture_hwnd = NULL;
    return TRUE;
}

HWND GetCapture(void) {
    return g_capture_hwnd;
}

BOOL EnableWindow(HWND hWnd, BOOL bEnable) {
    if (!hWnd) return FALSE;
    BOOL old = hWnd->enabled;
    hWnd->enabled = bEnable;
    return old;
}

BOOL IsWindowEnabled(HWND hWnd) {
    return hWnd ? hWnd->enabled : FALSE;
}

BOOL IsWindowVisible(HWND hWnd) {
    return hWnd ? hWnd->visible : FALSE;
}

HWND GetParent(HWND hWnd) {
    return hWnd ? hWnd->parent : NULL;
}

HWND GetAncestor(HWND hWnd, UINT gaFlags) {
    (void)gaFlags;
    HWND top = hWnd;
    while (top && top->parent) top = top->parent;
    return top;
}

HWND GetDlgItem(HWND hDlg, int nIDDlgItem) {
    if (!hDlg) return NULL;
    for (int i = 0; i < hDlg->childCount; ++i) {
        if (hDlg->children[i] && hDlg->children[i]->idCtrl == nIDDlgItem) {
            return hDlg->children[i];
        }
    }
    return NULL;
}

int GetDlgCtrlID(HWND hWnd) {
    return hWnd ? hWnd->idCtrl : 0;
}

UINT IsDlgButtonChecked(HWND hDlg, int nIDButton) {
    HWND btn = GetDlgItem(hDlg, nIDButton);
    return btn ? (UINT)btn->checkState : 0;
}

UINT_PTR SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, void *lpTimerFunc) {
    (void)lpTimerFunc;
    NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:((NSTimeInterval)uElapse / 1000.0)
                                                     repeats:YES
                                                       block:^(NSTimer * _Nonnull t) {
        (void)t;
        if (hWnd && hWnd->wndProc) {
            hWnd->wndProc(hWnd, WM_TIMER, (WPARAM)nIDEvent, 0);
        }
    }];
    return (UINT_PTR)(__bridge void*)timer;
}

BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent) {
    (void)hWnd;
    if (uIDEvent) {
        NSTimer *t = (__bridge NSTimer*)(void*)uIDEvent;
        [t invalidate];
    }
    return TRUE;
}

BOOL TrackMouseEvent(LPTRACKMOUSEEVENT lpEventTrack) {
    (void)lpEventTrack;
    return TRUE;
}

HCURSOR SetCursor(HCURSOR hCursor) {
    (void)hCursor;
    return NULL;
}

HCURSOR LoadCursor(HINSTANCE hInstance, LPCSTR lpCursorName) {
    (void)hInstance; (void)lpCursorName;
    return (HCURSOR)0x1;
}

HICON LoadImageA(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad) {
    (void)hInst; (void)name; (void)type; (void)cx; (void)cy; (void)fuLoad;
    return (HICON)0x1;
}

BOOL GetCursorPos(LPPOINT lpPoint) {
    if (!lpPoint) return FALSE;
    *lpPoint = g_cursor_pos;
    return TRUE;
}

SHORT GetAsyncKeyState(int vKey) {
    CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
    if (vKey == VK_CONTROL) {
        if ((flags & kCGEventFlagMaskControl) || (flags & kCGEventFlagMaskCommand)) {
            return (SHORT)0x8000;
        }
    } else if (vKey == VK_SHIFT) {
        if (flags & kCGEventFlagMaskShift) return (SHORT)0x8000;
    } else if (vKey == VK_MENU) {
        if (flags & kCGEventFlagMaskAlternate) return (SHORT)0x8000;
    }
    return 0;
}

SHORT GetKeyState(int vKey) {
    return GetAsyncKeyState(vKey);
}

int GetSystemMetrics(int nIndex) {
    NSScreen *screen = [NSScreen mainScreen];
    NSRect r = screen ? screen.frame : NSMakeRect(0, 0, 1920, 1080);
    if (nIndex == SM_CXSCREEN) return (int)r.size.width;
    if (nIndex == SM_CYSCREEN) return (int)r.size.height;
    return 0;
}

BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle) {
    (void)dwStyle; (void)bMenu; (void)dwExStyle;
    if (lpRect) {
        lpRect->top -= 28;
    }
    return TRUE;
}

/* =========================================================================
 * 4. Menu Management & Native macOS Menu Bar
 * ========================================================================= */
HMENU CreateMenu(void) {
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)calloc(1, sizeof(struct ZP_HMENU__));
    return (HMENU)m;
}

HMENU CreatePopupMenu(void) {
    return CreateMenu();
}

BOOL DestroyMenu(HMENU hMenu) {
    if (!hMenu) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    if (m->items) free(m->items);
    free(m);
    return TRUE;
}

BOOL AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) {
    if (!hMenu) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    if (m->count >= m->capacity) {
        int newCap = m->capacity == 0 ? 8 : m->capacity * 2;
        m->items = (ZP_MenuItem*)realloc(m->items, (size_t)newCap * sizeof(ZP_MenuItem));
        m->capacity = newCap;
    }
    ZP_MenuItem *it = &m->items[m->count++];
    memset(it, 0, sizeof(ZP_MenuItem));
    it->flags = uFlags;
    it->id = uIDNewItem;
    strncpy(it->text, lpNewItem ? lpNewItem : "", 127);
    if (uFlags & MF_POPUP) {
        it->subMenu = (HMENU)uIDNewItem;
    }
    return TRUE;
}

BOOL EnableMenuItem(HMENU hMenu, UINT uIDEnableItem, UINT uEnable) {
    if (!hMenu) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    for (int i = 0; i < m->count; ++i) {
        if (m->items[i].id == uIDEnableItem) {
            m->items[i].flags = (m->items[i].flags & ~MF_GRAYED) | (uEnable & MF_GRAYED);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck) {
    if (!hMenu) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    for (int i = 0; i < m->count; ++i) {
        if (m->items[i].id == uIDCheckItem) {
            m->items[i].flags = (m->items[i].flags & ~MF_CHECKED) | (uCheck & MF_CHECKED);
            return TRUE;
        }
    }
    return FALSE;
}

HMENU GetSubMenu(HMENU hMenu, int nPos) {
    if (!hMenu) return NULL;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    if (nPos >= 0 && nPos < m->count) {
        return m->items[nPos].subMenu;
    }
    return NULL;
}

HMENU GetMenu(HWND hWnd) {
    return hWnd ? hWnd->hMenu : NULL;
}

static NSMenu* build_nsmenu_from_zp(HMENU hMenu, HWND hwndTarget) {
    if (!hMenu) return nil;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    NSMenu *nsMenu = [[NSMenu alloc] initWithTitle:@""];
    [nsMenu setAutoenablesItems:NO];

    for (int i = 0; i < m->count; ++i) {
        ZP_MenuItem *it = &m->items[i];
        if (it->flags & MF_SEPARATOR) {
            [nsMenu addItem:[NSMenuItem separatorItem]];
        } else if (it->flags & MF_POPUP) {
            NSMenuItem *subItem = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:it->text]
                                                             action:nil
                                                      keyEquivalent:@""];
            if (it->subMenu) {
                NSMenu *sub = build_nsmenu_from_zp(it->subMenu, hwndTarget);
                [sub setTitle:[NSString stringWithUTF8String:it->text]];
                [subItem setSubmenu:sub];
            }
            [nsMenu addItem:subItem];
        } else {
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:it->text]
                                                          action:@selector(onMenuItemTriggered:)
                                                   keyEquivalent:@""];
            item.tag = (NSInteger)it->id;
            [item setTarget:[NSApp delegate]];
            [item setEnabled:(it->flags & MF_GRAYED) ? NO : YES];
            [item setState:(it->flags & MF_CHECKED) ? NSControlStateValueOn : NSControlStateValueOff];
            [nsMenu addItem:item];
        }
    }
    return nsMenu;
}

BOOL SetMenu(HWND hWnd, HMENU hMenu) {
    if (!hWnd) return FALSE;
    hWnd->hMenu = hMenu;

    NSMenu *appleMenu = build_nsmenu_from_zp(hMenu, hWnd);
    if (appleMenu) {
        [NSApp setMainMenu:appleMenu];
    }
    return TRUE;
}

BOOL DrawMenuBar(HWND hWnd) {
    if (hWnd && hWnd->hMenu) {
        SetMenu(hWnd, hWnd->hMenu);
    }
    return TRUE;
}

BOOL TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT *prcRect) {
    (void)uFlags; (void)nReserved; (void)prcRect;
    if (!hMenu || !hWnd || !hWnd->nsView) return FALSE;
    NSMenu *popup = build_nsmenu_from_zp(hMenu, hWnd);
    if (popup) {
        NSPoint pt = NSMakePoint(x, y);
        [popup popUpMenuPositioningItem:nil atLocation:pt inView:hWnd->nsView];
        return TRUE;
    }
    return FALSE;
}

int GetMenuItemCount(HMENU hMenu) {
    if (!hMenu) return 0;
    return ((struct ZP_HMENU__*)hMenu)->count;
}

UINT GetMenuItemID(HMENU hMenu, int nPos) {
    if (!hMenu) return (UINT)-1;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    if (nPos >= 0 && nPos < m->count) {
        return (UINT)m->items[nPos].id;
    }
    return (UINT)-1;
}

int GetMenuStringA(HMENU hMenu, UINT uIDItem, LPSTR lpString, int cchMax, UINT flags) {
    (void)flags;
    if (!hMenu || !lpString || cchMax <= 0) return 0;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    for (int i = 0; i < m->count; ++i) {
        if (m->items[i].id == uIDItem) {
            strncpy(lpString, m->items[i].text, (size_t)cchMax - 1);
            lpString[cchMax - 1] = '\0';
            return (int)strlen(lpString);
        }
    }
    return 0;
}

BOOL DeleteMenu(HMENU hMenu, UINT uPosition, UINT uFlags) {
    (void)uFlags;
    if (!hMenu) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    if (uPosition < (UINT)m->count) {
        for (int i = (int)uPosition; i < m->count - 1; ++i) {
            m->items[i] = m->items[i + 1];
        }
        m->count--;
        return TRUE;
    }
    return FALSE;
}

BOOL RemoveMenu(HMENU hMenu, UINT uPosition, UINT uFlags) {
    return DeleteMenu(hMenu, uPosition, uFlags);
}

BOOL InsertMenuA(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) {
    (void)uPosition;
    return AppendMenuA(hMenu, uFlags, uIDNewItem, lpNewItem);
}

BOOL ModifyMenuA(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) {
    if (!hMenu) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    if (uPosition < (UINT)m->count) {
        m->items[uPosition].flags = uFlags;
        m->items[uPosition].id = uIDNewItem;
        strncpy(m->items[uPosition].text, lpNewItem ? lpNewItem : "", 127);
        return TRUE;
    }
    return FALSE;
}

UINT GetMenuState(HMENU hMenu, UINT uId, UINT uFlags) {
    (void)uFlags;
    if (!hMenu) return (UINT)-1;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    for (int i = 0; i < m->count; ++i) {
        if (m->items[i].id == uId) return m->items[i].flags;
    }
    return 0;
}

/* =========================================================================
 * 5. Accelerators, Hooks & Multimedia Timers
 * ========================================================================= */
struct ZP_HACCEL__ {
    ACCEL *accels;
    int    count;
};

HACCEL CreateAcceleratorTableA(LPACCEL paccel, int cAccel) {
    if (!paccel || cAccel <= 0) return NULL;
    struct ZP_HACCEL__ *h = (struct ZP_HACCEL__*)malloc(sizeof(struct ZP_HACCEL__));
    h->count = cAccel;
    h->accels = (ACCEL*)malloc((size_t)cAccel * sizeof(ACCEL));
    memcpy(h->accels, paccel, (size_t)cAccel * sizeof(ACCEL));
    return (HACCEL)h;
}

int TranslateAcceleratorA(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg) {
    (void)hWnd; (void)hAccTable; (void)lpMsg;
    return 0;
}

BOOL DestroyAcceleratorTable(HACCEL hAccel) {
    if (!hAccel) return FALSE;
    struct ZP_HACCEL__ *h = (struct ZP_HACCEL__*)hAccel;
    if (h->accels) free(h->accels);
    free(h);
    return TRUE;
}

HHOOK SetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) {
    (void)idHook; (void)lpfn; (void)hmod; (void)dwThreadId;
    return (HHOOK)0x1;
}

BOOL UnhookWindowsHookEx(HHOOK hhk) {
    (void)hhk;
    return TRUE;
}

LRESULT CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam) {
    (void)hhk; (void)nCode; (void)wParam; (void)lParam;
    return 0;
}

UINT timeBeginPeriod(UINT uPeriod) {
    (void)uPeriod;
    return 0;
}

UINT timeEndPeriod(UINT uPeriod) {
    (void)uPeriod;
    return 0;
}

/* =========================================================================
 * 6. Common Dialogs & Clipboard
 * ========================================================================= */
int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {
    (void)hWnd;
    ensure_nsapp();

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:[NSString stringWithUTF8String:lpCaption ? lpCaption : "Alert"]];
    [alert setInformativeText:[NSString stringWithUTF8String:lpText ? lpText : ""]];

    if (uType & MB_ICONERROR) {
        [alert setAlertStyle:NSAlertStyleCritical];
    } else if (uType & MB_ICONWARNING) {
        [alert setAlertStyle:NSAlertStyleWarning];
    } else {
        [alert setAlertStyle:NSAlertStyleInformational];
    }

    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
    return 1;
}

BOOL GetOpenFileNameA(LPOPENFILENAMEA ofn) {
    if (!ofn) return FALSE;
    ensure_nsapp();

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];

    if (ofn->lpstrTitle) {
        [panel setTitle:[NSString stringWithUTF8String:ofn->lpstrTitle]];
    }

    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = [panel URL];
        if (url && url.path) {
            strncpy(ofn->lpstrFile, [url.path UTF8String], ofn->nMaxFile - 1);
            ofn->lpstrFile[ofn->nMaxFile - 1] = '\0';
            return TRUE;
        }
    }
    return FALSE;
}

BOOL GetSaveFileNameA(LPOPENFILENAMEA ofn) {
    if (!ofn) return FALSE;
    ensure_nsapp();

    NSSavePanel *panel = [NSSavePanel savePanel];
    if (ofn->lpstrTitle) {
        [panel setTitle:[NSString stringWithUTF8String:ofn->lpstrTitle]];
    }
    if (ofn->lpstrFile && ofn->lpstrFile[0]) {
        [panel setNameFieldStringValue:[NSString stringWithUTF8String:ofn->lpstrFile]];
    }

    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = [panel URL];
        if (url && url.path) {
            strncpy(ofn->lpstrFile, [url.path UTF8String], ofn->nMaxFile - 1);
            ofn->lpstrFile[ofn->nMaxFile - 1] = '\0';
            return TRUE;
        }
    }
    return FALSE;
}

DWORD GetTickCount(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (DWORD)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

void GetLocalTime(LPSYSTEMTIME lpSystemTime) {
    if (!lpSystemTime) return;
    time_t t = time(NULL);
    struct tm tm_val;
    localtime_r(&t, &tm_val);

    lpSystemTime->wYear = (WORD)(tm_val.tm_year + 1900);
    lpSystemTime->wMonth = (WORD)(tm_val.tm_mon + 1);
    lpSystemTime->wDay = (WORD)tm_val.tm_mday;
    lpSystemTime->wDayOfWeek = (WORD)tm_val.tm_wday;
    lpSystemTime->wHour = (WORD)tm_val.tm_hour;
    lpSystemTime->wMinute = (WORD)tm_val.tm_min;
    lpSystemTime->wSecond = (WORD)tm_val.tm_sec;
    lpSystemTime->wMilliseconds = 0;
}

BOOL OpenClipboard(HWND hWndNewOwner) {
    (void)hWndNewOwner;
    return TRUE;
}

BOOL CloseClipboard(void) {
    return TRUE;
}

HANDLE GetClipboardData(UINT uFormat) {
    (void)uFormat;
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    NSString *str = [pb stringForType:NSPasteboardTypeString];
    if (str) {
        return (HANDLE)[str UTF8String];
    }
    return NULL;
}

BOOL ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd) {
    (void)hwnd; (void)lpOperation; (void)lpParameters; (void)lpDirectory; (void)nShowCmd;
    if (!lpFile) return FALSE;
    NSString *s = [NSString stringWithUTF8String:lpFile];
    NSURL *u = [NSURL URLWithString:s];
    if (!u || !u.scheme) {
        u = [NSURL fileURLWithPath:s];
    }
    return [[NSWorkspace sharedWorkspace] openURL:u];
}

/* =========================================================================
 * 7. Threading, Synchronization, COM & Miscellaneous Stubs
 * ========================================================================= */
#include <pthread.h>

typedef struct {
    LPTHREAD_START_ROUTINE start_routine;
    LPVOID param;
} ZP_THREAD_START_ARGS;

static void* zp_posix_thread_shim(void *arg) {
    ZP_THREAD_START_ARGS *args = (ZP_THREAD_START_ARGS*)arg;
    LPTHREAD_START_ROUTINE func = args->start_routine;
    LPVOID param = args->param;
    free(args);
    DWORD ret = func ? func(param) : 0;
    return (void*)(intptr_t)ret;
}

HANDLE CreateThread(void *lpThreadAttributes, size_t dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId) {
    (void)lpThreadAttributes; (void)dwStackSize; (void)dwCreationFlags;
    ZP_THREAD_START_ARGS *args = (ZP_THREAD_START_ARGS*)malloc(sizeof(ZP_THREAD_START_ARGS));
    if (!args) return NULL;
    args->start_routine = lpStartAddress;
    args->param = lpParameter;

    pthread_t *pth = (pthread_t*)malloc(sizeof(pthread_t));
    if (!pth) { free(args); return NULL; }
    if (pthread_create(pth, NULL, zp_posix_thread_shim, args) != 0) {
        free(pth);
        free(args);
        return NULL;
    }
    if (lpThreadId) *lpThreadId = (DWORD)(uintptr_t)*pth;
    return (HANDLE)pth;
}

DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    if (!hHandle) return 0xFFFFFFFF;
    pthread_t *pth = (pthread_t*)hHandle;
    (void)dwMilliseconds;
    pthread_join(*pth, NULL);
    return 0;
}

BOOL CloseHandle(HANDLE hObject) {
    if (!hObject) return FALSE;
    free(hObject);
    return TRUE;
}

BOOL SetForegroundWindow(HWND hWnd) {
    (void)hWnd;
    [NSApp activateIgnoringOtherApps:YES];
    return TRUE;
}

HRESULT CoInitialize(LPVOID pvReserved) { (void)pvReserved; return S_OK; }
HRESULT CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit) { (void)pvReserved; (void)dwCoInit; return S_OK; }
void CoUninitialize(void) {}

BOOL CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck) {
    HWND btn = GetDlgItem(hDlg, nIDButton);
    if (btn) {
        SendMessageA(btn, BM_SETCHECK, (WPARAM)uCheck, 0);
        return TRUE;
    }
    return FALSE;
}

BOOL GetComboBoxInfo(HWND hwndCombo, PCOMBOBOXINFO pcbi) {
    if (!pcbi) return FALSE;
    memset(pcbi, 0, sizeof(COMBOBOXINFO));
    pcbi->cbSize = sizeof(COMBOBOXINFO);
    pcbi->hwndCombo = hwndCombo;
    pcbi->hwndItem = hwndCombo;
    pcbi->hwndList = hwndCombo;
    return TRUE;
}

HRESULT SetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList) {
    (void)hwnd; (void)pszSubAppName; (void)pszSubIdList;
    return S_OK;
}

DWORD GetLastError(void) {
    return (DWORD)errno;
}

void SetLastError(DWORD dwErrCode) {
    errno = (int)dwErrCode;
}

HWND GetDesktopWindow(void) {
    return NULL;
}

/* =========================================================================
 * 7.5 Extended Windowing, GDI & Platform Implementation
 * ========================================================================= */

ATOM RegisterClassA(const WNDCLASSA *wc) {
    if (!wc) return 0;
    WNDCLASSEXA ex;
    memset(&ex, 0, sizeof(ex));
    ex.cbSize = sizeof(ex);
    ex.style = wc->style;
    ex.lpfnWndProc = wc->lpfnWndProc;
    ex.cbClsExtra = wc->cbClsExtra;
    ex.cbWndExtra = wc->cbWndExtra;
    ex.hInstance = wc->hInstance;
    ex.hIcon = wc->hIcon;
    ex.hCursor = wc->hCursor;
    ex.hbrBackground = wc->hbrBackground;
    ex.lpszMenuName = wc->lpszMenuName;
    ex.lpszClassName = wc->lpszClassName;
    return RegisterClassExA(&ex);
}

BOOL RedrawWindow(HWND hWnd, const RECT *lprcUpdate, HRGN hrgnUpdate, UINT flags) {
    (void)hrgnUpdate;
    if (!hWnd) return FALSE;
    if (flags & (RDW_INVALIDATE | RDW_ERASE)) {
        InvalidateRect(hWnd, lprcUpdate, (flags & RDW_ERASE) ? TRUE : FALSE);
    }
    if (flags & RDW_UPDATENOW) {
        UpdateWindow(hWnd);
    }
    return TRUE;
}

HDWP BeginDeferWindowPos(int nNumWindows) {
    (void)nNumWindows;
    return (HDWP)1;
}

HDWP DeferWindowPos(HDWP hWinPosInfo, HWND hWnd, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT uFlags) {
    SetWindowPos(hWnd, hWndInsertAfter, x, y, cx, cy, uFlags);
    return hWinPosInfo;
}

BOOL EndDeferWindowPos(HDWP hWinPosInfo) {
    (void)hWinPosInfo;
    return TRUE;
}

BOOL IsZoomed(HWND hWnd) {
    if (!hWnd || !hWnd->nsWindow) return FALSE;
    return [hWnd->nsWindow isZoomed] ? TRUE : FALSE;
}

BOOL IsIconic(HWND hWnd) {
    if (!hWnd || !hWnd->nsWindow) return FALSE;
    return [hWnd->nsWindow isMiniaturized] ? TRUE : FALSE;
}

HWND WindowFromPoint(POINT Point) {
    NSPoint p = NSMakePoint(Point.x, Point.y);
    NSInteger winNum = [NSWindow windowNumberAtPoint:p belowWindowWithWindowNumber:0];
    NSWindow *w = [NSApp windowWithWindowNumber:winNum];
    if (w && [w.contentView isKindOfClass:[ZPContentView class]]) {
        ZPContentView *cv = (ZPContentView*)w.contentView;
        return cv->hwndOwner;
    }
    return NULL;
}

int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) {
    if (!hWnd || !lpString || nMaxCount <= 0) return 0;
    char buf[1024];
    int len = GetWindowTextA(hWnd, buf, sizeof(buf));
    return MultiByteToWideChar(CP_UTF8, 0, buf, len, lpString, nMaxCount);
}

HMODULE GetModuleHandleA(LPCSTR lpModuleName) {
    (void)lpModuleName;
    return (HMODULE)0x400000;
}

HMODULE LoadLibraryA(LPCSTR lpLibFileName) {
    (void)lpLibFileName;
    return NULL;
}

FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    (void)hModule;
    (void)lpProcName;
    return NULL;
}

BOOL SetPropA(HWND hWnd, LPCSTR lpString, HANDLE hData) {
    if (!hWnd || !lpString) return FALSE;
    for (int i = 0; i < hWnd->propCount; ++i) {
        if (strcmp(hWnd->propNames[i], lpString) == 0) {
            hWnd->propValues[i] = hData;
            return TRUE;
        }
    }
    if (hWnd->propCount < 16) {
        strncpy(hWnd->propNames[hWnd->propCount], lpString, 47);
        hWnd->propNames[hWnd->propCount][47] = '\0';
        hWnd->propValues[hWnd->propCount] = hData;
        hWnd->propCount++;
        return TRUE;
    }
    return FALSE;
}

HANDLE GetPropA(HWND hWnd, LPCSTR lpString) {
    if (!hWnd || !lpString) return NULL;
    for (int i = 0; i < hWnd->propCount; ++i) {
        if (strcmp(hWnd->propNames[i], lpString) == 0) {
            return hWnd->propValues[i];
        }
    }
    return NULL;
}

HANDLE RemovePropA(HWND hWnd, LPCSTR lpString) {
    if (!hWnd || !lpString) return NULL;
    for (int i = 0; i < hWnd->propCount; ++i) {
        if (strcmp(hWnd->propNames[i], lpString) == 0) {
            HANDLE val = hWnd->propValues[i];
            for (int j = i; j < hWnd->propCount - 1; ++j) {
                strncpy(hWnd->propNames[j], hWnd->propNames[j + 1], 48);
                hWnd->propValues[j] = hWnd->propValues[j + 1];
            }
            hWnd->propCount--;
            return val;
        }
    }
    return NULL;
}

HWND FindWindowExA(HWND hWndParent, HWND hWndChildAfter, LPCSTR lpszClass, LPCSTR lpszWindow) {
    (void)hWndChildAfter;
    if (!hWndParent) return NULL;
    for (int i = 0; i < hWndParent->childCount; ++i) {
        HWND c = hWndParent->children[i];
        if (!c) continue;
        BOOL matchClass = (!lpszClass || (c->className && strcmp(c->className, lpszClass) == 0));
        BOOL matchTitle = (!lpszWindow || (c->windowText && strcmp(c->windowText, lpszWindow) == 0));
        if (matchClass && matchTitle) return c;
    }
    return NULL;
}

BOOL IsMenu(HMENU hMenu) {
    return (hMenu != NULL);
}

BOOL GetMenuItemInfoA(HMENU hMenu, UINT item, BOOL fByPosition, LPMENUITEMINFOA lpmii) {
    if (!hMenu || !lpmii) return FALSE;
    struct ZP_HMENU__ *m = (struct ZP_HMENU__*)hMenu;
    ZP_MenuItem *found = NULL;
    if (fByPosition) {
        if (item < (UINT)m->count) found = &m->items[item];
    } else {
        for (int i = 0; i < m->count; ++i) {
            if (m->items[i].id == item) {
                found = &m->items[i];
                break;
            }
        }
    }
    if (!found) return FALSE;
    if (lpmii->fMask & MIIM_STATE) lpmii->fState = found->flags;
    if (lpmii->fMask & MIIM_ID) lpmii->wID = (UINT)found->id;
    if (lpmii->fMask & MIIM_SUBMENU) lpmii->hSubMenu = found->subMenu;
    if ((lpmii->fMask & MIIM_STRING) && lpmii->dwTypeData && lpmii->cch > 0) {
        strncpy(lpmii->dwTypeData, found->text, lpmii->cch - 1);
        lpmii->dwTypeData[lpmii->cch - 1] = '\0';
    }
    return TRUE;
}

BOOL GetMenuBarInfo(HWND hwnd, LONG idObject, LONG idItem, PMENUBARINFO pmbi) {
    (void)hwnd; (void)idObject; (void)idItem;
    if (!pmbi) return FALSE;
    memset(pmbi, 0, sizeof(*pmbi));
    pmbi->cbSize = sizeof(*pmbi);
    return TRUE;
}

DWORD timeGetTime(void) {
    return (DWORD)([[NSDate date] timeIntervalSince1970] * 1000.0);
}

DWORD GetCurrentThreadId(void) {
    return (DWORD)(uintptr_t)pthread_self();
}

void InitCommonControls(void) {
    /* No-op on macOS Cocoa */
}

HRESULT DwmSetWindowAttribute(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute) {
    (void)hwnd; (void)dwAttribute; (void)pvAttribute; (void)cbAttribute;
    return 0;
}

int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar) {
    (void)CodePage; (void)dwFlags; (void)lpDefaultChar; (void)lpUsedDefaultChar;
    if (!lpWideCharStr) return 0;
    int srcLen = (cchWideChar < 0) ? (int)wcslen((const wchar_t*)lpWideCharStr) : cchWideChar;
    if (cbMultiByte <= 0 || !lpMultiByteStr) {
        return srcLen + ((cchWideChar < 0) ? 1 : 0);
    }
    int outLen = 0;
    for (int i = 0; i < srcLen && outLen < cbMultiByte - 1; ++i) {
        wchar_t wc = ((const wchar_t*)lpWideCharStr)[i];
        lpMultiByteStr[outLen++] = (wc < 128) ? (char)wc : '?';
    }
    if (cchWideChar < 0 && outLen < cbMultiByte) {
        lpMultiByteStr[outLen++] = '\0';
    } else if (outLen < cbMultiByte) {
        lpMultiByteStr[outLen] = '\0';
    }
    return outLen;
}

int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar) {
    (void)CodePage; (void)dwFlags;
    if (!lpMultiByteStr) return 0;
    int srcLen = (cbMultiByte < 0) ? (int)strlen(lpMultiByteStr) : cbMultiByte;
    if (cchWideChar <= 0 || !lpWideCharStr) {
        return srcLen + ((cbMultiByte < 0) ? 1 : 0);
    }
    int outLen = 0;
    for (int i = 0; i < srcLen && outLen < cchWideChar - 1; ++i) {
        ((wchar_t*)lpWideCharStr)[outLen++] = (unsigned char)lpMultiByteStr[i];
    }
    if (cbMultiByte < 0 && outLen < cchWideChar) {
        ((wchar_t*)lpWideCharStr)[outLen++] = 0;
    } else if (outLen < cchWideChar) {
        ((wchar_t*)lpWideCharStr)[outLen] = 0;
    }
    return outLen;
}

HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags) {
    (void)pt; (void)dwFlags;
    return (HMONITOR)1;
}

HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags) {
    (void)hwnd; (void)dwFlags;
    return (HMONITOR)1;
}

BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi) {
    (void)hMonitor;
    if (!lpmi || lpmi->cbSize < sizeof(MONITORINFO)) return FALSE;
    NSScreen *mainScreen = [NSScreen mainScreen];
    NSRect frame = mainScreen ? [mainScreen frame] : NSMakeRect(0, 0, 1920, 1080);
    NSRect visibleFrame = mainScreen ? [mainScreen visibleFrame] : frame;

    lpmi->rcMonitor.left = (LONG)frame.origin.x;
    lpmi->rcMonitor.top = 0;
    lpmi->rcMonitor.right = (LONG)(frame.origin.x + frame.size.width);
    lpmi->rcMonitor.bottom = (LONG)frame.size.height;

    lpmi->rcWork.left = (LONG)visibleFrame.origin.x;
    lpmi->rcWork.top = (LONG)(frame.size.height - (visibleFrame.origin.y + visibleFrame.size.height));
    lpmi->rcWork.right = (LONG)(visibleFrame.origin.x + visibleFrame.size.width);
    lpmi->rcWork.bottom = (LONG)(lpmi->rcWork.top + visibleFrame.size.height);

    lpmi->dwFlags = MONITORINFOF_PRIMARY;
    return TRUE;
}

/* =========================================================================
 * 8. macOS Application Delegate & Entry Point (main)
 * ========================================================================= */
@interface ZPAppDelegate : NSObject <NSApplicationDelegate>
- (void)onMenuItemTriggered:(NSMenuItem*)sender;
@end

@implementation ZPAppDelegate
- (void)onMenuItemTriggered:(NSMenuItem*)sender {
    if (g_main_hwnd) {
        SendMessageA(g_main_hwnd, WM_COMMAND, MAKEWPARAM((int)sender.tag, 0), 0);
    }
}
@end

static ZPAppDelegate *s_app_delegate = nil;

extern int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    ensure_nsapp();
    s_app_delegate = [[ZPAppDelegate alloc] init];
    [NSApp setDelegate:s_app_delegate];

    return WinMain(NULL, NULL, (LPSTR)"", SW_SHOWNORMAL);
}

#endif /* __APPLE__ */
