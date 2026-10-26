#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#include "win32_macos_shim.h"

/* =========================================================================
 * Internal GDI Structures for macOS
 * ========================================================================= */
#define ZP_OBJ_BITMAP 1
#define ZP_OBJ_PEN    2
#define ZP_OBJ_BRUSH  3
#define ZP_OBJ_FONT   4
#define ZP_OBJ_RGN    5

struct ZP_GDIOBJ__ {
    uint32_t type;
};

struct ZP_HBITMAP__ {
    uint32_t     type;
    int          width;
    int          height;
    uint32_t    *bits;
    CGContextRef cgContext;
    BOOL         ownsBits;
};

struct ZP_HPEN__ {
    uint32_t type;
    int      style;
    int      width;
    COLORREF color;
};

struct ZP_HBRUSH__ {
    uint32_t type;
    int      style;
    COLORREF color;
};

struct ZP_HFONT__ {
    uint32_t  type;
    CTFontRef ctFont;
    int       height;
    char      faceName[64];
};

struct ZP_HRGN__ {
    uint32_t type;
    CGRect   rect;
};

struct ZP_HDC__ {
    CGContextRef         ctx;
    struct ZP_HBITMAP__ *selectedBitmap;
    struct ZP_HPEN__    *selectedPen;
    struct ZP_HBRUSH__  *selectedBrush;
    struct ZP_HFONT__   *selectedFont;
    COLORREF             textColor;
    COLORREF             bkColor;
    int                  bkMode;
    POINT                curPos;
};

/* Stock Objects (Singletons) */
static struct ZP_HBRUSH__ g_stock_null_brush  = { ZP_OBJ_BRUSH, 1, 0 };
static struct ZP_HBRUSH__ g_stock_white_brush = { ZP_OBJ_BRUSH, 0, 0x00FFFFFF };
static struct ZP_HBRUSH__ g_stock_black_brush = { ZP_OBJ_BRUSH, 0, 0x00000000 };
static struct ZP_HPEN__   g_stock_null_pen    = { ZP_OBJ_PEN,   1, 0, 0 };

static CGColorRef create_cgcolor_from_colorref(COLORREF c, CGFloat alpha) {
    CGFloat r = (CGFloat)GetRValue(c) / 255.0f;
    CGFloat g = (CGFloat)GetGValue(c) / 255.0f;
    CGFloat b = (CGFloat)GetBValue(c) / 255.0f;
    return CGColorCreateGenericRGB(r, g, b, alpha);
}

/* =========================================================================
 * DC & Bitmap Management
 * ========================================================================= */
HDC CreateCompatibleDC(HDC hdc) {
    (void)hdc;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)calloc(1, sizeof(struct ZP_HDC__));
    if (!dc) return NULL;
    dc->selectedBrush = &g_stock_white_brush;
    dc->selectedPen   = (struct ZP_HPEN__*)CreatePen(0, 1, 0);
    dc->textColor     = 0x00000000; /* Black */
    dc->bkColor       = 0x00FFFFFF; /* White */
    dc->bkMode        = OPAQUE;
    return (HDC)dc;
}

BOOL DeleteDC(HDC hdc) {
    if (!hdc) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        free(dc->selectedPen);
    }
    free(dc);
    return TRUE;
}

HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO *pbmi, UINT usage, void **ppvBits, HANDLE hSection, DWORD offset) {
    (void)hdc; (void)usage; (void)hSection; (void)offset;
    if (!pbmi || !ppvBits) return NULL;

    int width = (int)pbmi->bmiHeader.biWidth;
    int height = (int)pbmi->bmiHeader.biHeight;
    int absH = height < 0 ? -height : height;
    if (width <= 0 || absH <= 0) return NULL;

    uint32_t *bits = (uint32_t*)calloc((size_t)width * (size_t)absH, sizeof(uint32_t));
    if (!bits) return NULL;

    *ppvBits = bits;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        bits,
        (size_t)width,
        (size_t)absH,
        8,
        (size_t)width * 4,
        colorSpace,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst
    );
    CGColorSpaceRelease(colorSpace);

    if (!ctx) {
        free(bits);
        *ppvBits = NULL;
        return NULL;
    }

    /* Top-down coordinate flip so (0,0) in CGContext matches bits[0] in memory */
    CGContextTranslateCTM(ctx, 0, (CGFloat)absH);
    CGContextScaleCTM(ctx, 1.0f, -1.0f);

    struct ZP_HBITMAP__ *bmp = (struct ZP_HBITMAP__*)calloc(1, sizeof(struct ZP_HBITMAP__));
    bmp->type = ZP_OBJ_BITMAP;
    bmp->width = width;
    bmp->height = absH;
    bmp->bits = bits;
    bmp->cgContext = ctx;
    bmp->ownsBits = TRUE;

    return (HBITMAP)bmp;
}

