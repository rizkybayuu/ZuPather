#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <execinfo.h>
#include <signal.h>
#include <dirent.h>
#include <fnmatch.h>
#include "win32_macos_shim.h"

/* =========================================================================
 * 1. Transparent Path Normalization (Converts '\' to '/')
 * ========================================================================= */
static void normalize_path_to_posix(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < dst_size) {
        dst[i] = (src[i] == '\\') ? '/' : src[i];
        i++;
    }
    dst[i] = '\0';
}

#undef fopen
FILE* zp_macos_fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    char norm[1024];
    normalize_path_to_posix(path, norm, sizeof(norm));
    return fopen(norm, mode);
}

DWORD zp_macos_get_file_attributes_a(LPCSTR path) {
    if (!path) return INVALID_FILE_ATTRIBUTES;
    char norm[1024];
    normalize_path_to_posix(path, norm, sizeof(norm));

    struct stat st;
    if (stat(norm, &st) != 0) {
        return INVALID_FILE_ATTRIBUTES;
    }
    if (S_ISDIR(st.st_mode)) {
        return FILE_ATTRIBUTE_DIRECTORY;
    }
    return 0;
}

BOOL zp_macos_create_directory_a(LPCSTR path, void *sa) {
    (void)sa;
    if (!path) return FALSE;
    char norm[1024];
    normalize_path_to_posix(path, norm, sizeof(norm));
    return (mkdir(norm, 0755) == 0 || errno == EEXIST) ? TRUE : FALSE;
}

BOOL zp_macos_delete_file_a(LPCSTR path) {
    if (!path) return FALSE;
    char norm[1024];
    normalize_path_to_posix(path, norm, sizeof(norm));
    return (unlink(norm) == 0) ? TRUE : FALSE;
}

BOOL zp_macos_copy_file_a(LPCSTR src, LPCSTR dst, BOOL fail_if_exists) {
    if (!src || !dst) return FALSE;
    char norm_src[1024], norm_dst[1024];
    normalize_path_to_posix(src, norm_src, sizeof(norm_src));
    normalize_path_to_posix(dst, norm_dst, sizeof(norm_dst));

    if (fail_if_exists) {
        struct stat st;
        if (stat(norm_dst, &st) == 0) return FALSE;
    }

    FILE *f_in = fopen(norm_src, "rb");
    if (!f_in) return FALSE;
    FILE *f_out = fopen(norm_dst, "wb");
    if (!f_out) { fclose(f_in); return FALSE; }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f_in)) > 0) {
        if (fwrite(buf, 1, n, f_out) != n) {
            fclose(f_in);
            fclose(f_out);
            return FALSE;
        }
    }

    fclose(f_in);
    fclose(f_out);
    return TRUE;
}

BOOL zp_macos_move_file_ex_a(LPCSTR src, LPCSTR dst, DWORD flags) {
    (void)flags;
    if (!src || !dst) return FALSE;
    char norm_src[1024], norm_dst[1024];
    normalize_path_to_posix(src, norm_src, sizeof(norm_src));
    normalize_path_to_posix(dst, norm_dst, sizeof(norm_dst));
    return (rename(norm_src, norm_dst) == 0) ? TRUE : FALSE;
}

DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    (void)hModule;
    if (!lpFilename || nSize == 0) return 0;
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *path = [bundle executablePath];
    if (!path) path = [[NSProcessInfo processInfo] arguments].firstObject;
    if (!path) path = @"ZuPather";

    strncpy(lpFilename, [path UTF8String], (size_t)nSize - 1);
    lpFilename[nSize - 1] = '\0';
    return (DWORD)strlen(lpFilename);
}

/* =========================================================================
 * 2. Embedded Resource Lookup Engine
 * ========================================================================= */
typedef struct {
    int         id;
    const char *name;
    const void *data;
    size_t      size;
} ZP_ResourceEntry;

__attribute__((weak)) const ZP_ResourceEntry g_macos_embedded_resources[] = {};
__attribute__((weak)) const size_t           g_macos_embedded_resource_count = 0;