HBITMAP CreateCompatibleBitmap(HDC hdc, int cx, int cy) {
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy; /* Top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    return CreateDIBSection(hdc, &bmi, 0, &bits, NULL, 0);
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) {
    if (!hdc || !h) return NULL;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    struct ZP_GDIOBJ__ *obj = (struct ZP_GDIOBJ__*)h;

    if (obj->type == ZP_OBJ_BITMAP) {
        struct ZP_HBITMAP__ *bmp = (struct ZP_HBITMAP__*)obj;
        HGDIOBJ old = dc->selectedBitmap;
        dc->selectedBitmap = bmp;
        dc->ctx = bmp->cgContext;
        return old;
    } else if (obj->type == ZP_OBJ_PEN) {
        struct ZP_HPEN__ *pen = (struct ZP_HPEN__*)obj;
        HGDIOBJ old = dc->selectedPen;
        dc->selectedPen = pen;
        return old;
    } else if (obj->type == ZP_OBJ_BRUSH) {
        struct ZP_HBRUSH__ *br = (struct ZP_HBRUSH__*)obj;
        HGDIOBJ old = dc->selectedBrush;
        dc->selectedBrush = br;
        return old;
    } else if (obj->type == ZP_OBJ_FONT) {
        struct ZP_HFONT__ *f = (struct ZP_HFONT__*)obj;
        HGDIOBJ old = dc->selectedFont;
        dc->selectedFont = f;
        return old;
    }

    return NULL;
}

BOOL DeleteObject(HGDIOBJ ho) {
    if (!ho) return FALSE;
    if (ho == &g_stock_null_brush || ho == &g_stock_white_brush ||
        ho == &g_stock_black_brush || ho == &g_stock_null_pen) {
        return TRUE;
    }

    struct ZP_GDIOBJ__ *obj = (struct ZP_GDIOBJ__*)ho;
    if (obj->type == ZP_OBJ_BITMAP) {
        struct ZP_HBITMAP__ *bmp = (struct ZP_HBITMAP__*)obj;
        if (bmp->cgContext) CGContextRelease(bmp->cgContext);
        if (bmp->ownsBits && bmp->bits) free(bmp->bits);
        free(bmp);
        return TRUE;
    } else if (obj->type == ZP_OBJ_FONT) {
        struct ZP_HFONT__ *f = (struct ZP_HFONT__*)obj;
        if (f->ctFont) CFRelease(f->ctFont);
        free(f);
        return TRUE;
    } else if (obj->type == ZP_OBJ_PEN || obj->type == ZP_OBJ_BRUSH || obj->type == ZP_OBJ_RGN) {
        free(obj);
        return TRUE;
    }

    return TRUE;
}

HGDIOBJ GetStockObject(int i) {
    switch (i) {
        case NULL_BRUSH:  return (HGDIOBJ)&g_stock_null_brush;
        case WHITE_BRUSH: return (HGDIOBJ)&g_stock_white_brush;
        case BLACK_BRUSH: return (HGDIOBJ)&g_stock_black_brush;
        case NULL_PEN:    return (HGDIOBJ)&g_stock_null_pen;
        default:          return (HGDIOBJ)&g_stock_white_brush;
    }
}

HGDIOBJ GetCurrentObject(HDC hdc, UINT type) {
    if (!hdc) return NULL;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    switch (type) {
        case OBJ_PEN:    return (HGDIOBJ)dc->selectedPen;
        case OBJ_BRUSH:  return (HGDIOBJ)dc->selectedBrush;
        case OBJ_FONT:   return (HGDIOBJ)dc->selectedFont;
        case OBJ_BITMAP: return (HGDIOBJ)dc->selectedBitmap;
        default:         return (HGDIOBJ)dc->selectedBitmap;
    }
}

CGImageRef zp_macos_create_hdc_image(HDC hdc) {
    if (!hdc) return NULL;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (dc->selectedBitmap && dc->selectedBitmap->cgContext) {
        return CGBitmapContextCreateImage(dc->selectedBitmap->cgContext);
    }
    return NULL;
}

CGContextRef zp_macos_get_hdc_cgcontext(HDC hdc) {
    if (!hdc) return NULL;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (dc->ctx) return dc->ctx;
    if (dc->selectedBitmap && dc->selectedBitmap->cgContext) {
        return dc->selectedBitmap->cgContext;
    }
    return NULL;
}

int GetObjectA(HGDIOBJ hgdiobj, int cbBuffer, LPVOID lpvObject) {
    if (!hgdiobj) return 0;
    struct ZP_GDIOBJ__ *obj = (struct ZP_GDIOBJ__*)hgdiobj;
    if (obj->type == ZP_OBJ_BITMAP) {
        if (!lpvObject) return (int)sizeof(BITMAP);
        struct ZP_HBITMAP__ *bmp = (struct ZP_HBITMAP__*)obj;
        BITMAP bm;
        memset(&bm, 0, sizeof(bm));
        bm.bmWidth = bmp->width;
        bm.bmHeight = bmp->height;
        bm.bmWidthBytes = bmp->width * 4;
        bm.bmPlanes = 1;
        bm.bmBitsPixel = 32;
        bm.bmBits = bmp->bits;
        int copySz = (int)(cbBuffer < (int)sizeof(BITMAP) ? cbBuffer : (int)sizeof(BITMAP));
        memcpy(lpvObject, &bm, (size_t)copySz);
        return copySz;
    } else if (obj->type == ZP_OBJ_PEN) {
        if (!lpvObject) return (int)sizeof(LOGPEN);
        struct ZP_HPEN__ *pen = (struct ZP_HPEN__*)obj;
        LOGPEN lp;
        lp.lopnStyle = (UINT)pen->style;
        lp.lopnWidth.x = pen->width;
        lp.lopnWidth.y = 0;
        lp.lopnColor = pen->color;
        int copySz = (int)(cbBuffer < (int)sizeof(LOGPEN) ? cbBuffer : (int)sizeof(LOGPEN));
        memcpy(lpvObject, &lp, (size_t)copySz);
        return copySz;
    }
    return 0;
}

/* =========================================================================
 * Drawing Tools: Pens, Brushes, Fonts, Regions
 * ========================================================================= */
HPEN CreatePen(int iStyle, int cWidth, COLORREF color) {
    struct ZP_HPEN__ *pen = (struct ZP_HPEN__*)malloc(sizeof(struct ZP_HPEN__));
    if (!pen) return NULL;
    pen->type  = ZP_OBJ_PEN;
    pen->style = iStyle;
    pen->width = cWidth > 0 ? cWidth : 1;
    pen->color = color;
    return (HPEN)pen;
}

HPEN CreatePenIndirect(const LOGPEN *plp) {
    if (!plp) return NULL;
    return CreatePen((int)plp->lopnStyle, (int)plp->lopnWidth.x, plp->lopnColor);
}

HPEN ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH *plbrush, DWORD cStyle, const DWORD *pstyle) {
    (void)cStyle; (void)pstyle;
    COLORREF col = plbrush ? plbrush->lbColor : 0;
    return CreatePen((int)iPenStyle, (int)cWidth, col);
}

HBRUSH CreateSolidBrush(COLORREF color) {
    struct ZP_HBRUSH__ *br = (struct ZP_HBRUSH__*)malloc(sizeof(struct ZP_HBRUSH__));
    if (!br) return NULL;
    br->type  = ZP_OBJ_BRUSH;
    br->style = 0;
    br->color = color;
    return (HBRUSH)br;
}

HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
                  DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
                  DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
                  DWORD iPitchAndFamily, LPCSTR pszFaceName) {
    (void)cWidth; (void)cEscapement; (void)cOrientation; (void)bUnderline; (void)bStrikeOut;
    (void)iCharSet; (void)iOutPrecision; (void)iClipPrecision; (void)iQuality; (void)iPitchAndFamily;

    CGFloat pointSize = (CGFloat)(cHeight < 0 ? -cHeight : cHeight);
    if (pointSize <= 0) pointSize = 12.0f;

    CFStringRef faceNameCF = CFStringCreateWithCString(kCFAllocatorDefault,
                                pszFaceName ? pszFaceName : "Helvetica",
                                kCFStringEncodingUTF8);

    CTFontRef baseFont = CTFontCreateWithName(faceNameCF, pointSize, NULL);
    CFRelease(faceNameCF);

    if (!baseFont) {
        NSFont *sysFont = [NSFont systemFontOfSize:pointSize];
        baseFont = sysFont ? (CTFontRef)CFRetain((__bridge CTFontRef)sysFont) : NULL;
    }

    CTFontSymbolicTraits traits = 0;
    if (cWeight >= FW_BOLD) traits |= kCTFontBoldTrait;
    if (bItalic) traits |= kCTFontItalicTrait;

    CTFontRef styledFont = baseFont;
    if (traits != 0) {
        styledFont = CTFontCreateCopyWithSymbolicTraits(baseFont, pointSize, NULL, traits, traits);
        if (!styledFont) styledFont = baseFont;
        else CFRelease(baseFont);
    }

    struct ZP_HFONT__ *f = (struct ZP_HFONT__*)malloc(sizeof(struct ZP_HFONT__));
    f->type = ZP_OBJ_FONT;
    f->ctFont = styledFont;
    f->height = (int)pointSize;
    strncpy(f->faceName, pszFaceName ? pszFaceName : "", sizeof(f->faceName) - 1);
    f->faceName[sizeof(f->faceName) - 1] = '\0';
    return (HFONT)f;
}

HANDLE AddFontMemResourceEx(void *pFileView, DWORD cjSize, void *pvResrved, DWORD *pNumFonts) {
    (void)pvResrved;
    if (!pFileView || cjSize == 0) return NULL;

    NSData *data = [NSData dataWithBytes:pFileView length:(NSUInteger)cjSize];
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    if (!provider) return NULL;

    CGFontRef font = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);
    if (!font) return NULL;

    CFErrorRef error = NULL;
    BOOL ok = CTFontManagerRegisterGraphicsFont(font, &error);
    if (!ok && error) {
        CFRelease(error);
    }
    CGFontRelease(font);

    if (pNumFonts) *pNumFonts = 1;
    return (HANDLE)0x1;
}

BOOL RemoveFontMemResourceEx(HANDLE h) {
    (void)h;
    return TRUE;
}

COLORREF SetTextColor(HDC hdc, COLORREF color) {
    if (!hdc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    COLORREF old = dc->textColor;
    dc->textColor = color;
    return old;
}

COLORREF SetBkColor(HDC hdc, COLORREF color) {
    if (!hdc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    COLORREF old = dc->bkColor;
    dc->bkColor = color;
    return old;
}

int SetBkMode(HDC hdc, int mode) {
    if (!hdc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    int old = dc->bkMode;
    dc->bkMode = mode;
    return old;
}

BOOL SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt) {
    (void)hdc; (void)x; (void)y;
    if (lppt) { lppt->x = 0; lppt->y = 0; }
    return TRUE;
}

int SetROP2(HDC hdc, int rop2) {
    (void)hdc;
    return rop2;
}

HRGN CreateRectRgn(int x1, int y1, int x2, int y2) {
    struct ZP_HRGN__ *rgn = (struct ZP_HRGN__*)malloc(sizeof(struct ZP_HRGN__));
    if (!rgn) return NULL;
    rgn->type = ZP_OBJ_RGN;
    rgn->rect = CGRectMake((CGFloat)x1, (CGFloat)y1, (CGFloat)(x2 - x1), (CGFloat)(y2 - y1));
    return (HRGN)rgn;
}

int SelectClipRgn(HDC hdc, HRGN hrgn) {
    if (!hdc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (!dc->ctx) return 0;
    if (!hrgn) {
        CGContextResetClip(dc->ctx);
        return 1;
    }
    struct ZP_HRGN__ *rgn = (struct ZP_HRGN__*)hrgn;
    CGContextClipToRect(dc->ctx, rgn->rect);
    return 1;
}

/* =========================================================================
 * 2D Primitives: Lines, Rectangles, Rounded Rects, Curves, Pixels
 * ========================================================================= */
BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT lppt) {
    if (!hdc) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (lppt) *lppt = dc->curPos;
    dc->curPos.x = x;
    dc->curPos.y = y;
    return TRUE;
}

BOOL LineTo(HDC hdc, int x, int y) {
    if (!hdc) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);

        /* Half-pixel offset for crisp 1px lines */
        CGFloat offset = (dc->selectedPen->width % 2 == 1) ? 0.5f : 0.0f;
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, (CGFloat)dc->curPos.x + offset, (CGFloat)dc->curPos.y + offset);
        CGContextAddLineToPoint(ctx, (CGFloat)x + offset, (CGFloat)y + offset);
        CGContextStrokePath(ctx);

        CGColorRelease(strokeCol);
    }

    dc->curPos.x = x;
    dc->curPos.y = y;
    return TRUE;
}

BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom) {
    if (!hdc) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    CGFloat w = (CGFloat)(right - left);
    CGFloat h = (CGFloat)(bottom - top);
    if (w <= 0 || h <= 0) return TRUE;

    CGRect r = CGRectMake((CGFloat)left, (CGFloat)top, w, h);

    if (dc->selectedBrush && dc->selectedBrush != &g_stock_null_brush) {
        CGColorRef fillCol = create_cgcolor_from_colorref(dc->selectedBrush->color, 1.0f);
        CGContextSetFillColorWithColor(ctx, fillCol);
        CGContextFillRect(ctx, r);
        CGColorRelease(fillCol);
    }

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);

        CGFloat offset = 0.5f;
        CGRect strokeR = CGRectMake((CGFloat)left + offset, (CGFloat)top + offset, w - 1.0f, h - 1.0f);
        CGContextStrokeRect(ctx, strokeR);
        CGColorRelease(strokeCol);
    }

    return TRUE;
}

BOOL RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height) {
    if (!hdc) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    CGFloat w = (CGFloat)(right - left);
    CGFloat h = (CGFloat)(bottom - top);
    if (w <= 0 || h <= 0) return TRUE;

    CGFloat radius = (CGFloat)(width / 2);
    if (radius <= 0) radius = 4.0f;

    CGRect r = CGRectMake((CGFloat)left, (CGFloat)top, w, h);
    CGPathRef path = CGPathCreateWithRoundedRect(r, radius, radius, NULL);

    if (dc->selectedBrush && dc->selectedBrush != &g_stock_null_brush) {
        CGColorRef fillCol = create_cgcolor_from_colorref(dc->selectedBrush->color, 1.0f);
        CGContextSetFillColorWithColor(ctx, fillCol);
        CGContextAddPath(ctx, path);
        CGContextFillPath(ctx);
        CGColorRelease(fillCol);
    }

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);

        CGFloat offset = 0.5f;
        CGRect strokeR = CGRectMake((CGFloat)left + offset, (CGFloat)top + offset, w - 1.0f, h - 1.0f);
        CGPathRef strokePath = CGPathCreateWithRoundedRect(strokeR, radius, radius, NULL);
        CGContextAddPath(ctx, strokePath);
        CGContextStrokePath(ctx);
        CGPathRelease(strokePath);
        CGColorRelease(strokeCol);
    }

    CGPathRelease(path);
    return TRUE;
}

BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom) {
    if (!hdc) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    CGRect r = CGRectMake((CGFloat)left, (CGFloat)top, (CGFloat)(right - left), (CGFloat)(bottom - top));

    if (dc->selectedBrush && dc->selectedBrush != &g_stock_null_brush) {
        CGColorRef fillCol = create_cgcolor_from_colorref(dc->selectedBrush->color, 1.0f);
        CGContextSetFillColorWithColor(ctx, fillCol);
        CGContextFillEllipseInRect(ctx, r);
        CGColorRelease(fillCol);
    }

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);
        CGContextStrokeEllipseInRect(ctx, r);
        CGColorRelease(strokeCol);
    }

    return TRUE;
}

BOOL Polygon(HDC hdc, const POINT *apt, int cpt) {
    if (!hdc || !apt || cpt < 3) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, NULL, (CGFloat)apt[0].x, (CGFloat)apt[0].y);
    for (int i = 1; i < cpt; ++i) {
        CGPathAddLineToPoint(path, NULL, (CGFloat)apt[i].x, (CGFloat)apt[i].y);
    }
    CGPathCloseSubpath(path);

    if (dc->selectedBrush && dc->selectedBrush != &g_stock_null_brush) {
        CGColorRef fillCol = create_cgcolor_from_colorref(dc->selectedBrush->color, 1.0f);
        CGContextSetFillColorWithColor(ctx, fillCol);
        CGContextAddPath(ctx, path);
        CGContextFillPath(ctx);
        CGColorRelease(fillCol);
    }

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);
        CGContextAddPath(ctx, path);
        CGContextStrokePath(ctx);
        CGColorRelease(strokeCol);
    }

    CGPathRelease(path);
    return TRUE;
}

BOOL PolyBezier(HDC hdc, const POINT *apt, DWORD cpt) {
    if (!hdc || !apt || cpt < 4 || (cpt - 1) % 3 != 0) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);

        CGFloat offset = (dc->selectedPen->width % 2 == 1) ? 0.5f : 0.0f;
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, (CGFloat)apt[0].x + offset, (CGFloat)apt[0].y + offset);
        for (DWORD i = 1; i < cpt; i += 3) {
            CGContextAddCurveToPoint(ctx,
                (CGFloat)apt[i].x + offset, (CGFloat)apt[i].y + offset,
                (CGFloat)apt[i+1].x + offset, (CGFloat)apt[i+1].y + offset,
                (CGFloat)apt[i+2].x + offset, (CGFloat)apt[i+2].y + offset);
        }
        CGContextStrokePath(ctx);
        CGColorRelease(strokeCol);
    }
    dc->curPos.x = apt[cpt - 1].x;
    dc->curPos.y = apt[cpt - 1].y;
    return TRUE;
}