HRSRC FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType) {
    (void)hModule; (void)lpType;
    int id = (int)(intptr_t)lpName;

    /* Check in embedded resources table */
    if (g_macos_embedded_resources && g_macos_embedded_resource_count > 0) {
        for (size_t i = 0; i < g_macos_embedded_resource_count; ++i) {
            if (g_macos_embedded_resources[i].id == id) {
                return (HRSRC)&g_macos_embedded_resources[i];
            }
        }
    }

    /* Fallback to bundle resources directory */
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *resPath = [bundle resourcePath];
    if (resPath) {
        const char *filename = NULL;
        switch (id) {
            case 101: filename = "ZuSPLASHscreen.jpg"; break;
            case 102: filename = "github.svg"; break;
            case 103: filename = "AtkinsonHyperlegible-Regular.ttf"; break;
            case 104: filename = "AtkinsonHyperlegible-Bold.ttf"; break;
            case 105: filename = "JetBrainsMono-Regular.ttf"; break;
            case 106: filename = "JetBrainsMono-Bold.ttf"; break;
            case 107: filename = "SMALLFROGonPAD.png"; break;
            case 108: filename = "COIN.png"; break;
            case 109: filename = "SKULL.png"; break;
            case 110: filename = "baBallBlue.png"; break;
            case 111: filename = "baBallYellow.png"; break;
            case 112: filename = "baBallRed.png"; break;
            case 113: filename = "baBallGreen.png"; break;
            case 114: filename = "baBallPurple.png"; break;
            case 115: filename = "baBallWhite.png"; break;
            case 116: filename = "baBallOrange.png"; break;
            case 117: filename = "baBallNavy.png"; break;
            case 118: filename = "baBallCyan.png"; break;
            case 119: filename = "baBallBlack.png"; break;
            case 120: filename = "baBallCream.png"; break;
            default: break;
        }

        if (filename) {
            NSString *nsFile = [NSString stringWithUTF8String:filename];
            NSArray<NSString*> *candidates = @[
                [resPath stringByAppendingPathComponent:nsFile],
                [[resPath stringByAppendingPathComponent:@"assets/media"] stringByAppendingPathComponent:nsFile],
                [[resPath stringByAppendingPathComponent:@"assets/fonts"] stringByAppendingPathComponent:nsFile],
                [@"assets/media" stringByAppendingPathComponent:nsFile],
                [@"assets/fonts" stringByAppendingPathComponent:nsFile]
            ];
            NSData *fileData = nil;
            for (NSString *candPath in candidates) {
                fileData = [NSData dataWithContentsOfFile:candPath];
                if (fileData && [fileData length] > 0) break;
            }
            if (fileData) {
                ZP_ResourceEntry *entry = (ZP_ResourceEntry*)malloc(sizeof(ZP_ResourceEntry));
                entry->id = id;
                entry->name = filename;
                entry->data = [fileData bytes];
                entry->size = (size_t)[fileData length];
                CFRetain((__bridge CFTypeRef)fileData);
                return (HRSRC)entry;
            }
        }
    }

    return NULL;
}

HGLOBAL LoadResource(HMODULE hModule, HRSRC hResInfo) {
    (void)hModule;
    return (HGLOBAL)hResInfo;
}

LPVOID LockResource(HGLOBAL hResData) {
    if (!hResData) return NULL;
    ZP_ResourceEntry *entry = (ZP_ResourceEntry*)hResData;
    return (LPVOID)entry->data;
}

DWORD SizeofResource(HMODULE hModule, HRSRC hResInfo) {
    (void)hModule;
    if (!hResInfo) return 0;
    ZP_ResourceEntry *entry = (ZP_ResourceEntry*)hResInfo;
    return (DWORD)entry->size;
}

/* =========================================================================
 * 3. Apple ImageIO Decoder & Encoder (PNG, JPG, BMP, GIF)
 * ========================================================================= */
BOOL zp_macos_decode_image_memory(const void *buf, size_t len, int *out_w, int *out_h, uint32_t **out_bgra) {
    if (!buf || len == 0 || !out_w || !out_h || !out_bgra) return FALSE;

    NSData *data = [NSData dataWithBytesNoCopy:(void*)buf length:(NSUInteger)len freeWhenDone:NO];
    CGImageSourceRef src = CGImageSourceCreateWithData((__bridge CFDataRef)data, NULL);
    if (!src) return FALSE;

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if (!img) return FALSE;

    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    if (w == 0 || h == 0) {
        CGImageRelease(img);
        return FALSE;
    }

    uint32_t *pixels = (uint32_t*)malloc(w * h * sizeof(uint32_t));
    if (!pixels) {
        CGImageRelease(img);
        return FALSE;
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        pixels, w, h, 8, w * 4, colorSpace,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst
    );
    CGColorSpaceRelease(colorSpace);

    if (!ctx) {
        free(pixels);
        CGImageRelease(img);
        return FALSE;
    }

    /* Top-down upright drawing into BGRA buffer */
    CGContextTranslateCTM(ctx, 0, (CGFloat)h);
    CGContextScaleCTM(ctx, 1.0f, -1.0f);
    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    *out_w = (int)w;
    *out_h = (int)h;
    *out_bgra = pixels;
    return TRUE;
}