BOOL PolyBezierTo(HDC hdc, const POINT *apt, DWORD cpt) {
    if (!hdc || !apt || cpt < 3 || cpt % 3 != 0) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);

        CGFloat offset = (dc->selectedPen->width % 2 == 1) ? 0.5f : 0.0f;
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, (CGFloat)dc->curPos.x + offset, (CGFloat)dc->curPos.y + offset);
        for (DWORD i = 0; i < cpt; i += 3) {
            CGContextAddCurveToPoint(ctx,
                (CGFloat)apt[i].x + offset, (CGFloat)apt[i].y + offset,
                (CGFloat)apt[i+1].x + offset, (CGFloat)apt[i+1].y + offset,
                (CGFloat)apt[i+2].x + offset, (CGFloat)apt[i+2].y + offset);
        }
        CGContextStrokePath(ctx);
        CGColorRelease(strokeCol);
    }
    dc->curPos.x = apt[cpt - 1].x;
    dc->curPos.y = apt[cpt - 1].y;
    return TRUE;
}

BOOL BeginPath(HDC hdc) {
    CGContextRef ctx = zp_macos_get_hdc_cgcontext(hdc);
    if (!ctx) return FALSE;
    CGContextBeginPath(ctx);
    return TRUE;
}

BOOL CloseFigure(HDC hdc) {
    CGContextRef ctx = zp_macos_get_hdc_cgcontext(hdc);
    if (!ctx) return FALSE;
    CGContextClosePath(ctx);
    return TRUE;
}

BOOL EndPath(HDC hdc) {
    (void)hdc;
    return TRUE;
}

BOOL FillPath(HDC hdc) {
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (!dc || !dc->ctx) return FALSE;
    if (dc->selectedBrush && dc->selectedBrush != &g_stock_null_brush) {
        CGColorRef fillCol = create_cgcolor_from_colorref(dc->selectedBrush->color, 1.0f);
        CGContextSetFillColorWithColor(dc->ctx, fillCol);
        CGContextFillPath(dc->ctx);
        CGColorRelease(fillCol);
    }
    return TRUE;
}

BOOL StrokePath(HDC hdc) {
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (!dc || !dc->ctx) return FALSE;
    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(dc->ctx, strokeCol);
        CGContextSetLineWidth(dc->ctx, (CGFloat)dc->selectedPen->width);
        CGContextStrokePath(dc->ctx);
        CGColorRelease(strokeCol);
    }
    return TRUE;
}

BOOL StrokeAndFillPath(HDC hdc) {
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (!dc || !dc->ctx) return FALSE;
    if (dc->selectedBrush && dc->selectedBrush != &g_stock_null_brush) {
        CGColorRef fillCol = create_cgcolor_from_colorref(dc->selectedBrush->color, 1.0f);
        CGContextSetFillColorWithColor(dc->ctx, fillCol);
        CGColorRelease(fillCol);
    }
    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(dc->ctx, strokeCol);
        CGContextSetLineWidth(dc->ctx, (CGFloat)dc->selectedPen->width);
        CGColorRelease(strokeCol);
    }
    CGContextDrawPath(dc->ctx, kCGPathFillStroke);
    return TRUE;
}

COLORREF SetPixel(HDC hdc, int x, int y, COLORREF color) {
    if (!hdc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (dc->selectedBitmap && dc->selectedBitmap->bits) {
        if (x >= 0 && x < dc->selectedBitmap->width && y >= 0 && y < dc->selectedBitmap->height) {
            uint32_t val = (0xFF000000) | ((uint32_t)GetRValue(color) << 16) | ((uint32_t)GetGValue(color) << 8) | (uint32_t)GetBValue(color);
            dc->selectedBitmap->bits[y * dc->selectedBitmap->width + x] = val;
            return color;
        }
    }
    return 0;
}

COLORREF GetPixel(HDC hdc, int x, int y) {
    if (!hdc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    if (dc->selectedBitmap && dc->selectedBitmap->bits) {
        if (x >= 0 && x < dc->selectedBitmap->width && y >= 0 && y < dc->selectedBitmap->height) {
            uint32_t val = dc->selectedBitmap->bits[y * dc->selectedBitmap->width + x];
            BYTE b = (BYTE)(val & 0xFF);
            BYTE g = (BYTE)((val >> 8) & 0xFF);
            BYTE r = (BYTE)((val >> 16) & 0xFF);
            return RGB(r, g, b);
        }
    }
    return 0;
}

int GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage) {
    (void)hdc; (void)start; (void)cLines; (void)usage;
    if (!hbm || !lpbmi) return 0;
    struct ZP_HBITMAP__ *bmp = (struct ZP_HBITMAP__*)hbm;
    if (!lpvBits) {
        lpbmi->bmiHeader.biWidth = bmp->width;
        lpbmi->bmiHeader.biHeight = bmp->height;
        lpbmi->bmiHeader.biPlanes = 1;
        lpbmi->bmiHeader.biBitCount = 32;
        lpbmi->bmiHeader.biCompression = BI_RGB;
        lpbmi->bmiHeader.biSizeImage = (DWORD)(bmp->width * bmp->height * 4);
        return bmp->height;
    }
    if (bmp->bits && lpvBits) {
        memcpy(lpvBits, bmp->bits, (size_t)bmp->width * (size_t)bmp->height * 4);
        return bmp->height;
    }
    return 0;
}

int FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr) {
    if (!hdc || !lprc || !hbr) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return 0;

    struct ZP_HBRUSH__ *br = (struct ZP_HBRUSH__*)hbr;
    if (br == &g_stock_null_brush) return 1;

    CGFloat w = (CGFloat)(lprc->right - lprc->left);
    CGFloat h = (CGFloat)(lprc->bottom - lprc->top);
    if (w <= 0 || h <= 0) return 1;

    CGRect r = CGRectMake((CGFloat)lprc->left, (CGFloat)lprc->top, w, h);

    CGContextSaveGState(ctx);
    CGContextSetShouldAntialias(ctx, false);
    CGColorRef fillCol = create_cgcolor_from_colorref(br->color, 1.0f);
    CGContextSetFillColorWithColor(ctx, fillCol);
    CGContextFillRect(ctx, r);
    CGColorRelease(fillCol);
    CGContextRestoreGState(ctx);

    return 1;
}

/* =========================================================================
 * Block Transfers (BitBlt, StretchBlt, AlphaBlend)
 * ========================================================================= */
BOOL BitBlt(HDC hdcDest, int xDest, int yDest, int w, int h, HDC hdcSrc, int xSrc, int ySrc, DWORD rop) {
    if (!hdcDest || !hdcSrc || w <= 0 || h <= 0) return FALSE;
    struct ZP_HDC__ *dDest = (struct ZP_HDC__*)hdcDest;
    struct ZP_HDC__ *dSrc  = (struct ZP_HDC__*)hdcSrc;

    if (!dDest->ctx || !dSrc->selectedBitmap || !dSrc->selectedBitmap->cgContext) return FALSE;

    CGImageRef fullSrcImg = CGBitmapContextCreateImage(dSrc->selectedBitmap->cgContext);
    if (!fullSrcImg) return FALSE;

    CGRect srcSubRect = CGRectMake((CGFloat)xSrc, (CGFloat)ySrc, (CGFloat)w, (CGFloat)h);
    CGImageRef subImg = CGImageCreateWithImageInRect(fullSrcImg, srcSubRect);
    CGImageRelease(fullSrcImg);
    if (!subImg) return FALSE;

    CGContextSaveGState(dDest->ctx);
    if (rop == BLACKNESS) {
        CGContextSetRGBFillColor(dDest->ctx, 0, 0, 0, 1);
        CGContextFillRect(dDest->ctx, CGRectMake((CGFloat)xDest, (CGFloat)yDest, (CGFloat)w, (CGFloat)h));
    } else if (rop == WHITENESS) {
        CGContextSetRGBFillColor(dDest->ctx, 1, 1, 1, 1);
        CGContextFillRect(dDest->ctx, CGRectMake((CGFloat)xDest, (CGFloat)yDest, (CGFloat)w, (CGFloat)h));
    } else {
        CGContextTranslateCTM(dDest->ctx, (CGFloat)xDest, (CGFloat)(yDest + h));
        CGContextScaleCTM(dDest->ctx, 1.0f, -1.0f);
        CGContextDrawImage(dDest->ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), subImg);
    }
    CGContextRestoreGState(dDest->ctx);

    CGImageRelease(subImg);
    return TRUE;
}

BOOL StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop) {
    (void)rop;
    if (!hdcDest || !hdcSrc || wDest <= 0 || hDest <= 0 || wSrc <= 0 || hSrc <= 0) return FALSE;
    struct ZP_HDC__ *dDest = (struct ZP_HDC__*)hdcDest;
    struct ZP_HDC__ *dSrc  = (struct ZP_HDC__*)hdcSrc;

    if (!dDest->ctx || !dSrc->selectedBitmap || !dSrc->selectedBitmap->cgContext) return FALSE;

    CGImageRef fullSrcImg = CGBitmapContextCreateImage(dSrc->selectedBitmap->cgContext);
    if (!fullSrcImg) return FALSE;

    CGRect srcSubRect = CGRectMake((CGFloat)xSrc, (CGFloat)ySrc, (CGFloat)wSrc, (CGFloat)hSrc);
    CGImageRef subImg = CGImageCreateWithImageInRect(fullSrcImg, srcSubRect);
    CGImageRelease(fullSrcImg);
    if (!subImg) return FALSE;

    CGContextSaveGState(dDest->ctx);
    CGContextTranslateCTM(dDest->ctx, (CGFloat)xDest, (CGFloat)(yDest + hDest));
    CGContextScaleCTM(dDest->ctx, 1.0f, -1.0f);
    CGContextDrawImage(dDest->ctx, CGRectMake(0, 0, (CGFloat)wDest, (CGFloat)hDest), subImg);
    CGContextRestoreGState(dDest->ctx);

    CGImageRelease(subImg);
    return TRUE;
}

BOOL AlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest,
                HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc,
                BLENDFUNCTION ftn) {
    if (!hdcDest || !hdcSrc || wDest <= 0 || hDest <= 0) return FALSE;
    struct ZP_HDC__ *dDest = (struct ZP_HDC__*)hdcDest;
    struct ZP_HDC__ *dSrc  = (struct ZP_HDC__*)hdcSrc;

    if (!dDest->ctx || !dSrc->selectedBitmap || !dSrc->selectedBitmap->cgContext) return FALSE;

    CGImageRef fullSrcImg = CGBitmapContextCreateImage(dSrc->selectedBitmap->cgContext);
    if (!fullSrcImg) return FALSE;

    CGRect srcSubRect = CGRectMake((CGFloat)xoriginSrc, (CGFloat)yoriginSrc, (CGFloat)wSrc, (CGFloat)hSrc);
    CGImageRef subImg = CGImageCreateWithImageInRect(fullSrcImg, srcSubRect);
    CGImageRelease(fullSrcImg);
    if (!subImg) return FALSE;

    CGFloat globalAlpha = (CGFloat)ftn.SourceConstantAlpha / 255.0f;

    CGContextSaveGState(dDest->ctx);
    CGContextSetAlpha(dDest->ctx, globalAlpha);
    CGContextTranslateCTM(dDest->ctx, (CGFloat)xoriginDest, (CGFloat)(yoriginDest + hDest));
    CGContextScaleCTM(dDest->ctx, 1.0f, -1.0f);
    CGContextDrawImage(dDest->ctx, CGRectMake(0, 0, (CGFloat)wDest, (CGFloat)hDest), subImg);
    CGContextRestoreGState(dDest->ctx);

    CGImageRelease(subImg);
    return TRUE;
}