BOOL zp_macos_decode_image_file(const char *path, int *out_w, int *out_h, uint32_t **out_bgra) {
    if (!path || !out_w || !out_h || !out_bgra) return FALSE;
    char norm[1024];
    normalize_path_to_posix(path, norm, sizeof(norm));

    NSString *nsPath = [NSString stringWithUTF8String:norm];
    NSData *data = [NSData dataWithContentsOfFile:nsPath];
    if (!data || [data length] == 0) return FALSE;

    return zp_macos_decode_image_memory([data bytes], (size_t)[data length], out_w, out_h, out_bgra);
}

BOOL zp_macos_encode_image_file(const char *path, int w, int h, const uint32_t *bgra, int format_type) {
    if (!path || w <= 0 || h <= 0 || !bgra) return FALSE;
    char norm[1024];
    normalize_path_to_posix(path, norm, sizeof(norm));

    /* Create CGImage from BGRA pixel buffer */
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, bgra, (size_t)w * h * 4, NULL);
    if (!provider) {
        CGColorSpaceRelease(colorSpace);
        return FALSE;
    }

    CGImageRef img = CGImageCreate(
        (size_t)w, (size_t)h, 8, 32, (size_t)w * 4,
        colorSpace,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst,
        provider, NULL, false, kCGRenderingIntentDefault
    );
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    if (!img) return FALSE;

    CFStringRef uti = CFSTR("public.png");
    if (format_type == 1) { /* JPG */
        uti = CFSTR("public.jpeg");
    } else if (format_type == 2) { /* BMP */
        uti = CFSTR("com.microsoft.bmp");
    } else if (format_type == 3) { /* GIF */
        uti = CFSTR("com.compuserve.gif");
    }

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:norm]];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, uti, 1, NULL);
    if (!dest) {
        CGImageRelease(img);
        return FALSE;
    }

    CGImageDestinationAddImage(dest, img, NULL);
    BOOL ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
    CGImageRelease(img);

    return ok;
}

BOOL zp_macos_pick_folder(HWND hwndParent, char *out_path, size_t max_len) {
    (void)hwndParent;
    if (!out_path || max_len == 0) return FALSE;

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:@"Select Directory"];

    if ([panel runModal] == NSModalResponseOK) {
        NSURL *url = [panel URL];
        if (url && url.path) {
            strncpy(out_path, [url.path UTF8String], max_len - 1);
            out_path[max_len - 1] = '\0';
            return TRUE;
        }
    }
    return FALSE;
}

/* =========================================================================
 * 4. POSIX Crash Signal Handler (Diagnostic error.txt on macOS)
 * ========================================================================= */
static void macos_crash_handler(int sig) {
    FILE *f = fopen("error.txt", "w");
    if (f) {
        time_t t = time(NULL);
        struct tm tm_val;
        localtime_r(&t, &tm_val);
        fprintf(f, "=======================================================\n");
        fprintf(f, " ZuPather macOS Crash Diagnostic Log\n");
        fprintf(f, " Session Crashed : %04d-%02d-%02d %02d:%02d:%02d\n",
                tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
        fprintf(f, " Signal          : %d (%s)\n", sig, strsignal(sig));
#if defined(__arm64__) || defined(__aarch64__)
        fprintf(f, " Architecture    : Apple Silicon (ARM64)\n");
#else
        fprintf(f, " Architecture    : Intel x86_64\n");
#endif
        fprintf(f, "=======================================================\n\n");
        fprintf(f, "--- Stack Backtrace ---\n");

        void *callstack[128];
        int frames = backtrace(callstack, 128);
        char **strs = backtrace_symbols(callstack, frames);
        if (strs) {
            for (int i = 0; i < frames; ++i) {
                fprintf(f, "  [%02d] %s\n", i, strs[i]);
            }
            free(strs);
        }
        fclose(f);
    }
    _exit(1);
}

void zp_macos_install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = macos_crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

/* =========================================================================
 * 5. Directory Search & Shell File Operations
 * ========================================================================= */
typedef struct ZP_FIND_HANDLE {
    DIR *dir;
    char dir_path[1024];
    char pattern[256];
} ZP_FIND_HANDLE;

static BOOL fill_find_data(const char *dir_path, const char *entry_name, LPWIN32_FIND_DATAA lpFindFileData) {
    if (!dir_path || !entry_name || !lpFindFileData) return FALSE;
    memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATAA));
    strncpy(lpFindFileData->cFileName, entry_name, sizeof(lpFindFileData->cFileName) - 1);

    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry_name);
    struct stat st;
    if (stat(full_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }
        lpFindFileData->nFileSizeLow = (DWORD)(st.st_size & 0xFFFFFFFF);
        lpFindFileData->nFileSizeHigh = (DWORD)((st.st_size >> 32) & 0xFFFFFFFF);
    }
    return TRUE;
}