/* =========================================================================
 * Typography & Text Layout (CoreText)
 * ========================================================================= */
int DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format) {
    if (!hdc || !lpchText || !lprc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;

    size_t len = (cchText < 0) ? strlen(lpchText) : (size_t)cchText;
    if (len == 0) return 0;

    CFStringRef str = CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)lpchText, len, kCFStringEncodingUTF8, false);
    if (!str) {
        str = CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)lpchText, len, kCFStringEncodingWindowsLatin1, false);
    }
    if (!str) return 0;

    CTFontRef font = dc->selectedFont ? dc->selectedFont->ctFont : NULL;
    if (!font) {
        NSFont *sysFont = [NSFont systemFontOfSize:12.0f];
        font = sysFont ? (CTFontRef)CFRetain((__bridge CTFontRef)sysFont) : NULL;
    } else {
        CFRetain(font);
    }

    CGColorRef textCol = create_cgcolor_from_colorref(dc->textColor, 1.0f);

    const void *keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    const void *values[] = { font, textCol };
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CFAttributedStringRef attrStr = CFAttributedStringCreate(kCFAllocatorDefault, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    CGFloat ascent = 0, descent = 0, leading = 0;
    double lineWidth = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    CGFloat lineHeight = ascent + descent;

    if (format & DT_CALCRECT) {
        lprc->right = lprc->left + (LONG)ceil(lineWidth);
        lprc->bottom = lprc->top + (LONG)ceil(lineHeight);
        CFRelease(line);
        CFRelease(attrStr);
        CFRelease(attrs);
        CGColorRelease(textCol);
        CFRelease(font);
        CFRelease(str);
        return (int)lineHeight;
    }

    CGContextRef ctx = dc->ctx;
    if (ctx) {
        CGFloat x = (CGFloat)lprc->left;
        CGFloat y = (CGFloat)lprc->top;

        if (format & DT_CENTER) {
            x = (CGFloat)lprc->left + ((CGFloat)(lprc->right - lprc->left) - (CGFloat)lineWidth) / 2.0f;
        } else if (format & DT_RIGHT) {
            x = (CGFloat)lprc->right - (CGFloat)lineWidth;
        }

        if (format & DT_VCENTER) {
            y = (CGFloat)lprc->top + ((CGFloat)(lprc->bottom - lprc->top) - lineHeight) / 2.0f;
        }

        CGContextSaveGState(ctx);
        CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0f, -1.0f));
        CGContextSetTextPosition(ctx, x, y + ascent);
        CTLineDraw(line, ctx);
        CGContextRestoreGState(ctx);
    }

    CFRelease(line);
    CFRelease(attrStr);
    CFRelease(attrs);
    CGColorRelease(textCol);
    CFRelease(font);
    CFRelease(str);

    return (int)lineHeight;
}

int DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format) {
    if (!lpchText) return 0;
    size_t len = (cchText < 0) ? wcslen(lpchText) : (size_t)cchText;
    char buf[1024];
    size_t converted = wcstombs(buf, lpchText, sizeof(buf) - 1);
    if (converted == (size_t)-1) return 0;
    buf[converted] = '\0';
    return DrawTextA(hdc, buf, (int)converted, lprc, format);
}

BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE lpsz) {
    if (!lpString || !lpsz) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;

    size_t len = (c < 0) ? strlen(lpString) : (size_t)c;
    if (len == 0) {
        lpsz->cx = 0;
        lpsz->cy = dc && dc->selectedFont ? dc->selectedFont->height : 12;
        return TRUE;
    }

    CFStringRef str = CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)lpString, len, kCFStringEncodingUTF8, false);
    if (!str) str = CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)lpString, len, kCFStringEncodingWindowsLatin1, false);
    if (!str) return FALSE;

    CTFontRef font = (dc && dc->selectedFont) ? dc->selectedFont->ctFont : NULL;
    if (!font) {
        NSFont *sysFont = [NSFont systemFontOfSize:12.0f];
        font = sysFont ? (CTFontRef)CFRetain((__bridge CTFontRef)sysFont) : NULL;
    }
    else CFRetain(font);

    CFStringRef keys[] = { kCTFontAttributeName };
    const void *values[] = { font };
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys, values, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CFAttributedStringRef attrStr = CFAttributedStringCreate(kCFAllocatorDefault, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);

    CGFloat ascent = 0, descent = 0, leading = 0;
    double width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

    lpsz->cx = (LONG)ceil(width);
    lpsz->cy = (LONG)ceil(ascent + descent);

    CFRelease(line);
    CFRelease(attrStr);
    CFRelease(attrs);
    CFRelease(font);
    CFRelease(str);

    return TRUE;
}

BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE lpsz) {
    if (!lpString) return FALSE;
    char buf[1024];
    size_t converted = wcstombs(buf, lpString, sizeof(buf) - 1);
    if (converted == (size_t)-1) return FALSE;
    buf[converted] = '\0';
    return GetTextExtentPoint32A(hdc, buf, (int)converted, lpsz);
}

BOOL GetTextMetricsA(HDC hdc, LPTEXTMETRICA lptm) {
    if (!lptm) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    int h = (dc && dc->selectedFont) ? dc->selectedFont->height : 12;
    memset(lptm, 0, sizeof(TEXTMETRICA));
    lptm->tmHeight = h;
    lptm->tmAscent = (LONG)(h * 0.8);
    lptm->tmDescent = h - lptm->tmAscent;
    lptm->tmAveCharWidth = (LONG)(h * 0.5);
    return TRUE;
}

/* =========================================================================
 * RECT & Geometry Helper Functions
 * ========================================================================= */
BOOL SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom) {
    if (!lprc) return FALSE;
    lprc->left = xLeft; lprc->top = yTop; lprc->right = xRight; lprc->bottom = yBottom;
    return TRUE;
}

BOOL OffsetRect(LPRECT lprc, int dx, int dy) {
    if (!lprc) return FALSE;
    lprc->left += dx; lprc->right += dx;
    lprc->top += dy; lprc->bottom += dy;
    return TRUE;
}

BOOL InflateRect(LPRECT lprc, int dx, int dy) {
    if (!lprc) return FALSE;
    lprc->left -= dx; lprc->right += dx;
    lprc->top -= dy; lprc->bottom += dy;
    return TRUE;
}

BOOL PtInRect(const RECT *lprc, POINT pt) {
    if (!lprc) return FALSE;
    return (pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom);
}

BOOL IntersectRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2) {
    if (!lprcDst || !lprcSrc1 || !lprcSrc2) return FALSE;
    lprcDst->left   = lprcSrc1->left > lprcSrc2->left ? lprcSrc1->left : lprcSrc2->left;
    lprcDst->top    = lprcSrc1->top > lprcSrc2->top ? lprcSrc1->top : lprcSrc2->top;
    lprcDst->right  = lprcSrc1->right < lprcSrc2->right ? lprcSrc1->right : lprcSrc2->right;
    lprcDst->bottom = lprcSrc1->bottom < lprcSrc2->bottom ? lprcSrc1->bottom : lprcSrc2->bottom;
    if (lprcDst->right <= lprcDst->left || lprcDst->bottom <= lprcDst->top) {
        memset(lprcDst, 0, sizeof(RECT));
        return FALSE;
    }
    return TRUE;
}

BOOL UnionRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2) {
    if (!lprcDst || !lprcSrc1 || !lprcSrc2) return FALSE;
    lprcDst->left   = lprcSrc1->left < lprcSrc2->left ? lprcSrc1->left : lprcSrc2->left;
    lprcDst->top    = lprcSrc1->top < lprcSrc2->top ? lprcSrc1->top : lprcSrc2->top;
    lprcDst->right  = lprcSrc1->right > lprcSrc2->right ? lprcSrc1->right : lprcSrc2->right;
    lprcDst->bottom = lprcSrc1->bottom > lprcSrc2->bottom ? lprcSrc1->bottom : lprcSrc2->bottom;
    return TRUE;
}

BOOL EqualRect(const RECT *lprc1, const RECT *lprc2) {
    if (!lprc1 || !lprc2) return FALSE;
    return (lprc1->left == lprc2->left && lprc1->top == lprc2->top &&
            lprc1->right == lprc2->right && lprc1->bottom == lprc2->bottom);
}

BOOL IsRectEmpty(const RECT *lprc) {
    if (!lprc) return TRUE;
    return (lprc->left >= lprc->right || lprc->top >= lprc->bottom);
}

int MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoints, UINT cPoints) {
    (void)hWndFrom; (void)hWndTo; (void)lpPoints; (void)cPoints;
    return 1;
}

BOOL Polyline(HDC hdc, const POINT *apt, int cpt) {
    if (!hdc || !apt || cpt < 2) return FALSE;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hdc;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return FALSE;

    if (dc->selectedPen && dc->selectedPen != &g_stock_null_pen) {
        CGColorRef strokeCol = create_cgcolor_from_colorref(dc->selectedPen->color, 1.0f);
        CGContextSetStrokeColorWithColor(ctx, strokeCol);
        CGContextSetLineWidth(ctx, (CGFloat)dc->selectedPen->width);

        CGFloat offset = (dc->selectedPen->width % 2 == 1) ? 0.5f : 0.0f;
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, (CGFloat)apt[0].x + offset, (CGFloat)apt[0].y + offset);
        for (int i = 1; i < cpt; ++i) {
            CGContextAddLineToPoint(ctx, (CGFloat)apt[i].x + offset, (CGFloat)apt[i].y + offset);
        }
        CGContextStrokePath(ctx);
        CGColorRelease(strokeCol);
    }
    dc->curPos.x = apt[cpt - 1].x;
    dc->curPos.y = apt[cpt - 1].y;
    return TRUE;
}

int SetStretchBltMode(HDC hdc, int mode) {
    (void)hdc;
    return mode;
}

int CombineRgn(HRGN hrgnDst, HRGN hrgnSrc1, HRGN hrgnSrc2, int iMode) {
    (void)hrgnDst; (void)hrgnSrc1; (void)hrgnSrc2; (void)iMode;
    return 1; /* SIMPLEREGION */
}

int FrameRect(HDC hDC, const RECT *lprc, HBRUSH hbr) {
    if (!hDC || !lprc) return 0;
    struct ZP_HDC__ *dc = (struct ZP_HDC__*)hDC;
    CGContextRef ctx = dc->ctx;
    if (!ctx) return 0;

    struct ZP_HBRUSH__ *br = (struct ZP_HBRUSH__*)hbr;
    COLORREF col = br ? br->color : RGB(0, 0, 0);
    CGColorRef cgCol = create_cgcolor_from_colorref(col, 1.0f);
    CGContextSetStrokeColorWithColor(ctx, cgCol);
    CGContextSetLineWidth(ctx, 1.0f);
    CGRect r = CGRectMake((CGFloat)lprc->left + 0.5f, (CGFloat)lprc->top + 0.5f,
                          (CGFloat)(lprc->right - lprc->left - 1), (CGFloat)(lprc->bottom - lprc->top - 1));
    CGContextStrokeRect(ctx, r);
    CGColorRelease(cgCol);
    return 1;
}

int ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom) {
    (void)hdc; (void)left; (void)top; (void)right; (void)bottom;
    return 1; /* SIMPLEREGION */
}

#endif /* __APPLE__ */