HANDLE FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
    if (!lpFileName || !lpFindFileData) return INVALID_HANDLE_VALUE;

    char norm[1024];
    normalize_path_to_posix(lpFileName, norm, sizeof(norm));

    char dir_path[1024];
    char pattern[256];
    const char *last_slash = strrchr(norm, '/');
    if (last_slash) {
        size_t dlen = (size_t)(last_slash - norm);
        if (dlen >= sizeof(dir_path)) dlen = sizeof(dir_path) - 1;
        strncpy(dir_path, norm, dlen);
        dir_path[dlen] = '\0';
        strncpy(pattern, last_slash + 1, sizeof(pattern) - 1);
    } else {
        strcpy(dir_path, ".");
        strncpy(pattern, norm, sizeof(pattern) - 1);
    }
    pattern[sizeof(pattern) - 1] = '\0';

    DIR *d = opendir(dir_path[0] ? dir_path : ".");
    if (!d) return INVALID_HANDLE_VALUE;

    ZP_FIND_HANDLE *h = (ZP_FIND_HANDLE*)calloc(1, sizeof(ZP_FIND_HANDLE));
    if (!h) { closedir(d); return INVALID_HANDLE_VALUE; }
    h->dir = d;
    strncpy(h->dir_path, dir_path, sizeof(h->dir_path) - 1);
    strncpy(h->pattern, pattern, sizeof(h->pattern) - 1);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0 || fnmatch(pattern, ent->d_name, FNM_CASEFOLD) == 0) {
            fill_find_data(h->dir_path, ent->d_name, lpFindFileData);
            return (HANDLE)h;
        }
    }

    closedir(d);
    free(h);
    return INVALID_HANDLE_VALUE;
}

BOOL FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
    if (!hFindFile || hFindFile == INVALID_HANDLE_VALUE || !lpFindFileData) return FALSE;
    ZP_FIND_HANDLE *h = (ZP_FIND_HANDLE*)hFindFile;

    struct dirent *ent;
    while ((ent = readdir(h->dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (strcmp(h->pattern, "*.*") == 0 || strcmp(h->pattern, "*") == 0 || fnmatch(h->pattern, ent->d_name, FNM_CASEFOLD) == 0) {
            return fill_find_data(h->dir_path, ent->d_name, lpFindFileData);
        }
    }
    return FALSE;
}

BOOL FindClose(HANDLE hFindFile) {
    if (!hFindFile || hFindFile == INVALID_HANDLE_VALUE) return FALSE;
    ZP_FIND_HANDLE *h = (ZP_FIND_HANDLE*)hFindFile;
    if (h->dir) closedir(h->dir);
    free(h);
    return TRUE;
}

int SHFileOperationA(LPSHFILEOPSTRUCTA lpFileOp) {
    if (!lpFileOp || !lpFileOp->pFrom) return -1;
    if (lpFileOp->wFunc == FO_DELETE) {
        const char *p = lpFileOp->pFrom;
        while (*p) {
            char norm[1024];
            normalize_path_to_posix(p, norm, sizeof(norm));
            struct stat st;
            if (stat(norm, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    NSString *nsPath = [NSString stringWithUTF8String:norm];
                    [[NSFileManager defaultManager] removeItemAtPath:nsPath error:nil];
                } else {
                    unlink(norm);
                }
            }
            p += strlen(p) + 1;
        }
        return 0;
    }
    return 0;
}

PIDLIST_ABSOLUTE SHBrowseForFolderA(LPBROWSEINFOA lpbi) {
    (void)lpbi;
    return NULL;
}

BOOL SHGetPathFromIDListA(PIDLIST_ABSOLUTE pidl, LPSTR pszPath) {
    (void)pidl; (void)pszPath;
    return FALSE;
}

void CoTaskMemFree(LPVOID pv) {
    if (pv) free(pv);
}

LPVOID GlobalLock(HGLOBAL hMem) {
    return (LPVOID)hMem;
}

BOOL GlobalUnlock(HGLOBAL hMem) {
    (void)hMem;
    return TRUE;
}

BOOL MessageBeep(UINT uType) {
    (void)uType;
    NSBeep();
    return TRUE;
}

#endif /* __APPLE__ */
