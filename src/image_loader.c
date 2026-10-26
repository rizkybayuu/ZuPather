#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/demux.h>
#include <webp/mux.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "image_loader.h"

extern void log_app_error(const char *category, const char *fmt, ...);

struct VideoPlaybackCtx {
    IMFSourceReader *reader;
    int              disp_width;
    int              disp_height;
    int              surface_width;
    int              surface_height;
    int              stride;
    int              fps;
    int              frame_interval_ms;
    DWORD            last_tick;
    uint8_t         *frame_buf;
};

struct WebPPlaybackCtx {
    uint8_t            *file_data;
    size_t              file_size;
    WebPAnimDecoder    *decoder;
    WebPAnimInfo        info;
    int                 canvas_w;
    int                 canvas_h;
    int                 cur_frame_idx;
    int                 cur_frame_delay_ms;
    int                 prev_timestamp_ms;
    uint8_t            *premul_buf;
};

static void scale_bgra_buffer(const uint8_t *src, int src_w, int src_h, uint8_t *dst, int dst_w, int dst_h, bool is_premultiplied);

static const GUID k_GUID_NULL = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };

void image_subsystem_init(void) {
    CoInitialize(NULL);
    MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
}

void image_subsystem_shutdown(void) {
    MFShutdown();
    CoUninitialize();
}

void image_get_basename_no_ext(const char *filepath, char *out_basename, size_t max_len) {
    if (!filepath || !out_basename || max_len == 0) return;
    out_basename[0] = '\0';

    const char *last_slash = strrchr(filepath, '\\');
    const char *slash2 = strrchr(filepath, '/');
    if (slash2 && (!last_slash || slash2 > last_slash)) {
        last_slash = slash2;
    }

    const char *name_start = last_slash ? (last_slash + 1) : filepath;
    strncpy(out_basename, name_start, max_len - 1);
    out_basename[max_len - 1] = '\0';

    char *last_dot = strrchr(out_basename, '.');
    if (last_dot && last_dot != out_basename) {
        *last_dot = '\0';
    }
}

void image_get_extension(const char *filepath, char *out_ext, size_t max_len) {
    if (!filepath || !out_ext || max_len == 0) return;
    out_ext[0] = '\0';
    const char *dot = strrchr(filepath, '.');
    if (dot && *(dot + 1) != '\0') {
        strncpy(out_ext, dot + 1, max_len - 1);
        out_ext[max_len - 1] = '\0';
        for (char *p = out_ext; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
        }
    }
}

bool image_is_zuma_native_ext(const char *ext_or_filepath) {
    if (!ext_or_filepath || ext_or_filepath[0] == '\0') return false;
    char ext[16] = {0};
    if (strchr(ext_or_filepath, '.') != NULL) {
        image_get_extension(ext_or_filepath, ext, sizeof(ext));
    } else {
        strncpy(ext, ext_or_filepath, sizeof(ext) - 1);
        for (char *p = ext; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
        }
    }
    return (_stricmp(ext, "png") == 0 ||
            _stricmp(ext, "webp") == 0 ||
            _stricmp(ext, "jpg") == 0 ||
            _stricmp(ext, "jpeg") == 0 ||
            _stricmp(ext, "gif") == 0);
}

bool image_is_video_ext(const char *ext_or_filepath) {
    if (!ext_or_filepath || ext_or_filepath[0] == '\0') return false;
    char ext[16] = {0};
    if (strchr(ext_or_filepath, '.') != NULL) {
        image_get_extension(ext_or_filepath, ext, sizeof(ext));
    } else {
        strncpy(ext, ext_or_filepath, sizeof(ext) - 1);
        for (char *p = ext; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
        }
    }
    return (_stricmp(ext, "mp4") == 0 ||
            _stricmp(ext, "mkv") == 0 ||
            _stricmp(ext, "avi") == 0 ||
            _stricmp(ext, "mov") == 0 ||
            _stricmp(ext, "wmv") == 0 ||
            _stricmp(ext, "webm") == 0 ||
            _stricmp(ext, "flv") == 0 ||
            _stricmp(ext, "m4v") == 0 ||
            _stricmp(ext, "mpg") == 0 ||
            _stricmp(ext, "mpeg") == 0 ||
            _stricmp(ext, "3gp") == 0 ||
            _stricmp(ext, "ts") == 0 ||
            _stricmp(ext, "m2ts") == 0 ||
            _stricmp(ext, "asf") == 0 ||
            _stricmp(ext, "vob") == 0 ||
            _stricmp(ext, "ogv") == 0 ||
            _stricmp(ext, "divx") == 0);
}


static bool is_webp_file(const char *filepath, const uint8_t *header, size_t header_len) {
    if (header_len >= 12) {
        if (memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WEBP", 4) == 0) {
            return true;
        }
    }
    const char *dot = strrchr(filepath, '.');
    if (dot && _stricmp(dot, ".webp") == 0) {
        return true;
    }
    return false;
}

static HBITMAP create_premultiplied_dib(int width, int height, const uint8_t *bgra, void **out_bits, HDC *out_dc) {
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) return NULL;

    if (bgra) {
        uint8_t *dst = (uint8_t*)bits;
        for (int i = 0; i < width * height; ++i) {
            uint8_t a = bgra[i * 4 + 3];
            dst[i * 4 + 0] = (uint8_t)(((int)bgra[i * 4 + 0] * a) / 255);
            dst[i * 4 + 1] = (uint8_t)(((int)bgra[i * 4 + 1] * a) / 255);
            dst[i * 4 + 2] = (uint8_t)(((int)bgra[i * 4 + 2] * a) / 255);
            dst[i * 4 + 3] = a;
        }
    }

    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    SelectObject(mem_dc, hbm);
    ReleaseDC(NULL, screen_dc);

    if (out_bits) *out_bits = bits;
    if (out_dc) *out_dc = mem_dc;
    return hbm;
}

static bool load_webp_internal(const char *filepath, CanvasImage *out_img) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 12) {
        fclose(f);
        return false;
    }

    uint8_t *file_data = (uint8_t*)malloc(fsize);
    if (!file_data) {
        fclose(f);
        return false;
    }

    if (fread(file_data, 1, fsize, f) != (size_t)fsize) {
        free(file_data);
        fclose(f);
        return false;
    }
    fclose(f);

    /* 1. Try multi-frame decoding via WebPAnimDecoder */
    WebPData webp_data;
    webp_data.bytes = file_data;
    webp_data.size = (size_t)fsize;

    WebPAnimDecoderOptions dec_opt;
    WebPAnimDecoderOptionsInit(&dec_opt);
    dec_opt.color_mode = MODE_BGRA;
    dec_opt.use_threads = 1;

    WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_data, &dec_opt);
    if (dec) {
        WebPAnimInfo info;
        if (WebPAnimDecoderGetInfo(dec, &info) && info.frame_count > 1) {
            int w = (int)info.canvas_width;
            int h = (int)info.canvas_height;

            uint8_t *first_buf = NULL;
            int first_timestamp = 0;
            if (!WebPAnimDecoderGetNext(dec, &first_buf, &first_timestamp)) {
                WebPAnimDecoderDelete(dec);
                free(file_data);
                return false;
            }

            int first_dur = first_timestamp;
            if (first_dur <= 0) first_dur = 16;

            void *bits = NULL;
            HDC hdc = NULL;
            HBITMAP hbm = create_premultiplied_dib(w, h, first_buf, &bits, &hdc);
            if (!hbm || !bits) {
                WebPAnimDecoderDelete(dec);
                free(file_data);
                return false;
            }

            WebPPlaybackCtx *wctx = (WebPPlaybackCtx*)calloc(1, sizeof(WebPPlaybackCtx));
            if (!wctx) {
                DeleteDC(hdc);
                DeleteObject(hbm);
                WebPAnimDecoderDelete(dec);
                free(file_data);
                return false;
            }

            wctx->file_data = file_data;
            wctx->file_size = (size_t)fsize;
            wctx->decoder = dec;
            wctx->info = info;
            wctx->canvas_w = w;
            wctx->canvas_h = h;
            wctx->cur_frame_idx = 0;
            wctx->cur_frame_delay_ms = first_dur;
            wctx->prev_timestamp_ms = first_timestamp;

            out_img->webp_ctx = wctx;
            out_img->media_type = MEDIA_TYPE_ANIMATED_WEBP;
            out_img->is_animated = true;
            out_img->is_video = false;
            out_img->frame_count = (int)info.frame_count;
            out_img->cur_frame_idx = 0;
            out_img->last_frame_tick = GetTickCount();

            out_img->width = w;
            out_img->height = h;
            out_img->hbm = hbm;
            out_img->hdc_mem = hdc;
            out_img->bits = bits;

            /* Save still frame copy of frame 0 */
            out_img->first_frame_hbm = create_premultiplied_dib(w, h, first_buf, &out_img->first_frame_bits, &out_img->first_frame_hdc);

            if (bits && w > 0 && h > 0) {
                out_img->orig_bits = malloc((size_t)w * h * 4);
                if (out_img->orig_bits) {
                    memcpy(out_img->orig_bits, bits, (size_t)w * h * 4);
                    out_img->orig_width = w;
                    out_img->orig_height = h;
                }
            }

            strncpy(out_img->filepath, filepath, sizeof(out_img->filepath) - 1);
            const char *last_slash = strrchr(filepath, '\\');
            const char *slash2 = strrchr(filepath, '/');
            if (slash2 && (!last_slash || slash2 > last_slash)) last_slash = slash2;
            const char *name_start = last_slash ? (last_slash + 1) : filepath;
            strncpy(out_img->filename, name_start, sizeof(out_img->filename) - 1);
            image_get_basename_no_ext(filepath, out_img->display_name, sizeof(out_img->display_name));
            return true;
        }
        WebPAnimDecoderDelete(dec);
    }

    /* 2. Single-frame fallback */
    int width = 0, height = 0;
    if (!WebPGetInfo(file_data, fsize, &width, &height) || width <= 0 || height <= 0) {
        free(file_data);
        return false;
    }

    uint8_t *bgra = WebPDecodeBGRA(file_data, fsize, &width, &height);
    free(file_data);
    if (!bgra) return false;

    void *bits = NULL;
    HDC mem_dc = NULL;
    HBITMAP hbm = create_premultiplied_dib(width, height, bgra, &bits, &mem_dc);
    WebPFree(bgra);
    if (!hbm || !bits) return false;

    out_img->hbm = hbm;
    out_img->hdc_mem = mem_dc;
    out_img->bits = bits;
    out_img->width = width;
    out_img->height = height;
    out_img->media_type = MEDIA_TYPE_STATIC;
    out_img->is_animated = false;
    out_img->is_video = false;
    out_img->frame_count = 1;
    strncpy(out_img->filepath, filepath, sizeof(out_img->filepath) - 1);

    const char *last_slash = strrchr(filepath, '\\');
    const char *slash2 = strrchr(filepath, '/');
    if (slash2 && (!last_slash || slash2 > last_slash)) last_slash = slash2;
    const char *name_start = last_slash ? (last_slash + 1) : filepath;
    strncpy(out_img->filename, name_start, sizeof(out_img->filename) - 1);
    image_get_basename_no_ext(filepath, out_img->display_name, sizeof(out_img->display_name));

    return true;
}

static bool load_wic_internal(const char *filepath, CanvasImage *out_img) {
    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr) || !factory) return false;

    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);

    IWICBitmapDecoder *decoder = NULL;
    hr = factory->lpVtbl->CreateDecoderFromFilename(factory, wpath, NULL, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        factory->lpVtbl->Release(factory);
        return false;
    }

    UINT frameCount = 0;
    decoder->lpVtbl->GetFrameCount(decoder, &frameCount);
    if (frameCount == 0) {
        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);
        return false;
    }

    /* Check if multi-frame animated GIF */
    char ext[16] = {0};
    image_get_extension(filepath, ext, sizeof(ext));
    bool is_gif = (_stricmp(ext, "gif") == 0);

    if (is_gif && frameCount > 1) {
        out_img->media_type = MEDIA_TYPE_ANIMATED_GIF;
        out_img->is_animated = true;
        out_img->is_video = false;
        out_img->frame_count = (int)frameCount;
        out_img->cur_frame_idx = 0;
        out_img->last_frame_tick = GetTickCount();
        out_img->frames = (ImageAnimationFrame*)calloc(frameCount, sizeof(ImageAnimationFrame));

        UINT first_w = 0, first_h = 0;
        for (UINT i = 0; i < frameCount; ++i) {
            IWICBitmapFrameDecode *pFrame = NULL;
            if (SUCCEEDED(decoder->lpVtbl->GetFrame(decoder, i, &pFrame)) && pFrame) {
                UINT fw = 0, fh = 0;
                pFrame->lpVtbl->GetSize(pFrame, &fw, &fh);
                if (i == 0) { first_w = fw; first_h = fh; }

                IWICFormatConverter *converter = NULL;
                factory->lpVtbl->CreateFormatConverter(factory, &converter);
                if (converter) {
                    converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)pFrame,
                                                  &GUID_WICPixelFormat32bppPBGRA,
                                                  WICBitmapDitherTypeNone, NULL, 0.0,
                                                  WICBitmapPaletteTypeCustom);
                    void *bits = NULL;
                    HDC hdc = NULL;
                    HBITMAP hbm = create_premultiplied_dib((int)fw, (int)fh, NULL, &bits, &hdc);
                    if (hbm && bits) {
                        converter->lpVtbl->CopyPixels(converter, NULL, fw * 4, fw * fh * 4, (BYTE*)bits);
                        out_img->frames[i].hbm = hbm;
                        out_img->frames[i].hdc_mem = hdc;
                        out_img->frames[i].bits = bits;
                        out_img->frames[i].duration_ms = 100; /* standard default GIF delay */
                    }
                    converter->lpVtbl->Release(converter);
                }
                pFrame->lpVtbl->Release(pFrame);
            }
        }

        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);

        out_img->width = (int)first_w;
        out_img->height = (int)first_h;
        out_img->hbm = out_img->frames[0].hbm;
        out_img->hdc_mem = out_img->frames[0].hdc_mem;
        out_img->bits = out_img->frames[0].bits;

        out_img->first_frame_hbm = create_premultiplied_dib((int)first_w, (int)first_h, NULL, &out_img->first_frame_bits, &out_img->first_frame_hdc);
        if (out_img->first_frame_bits && out_img->frames[0].bits) {
            memcpy(out_img->first_frame_bits, out_img->frames[0].bits, (size_t)first_w * first_h * 4);
        }

        strncpy(out_img->filepath, filepath, sizeof(out_img->filepath) - 1);
        const char *last_slash = strrchr(filepath, '\\');
        const char *slash2 = strrchr(filepath, '/');
        if (slash2 && (!last_slash || slash2 > last_slash)) last_slash = slash2;
        const char *name_start = last_slash ? (last_slash + 1) : filepath;
        strncpy(out_img->filename, name_start, sizeof(out_img->filename) - 1);
        image_get_basename_no_ext(filepath, out_img->display_name, sizeof(out_img->display_name));
        return true;
    }

    /* Single frame decoding */
    IWICBitmapFrameDecode *frame = NULL;
    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);
        return false;
    }

    UINT width = 0, height = 0;
    frame->lpVtbl->GetSize(frame, &width, &height);
    if (width == 0 || height == 0) {
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);
        return false;
    }

    IWICFormatConverter *converter = NULL;
    hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (FAILED(hr) || !converter) {
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);
        return false;
    }

    hr = converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)frame,
                                       &GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, NULL, 0.0,
                                       WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->lpVtbl->Release(converter);
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);
        return false;
    }

    void *bits = NULL;
    HDC mem_dc = NULL;
    HBITMAP hbm = create_premultiplied_dib((int)width, (int)height, NULL, &bits, &mem_dc);
    if (!hbm || !bits) {
        converter->lpVtbl->Release(converter);
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        factory->lpVtbl->Release(factory);
        return false;
    }

    UINT stride = width * 4;
    UINT buffer_size = width * height * 4;
    hr = converter->lpVtbl->CopyPixels(converter, NULL, stride, buffer_size, (BYTE*)bits);

    converter->lpVtbl->Release(converter);
    frame->lpVtbl->Release(frame);
    decoder->lpVtbl->Release(decoder);
    factory->lpVtbl->Release(factory);

    if (FAILED(hr)) {
        DeleteDC(mem_dc);
        DeleteObject(hbm);
        return false;
    }

    out_img->hbm = hbm;
    out_img->hdc_mem = mem_dc;
    out_img->bits = bits;
    out_img->width = (int)width;
    out_img->height = (int)height;
    out_img->media_type = MEDIA_TYPE_STATIC;
    out_img->is_animated = false;
    out_img->is_video = false;
    out_img->frame_count = 1;
    strncpy(out_img->filepath, filepath, sizeof(out_img->filepath) - 1);

    const char *last_slash = strrchr(filepath, '\\');
    const char *slash2 = strrchr(filepath, '/');
    if (slash2 && (!last_slash || slash2 > last_slash)) last_slash = slash2;
    const char *name_start = last_slash ? (last_slash + 1) : filepath;
    strncpy(out_img->filename, name_start, sizeof(out_img->filename) - 1);
    image_get_basename_no_ext(filepath, out_img->display_name, sizeof(out_img->display_name));

    return true;
}

static void copy_rows_to_dib(const BYTE *row0, LONG pitch, uint32_t *dst, int w, int h) {
    if (!row0 || !dst || w <= 0 || h <= 0) return;
    for (int y = 0; y < h; ++y) {
        const uint32_t *src = (const uint32_t*)(row0 + (ptrdiff_t)y * pitch);
        uint32_t *dst_row = dst + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            dst_row[x] = src[x] | 0xFF000000;
        }
    }
}

static void extract_sample_to_dib(IMFSample *sample, void *dst_bits, int w, int h, int default_stride, const char *log_tag) {
    if (!sample || !dst_bits || w <= 0 || h <= 0) return;

    IMFMediaBuffer *buf = NULL;
    HRESULT hr = sample->lpVtbl->ConvertToContiguousBuffer(sample, &buf);
    if (FAILED(hr) || !buf) return;

    uint32_t *dst = (uint32_t*)dst_bits;

    /* 1. Primary: Query IMF2DBuffer to get the exact hardware row pitch */
    IMF2DBuffer *p2D = NULL;
    hr = buf->lpVtbl->QueryInterface(buf, &IID_IMF2DBuffer, (void**)&p2D);
    if (SUCCEEDED(hr) && p2D) {
        BYTE *p0 = NULL;
        LONG pitch = 0;
        if (SUCCEEDED(p2D->lpVtbl->Lock2D(p2D, &p0, &pitch))) {
            if (log_tag) {
                printf("[VIDEO_LOG %s] Lock2D: pitch=%ld, w=%d, h=%d\n", log_tag, pitch, w, h);
            }
            copy_rows_to_dib(p0, pitch, dst, w, h);
            p2D->lpVtbl->Unlock2D(p2D);
        }
        p2D->lpVtbl->Release(p2D);
    } else {
        /* 2. Fallback: 1D IMFMediaBuffer Lock */
        BYTE *pData = NULL;
        DWORD cbLen = 0;
        if (SUCCEEDED(buf->lpVtbl->Lock(buf, &pData, NULL, &cbLen))) {
            int s = abs(default_stride);
            if (s < w * 4) s = w * 4;
            if (log_tag) {
                printf("[VIDEO_LOG %s] 1D Lock fallback: cbLen=%lu, stride=%d, default_stride=%d\n",
                       log_tag, (unsigned long)cbLen, s, default_stride);
            }
            if ((size_t)s * h <= cbLen) {
                if (default_stride < 0) {
                    /* Bottom-up 1D buffer: first visual scanline is at bottom in memory */
                    copy_rows_to_dib(pData + (size_t)(h - 1) * s, -s, dst, w, h);
                } else {
                    /* Top-down 1D buffer */
                    copy_rows_to_dib(pData, s, dst, w, h);
                }
            }
            buf->lpVtbl->Unlock(buf);
        }
    }
    buf->lpVtbl->Release(buf);
}

static bool load_video_internal(const char *filepath, CanvasImage *out_img) {
    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);

    IMFAttributes *attr = NULL;
    MFCreateAttributes(&attr, 1);
    if (attr) {
        attr->lpVtbl->SetUINT32(attr, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    IMFSourceReader *reader = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath, attr, &reader);
    if (attr) attr->lpVtbl->Release(attr);
    if (FAILED(hr) || !reader) return false;

    /* Request RGB32 (32bpp BGRA uncompressed) */
    IMFMediaType *pType = NULL;
    MFCreateMediaType(&pType);
    pType->lpVtbl->SetGUID(pType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    hr = reader->lpVtbl->SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
    pType->lpVtbl->Release(pType);
    if (FAILED(hr)) {
        reader->lpVtbl->Release(reader);
        return false;
    }

    /* Disable all audio streams (NO AUDIO IS PLAYED OR USED) */
    reader->lpVtbl->SetStreamSelection(reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->lpVtbl->SetStreamSelection(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    /* Read first frame to initialize pipeline and trigger format negotiation */
    DWORD streamIdx = 0, flags = 0;
    LONGLONG timestamp = 0;
    IMFSample *sample = NULL;
    hr = reader->lpVtbl->ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIdx, &flags, &timestamp, &sample);
    if (FAILED(hr) || !sample) {
        reader->lpVtbl->Release(reader);
        return false;
    }

    /* Query media type AFTER first sample to get negotiated surface & aperture dimensions */
    IMFMediaType *curType = NULL;
    reader->lpVtbl->GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &curType);

    UINT32 disp_w = 0, disp_h = 0;
    UINT32 surf_w = 0, surf_h = 0;
    int fps = 30;
    int stride = 0;

    if (curType) {
        /* 1. Aperture (visible display dimensions) */
        MFVideoArea area;
        if (SUCCEEDED(curType->lpVtbl->GetBlob(curType, &MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), NULL)) && area.Area.cx > 0 && area.Area.cy > 0) {
            disp_w = area.Area.cx;
            disp_h = area.Area.cy;
        } else if (SUCCEEDED(curType->lpVtbl->GetBlob(curType, &MF_MT_GEOMETRIC_APERTURE, (UINT8*)&area, sizeof(area), NULL)) && area.Area.cx > 0 && area.Area.cy > 0) {
            disp_w = area.Area.cx;
            disp_h = area.Area.cy;
        }

        /* 2. Surface frame size */
        UINT64 size_val = 0;
        if (SUCCEEDED(curType->lpVtbl->GetUINT64(curType, &MF_MT_FRAME_SIZE, &size_val))) {
            surf_w = (UINT32)(size_val >> 32);
            surf_h = (UINT32)(size_val & 0xFFFFFFFF);
        }
        if (disp_w == 0 || disp_h == 0) {
            disp_w = surf_w;
            disp_h = surf_h;
        }

        /* 3. Frame rate: Query native stream first for exact fps (e.g. 60 FPS, 59.94 FPS, 30 FPS) */
        IMFMediaType *nativeType = NULL;
        if (SUCCEEDED(reader->lpVtbl->GetNativeMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType)) && nativeType) {
            UINT64 r_val = 0;
            if (SUCCEEDED(nativeType->lpVtbl->GetUINT64(nativeType, &MF_MT_FRAME_RATE, &r_val))) {
                UINT32 num = (UINT32)(r_val >> 32);
                UINT32 den = (UINT32)(r_val & 0xFFFFFFFF);
                if (den > 0) fps = (int)((num + (den / 2)) / den);
            }
            nativeType->lpVtbl->Release(nativeType);
        } else {
            UINT64 rate_val = 0;
            if (SUCCEEDED(curType->lpVtbl->GetUINT64(curType, &MF_MT_FRAME_RATE, &rate_val))) {
                UINT32 num = (UINT32)(rate_val >> 32);
                UINT32 den = (UINT32)(rate_val & 0xFFFFFFFF);
                if (den > 0) fps = (int)((num + (den / 2)) / den);
            }
        }

        /* 4. Default stride */
        UINT32 u_stride = 0;
        if (SUCCEEDED(curType->lpVtbl->GetUINT32(curType, &MF_MT_DEFAULT_STRIDE, &u_stride)) && u_stride > 0) {
            stride = (int)u_stride;
        } else {
            LONG calc_stride = 0;
            if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, surf_w > 0 ? surf_w : disp_w, &calc_stride))) {
                stride = abs(calc_stride);
            } else {
                stride = (surf_w > 0 ? surf_w : disp_w) * 4;
            }
        }
        curType->lpVtbl->Release(curType);
    }

    if (disp_w == 0 || disp_h == 0) {
        sample->lpVtbl->Release(sample);
        reader->lpVtbl->Release(reader);
        return false;
    }
    if (fps <= 0) fps = 30;
    if (fps > 60) fps = 60;

    /* Fallback hardware buffer alignment verification against sample length */
    IMFMediaBuffer *test_buf = NULL;
    if (SUCCEEDED(sample->lpVtbl->ConvertToContiguousBuffer(sample, &test_buf)) && test_buf) {
        BYTE *pD = NULL;
        DWORD cLen = 0;
        if (SUCCEEDED(test_buf->lpVtbl->Lock(test_buf, &pD, NULL, &cLen))) {
            if (surf_h > 0 && cLen >= (DWORD)stride * surf_h) {
                /* Verified exact stride */
            } else {
                for (int a = 64; a >= 16; a /= 2) {
                    int aw = (disp_w + a - 1) & ~(a - 1);
                    int ah = (disp_h + a - 1) & ~(a - 1);
                    if (cLen == (DWORD)aw * 4 * ah || cLen == (DWORD)aw * 4 * disp_h) {
                        stride = aw * 4;
                        break;
                    }
                }
            }
            test_buf->lpVtbl->Unlock(test_buf);
        }
        test_buf->lpVtbl->Release(test_buf);
    }

    void *bits = NULL;
    HDC mem_dc = NULL;
    HBITMAP hbm = create_premultiplied_dib((int)disp_w, (int)disp_h, NULL, &bits, &mem_dc);
    if (!hbm || !bits) {
        sample->lpVtbl->Release(sample);
        reader->lpVtbl->Release(reader);
        return false;
    }

    extract_sample_to_dib(sample, bits, (int)disp_w, (int)disp_h, stride, "INIT");
    sample->lpVtbl->Release(sample);

    /* Allocate VideoPlaybackCtx */
    VideoPlaybackCtx *vctx = (VideoPlaybackCtx*)calloc(1, sizeof(VideoPlaybackCtx));
    vctx->reader = reader;
    vctx->disp_width = (int)disp_w;
    vctx->disp_height = (int)disp_h;
    vctx->surface_width = (int)surf_w;
    vctx->surface_height = (int)surf_h;
    vctx->stride = stride;
    vctx->fps = fps;
    vctx->frame_interval_ms = 1000 / fps;
    vctx->last_tick = GetTickCount();

    out_img->video_ctx = vctx;
    out_img->media_type = MEDIA_TYPE_VIDEO;
    out_img->is_animated = true;
    out_img->is_video = true;
    out_img->frame_count = 1;
    out_img->width = (int)disp_w;
    out_img->height = (int)disp_h;
    out_img->hbm = hbm;
    out_img->hdc_mem = mem_dc;
    out_img->bits = bits;
    out_img->last_frame_tick = GetTickCount();

    /* Save still frame copy of frame 0 */
    out_img->first_frame_hbm = create_premultiplied_dib((int)disp_w, (int)disp_h, NULL, &out_img->first_frame_bits, &out_img->first_frame_hdc);
    if (out_img->first_frame_bits && bits) {
        memcpy(out_img->first_frame_bits, bits, (size_t)disp_w * disp_h * 4);
    }

    strncpy(out_img->filepath, filepath, sizeof(out_img->filepath) - 1);
    const char *last_slash = strrchr(filepath, '\\');
    const char *slash2 = strrchr(filepath, '/');
    if (slash2 && (!last_slash || slash2 > last_slash)) last_slash = slash2;
    const char *name_start = last_slash ? (last_slash + 1) : filepath;
    strncpy(out_img->filename, name_start, sizeof(out_img->filename) - 1);
    image_get_basename_no_ext(filepath, out_img->display_name, sizeof(out_img->display_name));

    return true;
}

bool image_load_from_file(const char *filepath, CanvasImage *out_img) {
    if (!filepath || !out_img) return false;
    memset(out_img, 0, sizeof(CanvasImage));

    bool ok = false;
    /* 1. Check if video file */
    if (image_is_video_ext(filepath)) {
        if (load_video_internal(filepath, out_img)) {
            ok = true;
        }
    }

    if (!ok) {
        /* 2. Peek first 16 bytes for WebP magic */
        uint8_t header[16] = {0};
        FILE *f = fopen(filepath, "rb");
        if (f) {
            fread(header, 1, sizeof(header), f);
            fclose(f);
        }

        if (is_webp_file(filepath, header, sizeof(header))) {
            if (load_webp_internal(filepath, out_img)) {
                ok = true;
            }
        }

        /* 3. Fallback to WIC (PNG, JPG, BMP, TIFF, GIF, etc.) */
        if (!ok && load_wic_internal(filepath, out_img)) {
            ok = true;
        }

        /* 4. If WIC didn't succeed and haven't tried WebP yet */
        if (!ok && !is_webp_file(filepath, header, sizeof(header))) {
            if (load_webp_internal(filepath, out_img)) {
                ok = true;
            }
        }

        /* 5. Final fallback: try Media Foundation in case video has unusual extension */
        if (!ok && load_video_internal(filepath, out_img)) {
            ok = true;
        }
    }

    if (!ok) {
        log_app_error("IMAGE_LOAD", "Failed to load image from file: '%s'", filepath);
        return false;
    }

    if (ok) {
        if (out_img->bits && out_img->width > 0 && out_img->height > 0 && !out_img->orig_bits) {
            out_img->orig_bits = malloc((size_t)out_img->width * out_img->height * 4);
            if (out_img->orig_bits) {
                memcpy(out_img->orig_bits, out_img->bits, (size_t)out_img->width * out_img->height * 4);
                out_img->orig_width = out_img->width;
                out_img->orig_height = out_img->height;
            }
        }
        return true;
    }

    return false;
}

bool image_load_from_memory(const void *data, size_t size, CanvasImage *out_img) {
    if (!data || size == 0 || !out_img) return false;
    memset(out_img, 0, sizeof(CanvasImage));

    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr) || !factory) return false;

    IWICStream *stream = NULL;
    hr = factory->lpVtbl->CreateStream(factory, &stream);
    if (FAILED(hr) || !stream) {
        factory->lpVtbl->Release(factory);
        return false;
    }

    hr = stream->lpVtbl->InitializeFromMemory(stream, (BYTE*)data, (DWORD)size);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    IWICBitmapDecoder *decoder = NULL;
    hr = factory->lpVtbl->CreateDecoderFromStream(factory, (IStream*)stream, NULL,
                                                  WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    IWICBitmapFrameDecode *frame = NULL;
    hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->lpVtbl->Release(decoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    UINT width = 0, height = 0;
    frame->lpVtbl->GetSize(frame, &width, &height);
    if (width == 0 || height == 0) {
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    IWICFormatConverter *converter = NULL;
    hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    if (FAILED(hr) || !converter) {
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)frame,
                                  &GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapDitherTypeNone, NULL, 0.0,
                                  WICBitmapPaletteTypeCustom);

    void *bits = NULL;
    HDC hdc_mem = NULL;
    HBITMAP hbm = create_premultiplied_dib((int)width, (int)height, NULL, &bits, &hdc_mem);
    if (!hbm || !bits) {
        converter->lpVtbl->Release(converter);
        frame->lpVtbl->Release(frame);
        decoder->lpVtbl->Release(decoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    converter->lpVtbl->CopyPixels(converter, NULL, width * 4, width * height * 4, (BYTE*)bits);

    converter->lpVtbl->Release(converter);
    frame->lpVtbl->Release(frame);
    decoder->lpVtbl->Release(decoder);
    stream->lpVtbl->Release(stream);
    factory->lpVtbl->Release(factory);

    out_img->width = (int)width;
    out_img->height = (int)height;
    out_img->hbm = hbm;
    out_img->hdc_mem = hdc_mem;
    out_img->bits = bits;
    out_img->media_type = MEDIA_TYPE_STATIC;
    out_img->is_animated = false;
    out_img->is_video = false;
    out_img->frame_count = 1;
    strncpy(out_img->filename, "embedded_resource", sizeof(out_img->filename) - 1);
    strncpy(out_img->display_name, "embedded_resource", sizeof(out_img->display_name) - 1);

    if (bits && width > 0 && height > 0 && !out_img->orig_bits) {
        out_img->orig_bits = malloc((size_t)width * height * 4);
        if (out_img->orig_bits) {
            memcpy(out_img->orig_bits, bits, (size_t)width * height * 4);
            out_img->orig_width = (int)width;
            out_img->orig_height = (int)height;
        }
    }

    return true;
}

bool image_load_from_resource(int res_id, CanvasImage *out_img) {
    if (!out_img) return false;
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(res_id), (LPCSTR)RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hMem = LoadResource(NULL, hRes);
    if (!hMem) return false;
    DWORD sz = SizeofResource(NULL, hRes);
    const void *ptr = LockResource(hMem);
    if (!ptr || sz == 0) return false;
    return image_load_from_memory(ptr, (size_t)sz, out_img);
}

bool image_advance_animation(CanvasImage *img) {
    if (!img || !img->is_animated) return false;
    DWORD now = GetTickCount();

    /* 1. Animated WebP streaming playback via WebPAnimDecoder */
    if (img->webp_ctx && img->webp_ctx->decoder) {
        WebPPlaybackCtx *wctx = img->webp_ctx;
        int dur = wctx->cur_frame_delay_ms > 0 ? wctx->cur_frame_delay_ms : 16;
        DWORD elapsed = now - img->last_frame_tick;

        if (elapsed >= (DWORD)dur) {
            uint8_t *buf = NULL;
            int timestamp = 0;

            if (WebPAnimDecoderHasMoreFrames(wctx->decoder)) {
                if (WebPAnimDecoderGetNext(wctx->decoder, &buf, &timestamp)) {
                    int delta = timestamp - wctx->prev_timestamp_ms;
                    if (delta <= 0) delta = 16;
                    wctx->cur_frame_delay_ms = delta;
                    wctx->prev_timestamp_ms = timestamp;
                    wctx->cur_frame_idx++;
                }
            } else {
                /* Loop back to beginning! */
                WebPAnimDecoderReset(wctx->decoder);
                if (WebPAnimDecoderGetNext(wctx->decoder, &buf, &timestamp)) {
                    wctx->cur_frame_delay_ms = timestamp > 0 ? timestamp : 16;
                    wctx->prev_timestamp_ms = timestamp;
                    wctx->cur_frame_idx = 0;
                }
            }

            if (buf && img->bits) {
                if (img->width == wctx->canvas_w && img->height == wctx->canvas_h) {
                    uint8_t *dst = (uint8_t*)img->bits;
                    int total_px = wctx->canvas_w * wctx->canvas_h;
                    for (int p = 0; p < total_px; ++p) {
                        uint8_t a = buf[p * 4 + 3];
                        dst[p * 4 + 0] = (uint8_t)(((int)buf[p * 4 + 0] * a) / 255);
                        dst[p * 4 + 1] = (uint8_t)(((int)buf[p * 4 + 1] * a) / 255);
                        dst[p * 4 + 2] = (uint8_t)(((int)buf[p * 4 + 2] * a) / 255);
                        dst[p * 4 + 3] = a;
                    }
                } else if (img->width > 0 && img->height > 0) {
                    /* Scaled animated WebP overlay - decode to full frame then resample */
                    if (!wctx->premul_buf) {
                        wctx->premul_buf = (uint8_t*)malloc((size_t)wctx->canvas_w * wctx->canvas_h * 4);
                    }
                    if (wctx->premul_buf) {
                        int total_px = wctx->canvas_w * wctx->canvas_h;
                        for (int p = 0; p < total_px; ++p) {
                            uint8_t a = buf[p * 4 + 3];
                            wctx->premul_buf[p * 4 + 0] = (uint8_t)(((int)buf[p * 4 + 0] * a) / 255);
                            wctx->premul_buf[p * 4 + 1] = (uint8_t)(((int)buf[p * 4 + 1] * a) / 255);
                            wctx->premul_buf[p * 4 + 2] = (uint8_t)(((int)buf[p * 4 + 2] * a) / 255);
                            wctx->premul_buf[p * 4 + 3] = a;
                        }
                        scale_bgra_buffer(wctx->premul_buf, wctx->canvas_w, wctx->canvas_h,
                                          (uint8_t*)img->bits, img->width, img->height, true);
                    }
                }
            }

            img->cur_frame_idx = wctx->cur_frame_idx;
            img->last_frame_tick = now;
            return true;
        }
        return false;
    }

    /* 2. Multi-frame buffer: GIF */
    if (img->frames && img->frame_count > 1) {
        int dur = img->frames[img->cur_frame_idx].duration_ms;
        if (dur <= 0) dur = 100;
        if ((now - img->last_frame_tick) >= (DWORD)dur) {
            img->cur_frame_idx = (img->cur_frame_idx + 1) % img->frame_count;
            img->hbm = img->frames[img->cur_frame_idx].hbm;
            img->hdc_mem = img->frames[img->cur_frame_idx].hdc_mem;
            img->bits = img->frames[img->cur_frame_idx].bits;
            img->last_frame_tick = now;
            return true;
        }
        return false;
    }

    /* 3. Video stream via IMFSourceReader */
    if (img->is_video && img->video_ctx && img->video_ctx->reader) {
        VideoPlaybackCtx *vc = img->video_ctx;
        int interval = vc->frame_interval_ms > 0 ? vc->frame_interval_ms : 16;
        if ((now - img->last_frame_tick) >= (DWORD)interval) {
            DWORD streamIdx = 0, flags = 0;
            LONGLONG timestamp = 0;
            IMFSample *sample = NULL;
            HRESULT hr = vc->reader->lpVtbl->ReadSample(
                vc->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                &streamIdx, &flags, &timestamp, &sample
            );

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                IMFMediaType *newType = NULL;
                if (SUCCEEDED(vc->reader->lpVtbl->GetCurrentMediaType(vc->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &newType))) {
                    UINT32 u_stride = 0;
                    if (SUCCEEDED(newType->lpVtbl->GetUINT32(newType, &MF_MT_DEFAULT_STRIDE, &u_stride)) && u_stride > 0) {
                        vc->stride = (int)u_stride;
                    }
                    newType->lpVtbl->Release(newType);
                }
            }

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                /* Loop back to beginning! */
                PROPVARIANT var;
                PropVariantInit(&var);
                var.vt = VT_I8;
                var.hVal.QuadPart = 0;
                vc->reader->lpVtbl->SetCurrentPosition(vc->reader, &k_GUID_NULL, &var);
                PropVariantClear(&var);
                if (sample) { sample->lpVtbl->Release(sample); sample = NULL; }

                hr = vc->reader->lpVtbl->ReadSample(
                    vc->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                    &streamIdx, &flags, &timestamp, &sample
                );
            }

            if (sample) {
                if (img->bits) {
                    if (img->width == vc->disp_width && img->height == vc->disp_height) {
                        extract_sample_to_dib(sample, img->bits, vc->disp_width, vc->disp_height, vc->stride, NULL);
                    } else if (img->width > 0 && img->height > 0) {
                        if (!vc->frame_buf) {
                            vc->frame_buf = (uint8_t*)malloc((size_t)vc->disp_width * vc->disp_height * 4);
                        }
                        if (vc->frame_buf) {
                            extract_sample_to_dib(sample, vc->frame_buf, vc->disp_width, vc->disp_height, vc->stride, NULL);
                            scale_bgra_buffer(vc->frame_buf, vc->disp_width, vc->disp_height,
                                              (uint8_t*)img->bits, img->width, img->height, false);
                        }
                    }
                }
                sample->lpVtbl->Release(sample);
            }
            img->last_frame_tick = now;
            return true;
        }
        return false;
    }

    return false;
}

void image_free(CanvasImage *img) {
    if (!img) return;

    if (img->video_ctx) {
        if (img->video_ctx->reader) {
            img->video_ctx->reader->lpVtbl->Release(img->video_ctx->reader);
            img->video_ctx->reader = NULL;
        }
        if (img->video_ctx->frame_buf) {
            free(img->video_ctx->frame_buf);
            img->video_ctx->frame_buf = NULL;
        }
        free(img->video_ctx);
        img->video_ctx = NULL;
    }

    if (img->webp_ctx) {
        if (img->webp_ctx->decoder) {
            WebPAnimDecoderDelete(img->webp_ctx->decoder);
            img->webp_ctx->decoder = NULL;
        }
        if (img->webp_ctx->file_data) {
            free(img->webp_ctx->file_data);
            img->webp_ctx->file_data = NULL;
        }
        if (img->webp_ctx->premul_buf) {
            free(img->webp_ctx->premul_buf);
            img->webp_ctx->premul_buf = NULL;
        }
        free(img->webp_ctx);
        img->webp_ctx = NULL;
    }

    if (img->frames) {
        for (int i = 0; i < img->frame_count; ++i) {
            if (img->frames[i].hdc_mem) DeleteDC(img->frames[i].hdc_mem);
            if (img->frames[i].hbm) DeleteObject(img->frames[i].hbm);
        }
        free(img->frames);
        img->frames = NULL;
    } else {
        if (img->hdc_mem) {
            DeleteDC(img->hdc_mem);
            img->hdc_mem = NULL;
        }
        if (img->hbm) {
            DeleteObject(img->hbm);
            img->hbm = NULL;
        }
    }

    if (img->first_frame_hdc) {
        DeleteDC(img->first_frame_hdc);
        img->first_frame_hdc = NULL;
    }
    if (img->first_frame_hbm) {
        DeleteObject(img->first_frame_hbm);
        img->first_frame_hbm = NULL;
    }

    img->bits = NULL;
    img->first_frame_bits = NULL;
    if (img->orig_bits) {
        free(img->orig_bits);
        img->orig_bits = NULL;
    }
    img->orig_width = 0;
    img->orig_height = 0;
    img->width = 0;
    img->height = 0;
    img->media_type = MEDIA_TYPE_STATIC;
    img->is_animated = false;
    img->is_video = false;
    img->frame_count = 0;
    img->cur_frame_idx = 0;
    memset(img->filepath, 0, sizeof(img->filepath));
    memset(img->filename, 0, sizeof(img->filename));
    memset(img->display_name, 0, sizeof(img->display_name));
}

bool get_video_stream_info(const char *filepath, double *out_duration_sec, int *out_fps, int *out_total_frames) {
    if (!filepath || filepath[0] == '\0') return false;
    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);

    IMFSourceReader *reader = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath, NULL, &reader);
    if (FAILED(hr) || !reader) return false;

    double dur_sec = 0.0;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader->lpVtbl->GetPresentationAttribute(reader, (DWORD)MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &var))) {
        if (var.vt == VT_UI8) {
            dur_sec = (double)var.uhVal.QuadPart / 10000000.0;
        }
        PropVariantClear(&var);
    }

    int fps = 30;
    IMFMediaType *pType = NULL;
    if (SUCCEEDED(reader->lpVtbl->GetNativeMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pType)) && pType) {
        UINT64 r_val = 0;
        if (SUCCEEDED(pType->lpVtbl->GetUINT64(pType, &MF_MT_FRAME_RATE, &r_val))) {
            UINT32 num = (UINT32)(r_val >> 32);
            UINT32 den = (UINT32)(r_val & 0xFFFFFFFF);
            if (den > 0) {
                fps = (int)((num + (den / 2)) / den);
            }
        }
        pType->lpVtbl->Release(pType);
    }
    reader->lpVtbl->Release(reader);

    if (fps <= 0) fps = 30;
    if (fps > 60) fps = 60;

    int total_frames = (int)(dur_sec * (double)fps);
    if (out_duration_sec) *out_duration_sec = dur_sec;
    if (out_fps) *out_fps = fps;
    if (out_total_frames) *out_total_frames = total_frames;
    return true;
}

/* High-quality 32-bit BGRA image scaling using Windows Imaging Component (WIC) */
static bool scale_bgra_wic(const uint8_t *src_bgra, int src_w, int src_h, uint8_t *dst_bgra, int dst_w, int dst_h, bool is_premultiplied) {
    if (!src_bgra || !dst_bgra || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;

    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr) || !factory) return false;

    const GUID *fmt = is_premultiplied ? &GUID_WICPixelFormat32bppPBGRA : &GUID_WICPixelFormat32bppBGRA;

    IWICBitmap *wic_bitmap = NULL;
    hr = factory->lpVtbl->CreateBitmapFromMemory(
        factory, (UINT)src_w, (UINT)src_h,
        fmt,
        (UINT)(src_w * 4),
        (UINT)(src_w * src_h * 4),
        (BYTE*)src_bgra,
        &wic_bitmap
    );
    if (FAILED(hr) || !wic_bitmap) {
        factory->lpVtbl->Release(factory);
        return false;
    }

    IWICBitmapScaler *scaler = NULL;
    hr = factory->lpVtbl->CreateBitmapScaler(factory, &scaler);
    if (FAILED(hr) || !scaler) {
        wic_bitmap->lpVtbl->Release(wic_bitmap);
        factory->lpVtbl->Release(factory);
        return false;
    }

    /* Fant interpolation provides optimal anti-aliased area-averaging for downscaling and crisp upscaling */
    hr = scaler->lpVtbl->Initialize(scaler, (IWICBitmapSource*)wic_bitmap, (UINT)dst_w, (UINT)dst_h, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        hr = scaler->lpVtbl->Initialize(scaler, (IWICBitmapSource*)wic_bitmap, (UINT)dst_w, (UINT)dst_h, WICBitmapInterpolationModeCubic);
    }

    if (SUCCEEDED(hr)) {
        hr = scaler->lpVtbl->CopyPixels(scaler, NULL, (UINT)(dst_w * 4), (UINT)(dst_w * dst_h * 4), (BYTE*)dst_bgra);
    }

    scaler->lpVtbl->Release(scaler);
    wic_bitmap->lpVtbl->Release(wic_bitmap);
    factory->lpVtbl->Release(factory);

    return SUCCEEDED(hr);
}

/* Fast pure C bilinear interpolation rescaler for 32-bit BGRA with precalculated X table */
static void scale_bgra_software_bilinear(const uint8_t *src, int src_w, int src_h, uint8_t *dst, int dst_w, int dst_h) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    float scale_x = (float)src_w / (float)dst_w;
    float scale_y = (float)src_h / (float)dst_h;

    typedef struct {
        int x0, x1;
        float fx, inv_fx;
    } XWeight;

    XWeight *x_weights = (XWeight*)malloc(sizeof(XWeight) * dst_w);
    if (!x_weights) return;

    for (int x = 0; x < dst_w; ++x) {
        float src_x = (x + 0.5f) * scale_x - 0.5f;
        int x0 = (int)floorf(src_x);
        int x1 = x0 + 1;
        float fx = src_x - (float)x0;
        if (x0 < 0) { x0 = 0; fx = 0.0f; }
        if (x1 >= src_w) { x1 = src_w - 1; }
        if (x0 >= src_w) { x0 = src_w - 1; }
        x_weights[x].x0 = x0 * 4;
        x_weights[x].x1 = x1 * 4;
        x_weights[x].fx = fx;
        x_weights[x].inv_fx = 1.0f - fx;
    }

    for (int y = 0; y < dst_h; ++y) {
        float src_y = (y + 0.5f) * scale_y - 0.5f;
        int y0 = (int)floorf(src_y);
        int y1 = y0 + 1;
        float fy = src_y - (float)y0;
        if (y0 < 0) { y0 = 0; fy = 0.0f; }
        if (y1 >= src_h) { y1 = src_h - 1; }
        if (y0 >= src_h) { y0 = src_h - 1; }

        float inv_fy = 1.0f - fy;
        uint8_t *dst_row = dst + (size_t)y * dst_w * 4;
        const uint8_t *src_row0 = src + (size_t)y0 * src_w * 4;
        const uint8_t *src_row1 = src + (size_t)y1 * src_w * 4;

        for (int x = 0; x < dst_w; ++x) {
            float w00 = x_weights[x].inv_fx * inv_fy;
            float w10 = x_weights[x].fx * inv_fy;
            float w01 = x_weights[x].inv_fx * fy;
            float w11 = x_weights[x].fx * fy;

            const uint8_t *p00 = src_row0 + x_weights[x].x0;
            const uint8_t *p10 = src_row0 + x_weights[x].x1;
            const uint8_t *p01 = src_row1 + x_weights[x].x0;
            const uint8_t *p11 = src_row1 + x_weights[x].x1;

            float b = (float)p00[0] * w00 + (float)p10[0] * w10 + (float)p01[0] * w01 + (float)p11[0] * w11 + 0.5f;
            float g = (float)p00[1] * w00 + (float)p10[1] * w10 + (float)p01[1] * w01 + (float)p11[1] * w11 + 0.5f;
            float r = (float)p00[2] * w00 + (float)p10[2] * w10 + (float)p01[2] * w01 + (float)p11[2] * w11 + 0.5f;
            float a = (float)p00[3] * w00 + (float)p10[3] * w10 + (float)p01[3] * w01 + (float)p11[3] * w11 + 0.5f;

            dst_row[x * 4 + 0] = (uint8_t)(b < 0.0f ? 0 : (b > 255.0f ? 255 : (int)b));
            dst_row[x * 4 + 1] = (uint8_t)(g < 0.0f ? 0 : (g > 255.0f ? 255 : (int)g));
            dst_row[x * 4 + 2] = (uint8_t)(r < 0.0f ? 0 : (r > 255.0f ? 255 : (int)r));
            dst_row[x * 4 + 3] = (uint8_t)(a < 0.0f ? 0 : (a > 255.0f ? 255 : (int)a));
        }
    }

    free(x_weights);
}

/* Master scaling function: attempts WIC scaler first, falls back to software bilinear */
static void scale_bgra_buffer(const uint8_t *src, int src_w, int src_h, uint8_t *dst, int dst_w, int dst_h, bool is_premultiplied) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    if (src_w == dst_w && src_h == dst_h) {
        memcpy(dst, src, (size_t)dst_w * dst_h * 4);
        return;
    }
    if (!scale_bgra_wic(src, src_w, src_h, dst, dst_w, dst_h, is_premultiplied)) {
        scale_bgra_software_bilinear(src, src_w, src_h, dst, dst_w, dst_h);
    }
}

bool image_resize_in_place(CanvasImage *img, int new_w, int new_h) {
    if (!img) return false;
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;
    if (new_w > 4096) new_w = 4096;
    if (new_h > 4096) new_h = 4096;

    if (img->width == new_w && img->height == new_h && img->hbm) {
        return true;
    }

    /* Resize multi-frame GIF frames if present */
    if (img->frames && img->frame_count > 1) {
        for (int i = 0; i < img->frame_count; ++i) {
            void *f_bits = NULL;
            HDC f_dc = NULL;
            HBITMAP f_hbm = create_premultiplied_dib(new_w, new_h, NULL, &f_bits, &f_dc);
            if (f_hbm && f_bits) {
                scale_bgra_buffer((const uint8_t*)img->frames[i].bits, img->width, img->height,
                                  (uint8_t*)f_bits, new_w, new_h, true);
                if (img->frames[i].hdc_mem) DeleteDC(img->frames[i].hdc_mem);
                if (img->frames[i].hbm) DeleteObject(img->frames[i].hbm);
                img->frames[i].hbm = f_hbm;
                img->frames[i].hdc_mem = f_dc;
                img->frames[i].bits = f_bits;
            }
        }
        img->hbm = img->frames[img->cur_frame_idx].hbm;
        img->hdc_mem = img->frames[img->cur_frame_idx].hdc_mem;
        img->bits = img->frames[img->cur_frame_idx].bits;
        img->width = new_w;
        img->height = new_h;
        return true;
    }

    /* Ensure we have pristine orig_bits */
    if (!img->orig_bits && img->bits && img->width > 0 && img->height > 0) {
        img->orig_bits = malloc((size_t)img->width * img->height * 4);
        if (img->orig_bits) {
            memcpy(img->orig_bits, img->bits, (size_t)img->width * img->height * 4);
            img->orig_width = img->width;
            img->orig_height = img->height;
        }
    }

    if (!img->orig_bits || img->orig_width <= 0 || img->orig_height <= 0) {
        return false;
    }

    void *new_bits = NULL;
    HDC new_dc = NULL;
    HBITMAP new_hbm = create_premultiplied_dib(new_w, new_h, NULL, &new_bits, &new_dc);
    if (!new_hbm || !new_bits) {
        if (new_dc) DeleteDC(new_dc);
        if (new_hbm) DeleteObject(new_hbm);
        return false;
    }

    scale_bgra_buffer(
        (const uint8_t*)img->orig_bits, img->orig_width, img->orig_height,
        (uint8_t*)new_bits, new_w, new_h, true
    );

    /* Delete old GDI bitmap and DC if not pointing to an animation frame */
    if (!img->frames) {
        if (img->hdc_mem) DeleteDC(img->hdc_mem);
        if (img->hbm) DeleteObject(img->hbm);
    }
    img->hbm = new_hbm;
    img->hdc_mem = new_dc;
    img->bits = new_bits;
    img->width = new_w;
    img->height = new_h;

    /* If first_frame_hbm exists, invalidate it so export uses resized bits directly */
    if (img->first_frame_hdc) {
        DeleteDC(img->first_frame_hdc);
        img->first_frame_hdc = NULL;
    }
    if (img->first_frame_hbm) {
        DeleteObject(img->first_frame_hbm);
        img->first_frame_hbm = NULL;
    }
    img->first_frame_bits = NULL;

    return true;
}

extern CanvasFitMode   g_canvas_fit_mode;
extern CanvasAlignment g_canvas_alignment;

void get_canvas_image_dest_rect(
    int canvas_w, int canvas_h,
    int img_src_w, int img_src_h,
    CanvasFitMode fit_mode,
    CanvasAlignment alignment,
    int *out_x, int *out_y,
    int *out_w, int *out_h
) {
    if (img_src_w <= 0 || img_src_h <= 0 || canvas_w <= 0 || canvas_h <= 0) {
        if (out_x) *out_x = 0;
        if (out_y) *out_y = 0;
        if (out_w) *out_w = (canvas_w > 0) ? canvas_w : 640;
        if (out_h) *out_h = (canvas_h > 0) ? canvas_h : 480;
        return;
    }

    int dest_w = img_src_w;
    int dest_h = img_src_h;

    switch (fit_mode) {
        case CANVAS_FIT_ORIGINAL:
            dest_w = img_src_w;
            dest_h = img_src_h;
            break;

        case CANVAS_FIT_FIT: { /* Contain / Letterbox */
            float sw = (float)canvas_w / (float)img_src_w;
            float sh = (float)canvas_h / (float)img_src_h;
            float s = (sw < sh) ? sw : sh;
            dest_w = (int)roundf((float)img_src_w * s);
            dest_h = (int)roundf((float)img_src_h * s);
            if (dest_w < 1) dest_w = 1;
            if (dest_h < 1) dest_h = 1;
            break;
        }

        case CANVAS_FIT_FILL: { /* Cover / Crop */
            float sw = (float)canvas_w / (float)img_src_w;
            float sh = (float)canvas_h / (float)img_src_h;
            float s = (sw > sh) ? sw : sh;
            dest_w = (int)roundf((float)img_src_w * s);
            dest_h = (int)roundf((float)img_src_h * s);
            if (dest_w < 1) dest_w = 1;
            if (dest_h < 1) dest_h = 1;
            break;
        }

        case CANVAS_FIT_STRETCH:
        default:
            dest_w = canvas_w;
            dest_h = canvas_h;
            break;
    }

    int dest_x = 0;
    int dest_y = 0;

    /* Horizontal alignment */
    switch (alignment) {
        case CANVAS_ALIGN_TOP_LEFT:
        case CANVAS_ALIGN_MIDDLE_LEFT:
        case CANVAS_ALIGN_BOTTOM_LEFT:
            dest_x = 0;
            break;

        case CANVAS_ALIGN_TOP_CENTER:
        case CANVAS_ALIGN_MIDDLE_CENTER:
        case CANVAS_ALIGN_BOTTOM_CENTER:
            dest_x = (canvas_w - dest_w) / 2;
            break;

        case CANVAS_ALIGN_TOP_RIGHT:
        case CANVAS_ALIGN_MIDDLE_RIGHT:
        case CANVAS_ALIGN_BOTTOM_RIGHT:
            dest_x = canvas_w - dest_w;
            break;
    }

    /* Vertical alignment */
    switch (alignment) {
        case CANVAS_ALIGN_TOP_LEFT:
        case CANVAS_ALIGN_TOP_CENTER:
        case CANVAS_ALIGN_TOP_RIGHT:
            dest_y = 0;
            break;

        case CANVAS_ALIGN_MIDDLE_LEFT:
        case CANVAS_ALIGN_MIDDLE_CENTER:
        case CANVAS_ALIGN_MIDDLE_RIGHT:
            dest_y = (canvas_h - dest_h) / 2;
            break;

        case CANVAS_ALIGN_BOTTOM_LEFT:
        case CANVAS_ALIGN_BOTTOM_CENTER:
        case CANVAS_ALIGN_BOTTOM_RIGHT:
            dest_y = canvas_h - dest_h;
            break;
    }

    if (out_x) *out_x = dest_x;
    if (out_y) *out_y = dest_y;
    if (out_w) *out_w = dest_w;
    if (out_h) *out_h = dest_h;
}

static void blit_bgra_to_canvas(
    const uint8_t *src, int src_w, int src_h,
    uint8_t *dst, int dst_w, int dst_h,
    int place_x, int place_y
) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    int start_y = (place_y < 0) ? -place_y : 0;
    int end_y = src_h;
    if (place_y + end_y > dst_h) end_y = dst_h - place_y;

    int start_x = (place_x < 0) ? -place_x : 0;
    int end_x = src_w;
    if (place_x + end_x > dst_w) end_x = dst_w - place_x;

    if (start_x >= end_x || start_y >= end_y) return;
    int copy_bytes = (end_x - start_x) * 4;

    for (int y = start_y; y < end_y; ++y) {
        int dy = place_y + y;
        int dx = place_x + start_x;
        const uint8_t *src_row = src + ((size_t)y * src_w + start_x) * 4;
        uint8_t *dst_row = dst + ((size_t)dy * dst_w + dx) * 4;
        memcpy(dst_row, src_row, copy_bytes);
    }
}

static bool export_webp_file(
    const CanvasImage *img,
    const char *dest_filepath,
    bool resize,
    int target_w,
    int target_h,
    int max_frames_limit
) {
    if (!img || !dest_filepath || dest_filepath[0] == '\0') return false;

    int out_w = (resize && target_w > 0) ? target_w : img->width;
    int out_h = (resize && target_h > 0) ? target_h : img->height;
    if (out_w <= 0) out_w = 640;
    if (out_h <= 0) out_h = 480;

    /* 1. If video: encode video frames into animated WebP */
    if (img->is_video && img->filepath[0] != '\0') {
        WCHAR wpath[MAX_PATH] = {0};
        MultiByteToWideChar(CP_ACP, 0, img->filepath, -1, wpath, MAX_PATH);

        IMFAttributes *attr = NULL;
        MFCreateAttributes(&attr, 1);
        if (attr) {
            attr->lpVtbl->SetUINT32(attr, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }
        IMFSourceReader *v_reader = NULL;
        HRESULT hr = MFCreateSourceReaderFromURL(wpath, attr, &v_reader);
        if (attr) attr->lpVtbl->Release(attr);

        if (SUCCEEDED(hr) && v_reader) {
            IMFMediaType *pType = NULL;
            MFCreateMediaType(&pType);
            pType->lpVtbl->SetGUID(pType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
            v_reader->lpVtbl->SetCurrentMediaType(v_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
            pType->lpVtbl->Release(pType);

            v_reader->lpVtbl->SetStreamSelection(v_reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
            v_reader->lpVtbl->SetStreamSelection(v_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

            int stride = img->video_ctx ? img->video_ctx->stride : (img->width * 4);
            int fps = 30;
            if (img->video_ctx && img->video_ctx->fps > 0) {
                fps = img->video_ctx->fps;
            }
            /* Query native stream type for exact real frame rate (e.g. 60 FPS) */
            IMFMediaType *nativeType = NULL;
            if (SUCCEEDED(v_reader->lpVtbl->GetNativeMediaType(v_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType)) && nativeType) {
                UINT64 r_val = 0;
                if (SUCCEEDED(nativeType->lpVtbl->GetUINT64(nativeType, &MF_MT_FRAME_RATE, &r_val))) {
                    UINT32 num = (UINT32)(r_val >> 32);
                    UINT32 den = (UINT32)(r_val & 0xFFFFFFFF);
                    if (den > 0) fps = (int)((num + (den / 2)) / den);
                }
                nativeType->lpVtbl->Release(nativeType);
            }
            if (fps <= 0) fps = 30;
            if (fps > 60) fps = 60;
            int frame_delay = 1000 / fps;
            if (frame_delay < 16) frame_delay = 16;

            WebPAnimEncoderOptions enc_options;
            WebPAnimEncoderOptionsInit(&enc_options);
            WebPAnimEncoder *enc = WebPAnimEncoderNew(out_w, out_h, &enc_options);
            if (enc) {
                WebPConfig config;
                WebPConfigInit(&config);
                config.lossless = 0;
                config.quality = 85.0f;

                int place_x = 0, place_y = 0, place_w = out_w, place_h = out_h;
                if (resize) {
                    get_canvas_image_dest_rect(out_w, out_h, img->width, img->height, g_canvas_fit_mode, g_canvas_alignment, &place_x, &place_y, &place_w, &place_h);
                }
                uint8_t *raw_bits = (uint8_t*)malloc((size_t)img->width * img->height * 4);
                uint8_t *scaled_bits = (uint8_t*)malloc((size_t)place_w * place_h * 4);
                uint8_t *canvas_bits = (place_w != out_w || place_h != out_h || place_x != 0 || place_y != 0) ? (uint8_t*)malloc((size_t)out_w * out_h * 4) : NULL;
                int timestamp_ms = 0;
                int frame_idx = 0;
                int max_frames = (max_frames_limit > 0) ? max_frames_limit : 999999;

                while (frame_idx < max_frames) {
                    DWORD s_idx = 0, s_flags = 0;
                    LONGLONG s_ts = 0;
                    IMFSample *s_sample = NULL;
                    HRESULT hr_s = v_reader->lpVtbl->ReadSample(v_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &s_idx, &s_flags, &s_ts, &s_sample);
                    if (FAILED(hr_s) || (s_flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
                    if (s_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                        IMFMediaType *nT = NULL;
                        if (SUCCEEDED(v_reader->lpVtbl->GetCurrentMediaType(v_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &nT))) {
                            UINT32 u_s = 0;
                            if (SUCCEEDED(nT->lpVtbl->GetUINT32(nT, &MF_MT_DEFAULT_STRIDE, &u_s)) && u_s > 0) {
                                stride = (int)u_s;
                            }
                            nT->lpVtbl->Release(nT);
                        }
                    }
                    if (s_sample) {
                        if (raw_bits && scaled_bits) {
                            extract_sample_to_dib(s_sample, raw_bits, img->width, img->height, stride, NULL);

                            scale_bgra_buffer(raw_bits, img->width, img->height, scaled_bits, place_w, place_h, false);

                            /* Ensure alpha is 255 for video frames */
                            for (int p = 0; p < place_w * place_h; ++p) {
                                scaled_bits[p * 4 + 3] = 255;
                            }

                            const uint8_t *final_bits = scaled_bits;
                            if (canvas_bits) {
                                memset(canvas_bits, 0, (size_t)out_w * out_h * 4);
                                blit_bgra_to_canvas(scaled_bits, place_w, place_h, canvas_bits, out_w, out_h, place_x, place_y);
                                final_bits = canvas_bits;
                            }

                            WebPPicture pic;
                            WebPPictureInit(&pic);
                            pic.use_argb = 1;
                            pic.width = out_w;
                            pic.height = out_h;
                            WebPPictureImportBGRA(&pic, (uint8_t*)final_bits, out_w * 4);
                            WebPAnimEncoderAdd(enc, &pic, timestamp_ms, &config);
                            WebPPictureFree(&pic);
                        }
                        s_sample->lpVtbl->Release(s_sample);
                        timestamp_ms += frame_delay;
                        frame_idx++;

                        if (frame_idx % 10 == 0 || frame_idx == 1) {
                            char pmsg[128];
                            snprintf(pmsg, sizeof(pmsg), "Encoding video frame %d (@ %d FPS) to Animated WebP...", frame_idx, fps);
                            int pct = 15 + (frame_idx * 40 / max_frames);
                            if (pct > 58) pct = 58;
                            update_save_progress(pmsg, pct);
                        }
                    }
                }
                if (raw_bits) free(raw_bits);
                if (scaled_bits) free(scaled_bits);
                if (canvas_bits) free(canvas_bits);

                update_save_progress("Assembling Animated WebP payload...", 58);

                WebPAnimEncoderAdd(enc, NULL, timestamp_ms, NULL);
                WebPData webp_data;
                WebPDataInit(&webp_data);
                bool ok = false;
                if (WebPAnimEncoderAssemble(enc, &webp_data)) {
                    FILE *f = fopen(dest_filepath, "wb");
                    if (f) {
                        fwrite(webp_data.bytes, 1, webp_data.size, f);
                        fclose(f);
                        ok = true;
                    }
                    WebPDataClear(&webp_data);
                }
                WebPAnimEncoderDelete(enc);
                v_reader->lpVtbl->Release(v_reader);
                if (ok) return true;
            } else {
                v_reader->lpVtbl->Release(v_reader);
            }
        }
    }

    /* 2. If animated WebP: decode all frames and re-encode */
    if (img->is_animated && (img->media_type == MEDIA_TYPE_ANIMATED_WEBP || img->webp_ctx != NULL || (!img->frames && img->frame_count > 1))) {
        const uint8_t *file_data = NULL;
        size_t file_size = 0;
        bool must_free_file_data = false;

        if (img->webp_ctx && img->webp_ctx->file_data && img->webp_ctx->file_size > 0) {
            file_data = img->webp_ctx->file_data;
            file_size = img->webp_ctx->file_size;
        } else if (img->filepath[0] != '\0') {
            FILE *f = fopen(img->filepath, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long fsz = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (fsz > 12) {
                    uint8_t *tmp_buf = (uint8_t*)malloc(fsz);
                    if (tmp_buf && fread(tmp_buf, 1, fsz, f) == (size_t)fsz) {
                        file_data = tmp_buf;
                        file_size = (size_t)fsz;
                        must_free_file_data = true;
                    } else if (tmp_buf) {
                        free(tmp_buf);
                    }
                }
                fclose(f);
            }
        }

        if (file_data && file_size > 0) {
            WebPData webp_in;
            webp_in.bytes = file_data;
            webp_in.size = file_size;

            WebPAnimDecoderOptions dec_opt;
            WebPAnimDecoderOptionsInit(&dec_opt);
            dec_opt.color_mode = MODE_BGRA;
            dec_opt.use_threads = 1;

            WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_in, &dec_opt);
            if (dec) {
                WebPAnimInfo info;
                if (WebPAnimDecoderGetInfo(dec, &info) && info.frame_count > 1) {
                    WebPAnimEncoderOptions enc_options;
                    WebPAnimEncoderOptionsInit(&enc_options);
                    WebPAnimEncoder *enc = WebPAnimEncoderNew(out_w, out_h, &enc_options);
                    if (enc) {
                        WebPConfig config;
                        WebPConfigInit(&config);
                        config.lossless = 0;
                        config.quality = 90.0f;
                        config.method = 2; /* Fast high-quality multi-threaded encoding */

                        int place_x = 0, place_y = 0, place_w = out_w, place_h = out_h;
                        if (resize) {
                            get_canvas_image_dest_rect(out_w, out_h, img->width, img->height, g_canvas_fit_mode, g_canvas_alignment, &place_x, &place_y, &place_w, &place_h);
                        }

                        uint8_t *scaled_bits = NULL;
                        if (place_w != img->width || place_h != img->height) {
                            scaled_bits = (uint8_t*)malloc((size_t)place_w * place_h * 4);
                        }
                        uint8_t *canvas_bits = (place_w != out_w || place_h != out_h || place_x != 0 || place_y != 0) ? (uint8_t*)malloc((size_t)out_w * out_h * 4) : NULL;
                        int prev_timestamp_ms = 0;
                        int frame_idx = 0;
                        int max_frames = (max_frames_limit > 0) ? max_frames_limit : (int)info.frame_count;

                        while (WebPAnimDecoderHasMoreFrames(dec) && frame_idx < max_frames) {
                            uint8_t *frame_buf = NULL;
                            int timestamp_ms = 0;
                            if (!WebPAnimDecoderGetNext(dec, &frame_buf, &timestamp_ms)) break;

                            const uint8_t *out_pixels = frame_buf;
                            if (place_w != img->width || place_h != img->height) {
                                if (scaled_bits) {
                                    scale_bgra_software_bilinear(frame_buf, img->width, img->height, scaled_bits, place_w, place_h);
                                    out_pixels = scaled_bits;
                                }
                            }

                            if (canvas_bits) {
                                memset(canvas_bits, 0, (size_t)out_w * out_h * 4);
                                blit_bgra_to_canvas(out_pixels, place_w, place_h, canvas_bits, out_w, out_h, place_x, place_y);
                                out_pixels = canvas_bits;
                            }

                            WebPPicture pic;
                            WebPPictureInit(&pic);
                            pic.use_argb = 1;
                            pic.width = out_w;
                            pic.height = out_h;
                            WebPPictureImportBGRA(&pic, (uint8_t*)out_pixels, out_w * 4);
                            WebPAnimEncoderAdd(enc, &pic, prev_timestamp_ms, &config);
                            WebPPictureFree(&pic);

                            prev_timestamp_ms = timestamp_ms;
                            frame_idx++;

                            if (frame_idx % 5 == 0 || frame_idx == 1) {
                                char pmsg[128];
                                snprintf(pmsg, sizeof(pmsg), "Encoding WebP animation frame %d / %d (@ %dx%d)...", frame_idx, max_frames, out_w, out_h);
                                int pct = 15 + (frame_idx * 43 / (max_frames > 0 ? max_frames : 1));
                                if (pct > 58) pct = 58;
                                update_save_progress(pmsg, pct);
                            }
                        }

                        update_save_progress("Assembling Animated WebP payload...", 58);

                        WebPAnimEncoderAdd(enc, NULL, prev_timestamp_ms, NULL);
                        WebPData webp_out;
                        WebPDataInit(&webp_out);
                        bool ok = false;
                        if (WebPAnimEncoderAssemble(enc, &webp_out)) {
                            FILE *fo = fopen(dest_filepath, "wb");
                            if (fo) {
                                fwrite(webp_out.bytes, 1, webp_out.size, fo);
                                fclose(fo);
                                ok = true;
                            }
                            WebPDataClear(&webp_out);
                        }

                        if (scaled_bits) free(scaled_bits);
                        if (canvas_bits) free(canvas_bits);
                        WebPAnimEncoderDelete(enc);
                        WebPAnimDecoderDelete(dec);
                        if (must_free_file_data) free((void*)file_data);
                        if (ok) return true;
                    }
                }
                WebPAnimDecoderDelete(dec);
            }
            if (must_free_file_data) free((void*)file_data);
        }
    }

    /* 3. If animated GIF: encode all frames into animated WebP */
    if (img->is_animated && img->frames && img->frame_count > 1) {
        WebPAnimEncoderOptions enc_options;
        WebPAnimEncoderOptionsInit(&enc_options);
        WebPAnimEncoder *enc = WebPAnimEncoderNew(out_w, out_h, &enc_options);
        if (!enc) return false;

        WebPConfig config;
        WebPConfigInit(&config);
        config.lossless = 0;
        config.quality = 90.0f;
        config.method = 2;

        int place_x = 0, place_y = 0, place_w = out_w, place_h = out_h;
        if (resize) {
            get_canvas_image_dest_rect(out_w, out_h, img->width, img->height, g_canvas_fit_mode, g_canvas_alignment, &place_x, &place_y, &place_w, &place_h);
        }

        uint8_t *scaled_bits = (uint8_t*)malloc((size_t)place_w * place_h * 4);
        uint8_t *canvas_bits = (place_w != out_w || place_h != out_h || place_x != 0 || place_y != 0) ? (uint8_t*)malloc((size_t)out_w * out_h * 4) : NULL;
        int timestamp_ms = 0;
        int total_to_export = (max_frames_limit > 0 && img->frame_count > max_frames_limit) ? max_frames_limit : img->frame_count;

        for (int i = 0; i < total_to_export; ++i) {
            const uint8_t *src_bits = (const uint8_t*)img->frames[i].bits;
            if (!src_bits || !scaled_bits) continue;

            if (place_w != img->width || place_h != img->height) {
                scale_bgra_software_bilinear(src_bits, img->width, img->height, scaled_bits, place_w, place_h);
            } else {
                memcpy(scaled_bits, src_bits, (size_t)place_w * place_h * 4);
            }

            /* Unpremultiply alpha for WebPPicture */
            for (int p = 0; p < place_w * place_h; ++p) {
                uint8_t a = scaled_bits[p * 4 + 3];
                if (a > 0 && a < 255) {
                    int b = (int)scaled_bits[p * 4 + 0] * 255 / a;
                    int g = (int)scaled_bits[p * 4 + 1] * 255 / a;
                    int r = (int)scaled_bits[p * 4 + 2] * 255 / a;
                    scaled_bits[p * 4 + 0] = (uint8_t)(b > 255 ? 255 : b);
                    scaled_bits[p * 4 + 1] = (uint8_t)(g > 255 ? 255 : g);
                    scaled_bits[p * 4 + 2] = (uint8_t)(r > 255 ? 255 : r);
                } else if (a == 0) {
                    scaled_bits[p * 4 + 0] = 0;
                    scaled_bits[p * 4 + 1] = 0;
                    scaled_bits[p * 4 + 2] = 0;
                }
            }

            const uint8_t *final_bits = scaled_bits;
            if (canvas_bits) {
                memset(canvas_bits, 0, (size_t)out_w * out_h * 4);
                blit_bgra_to_canvas(scaled_bits, place_w, place_h, canvas_bits, out_w, out_h, place_x, place_y);
                final_bits = canvas_bits;
            }

            WebPPicture pic;
            WebPPictureInit(&pic);
            pic.use_argb = 1;
            pic.width = out_w;
            pic.height = out_h;
            WebPPictureImportBGRA(&pic, (uint8_t*)final_bits, out_w * 4);

            WebPAnimEncoderAdd(enc, &pic, timestamp_ms, &config);
            WebPPictureFree(&pic);

            int dur = img->frames[i].duration_ms > 0 ? img->frames[i].duration_ms : 100;
            timestamp_ms += dur;

            if ((i + 1) % 5 == 0 || i == 0) {
                char pmsg[128];
                snprintf(pmsg, sizeof(pmsg), "Encoding GIF animation frame %d / %d (@ %dx%d)...", i + 1, total_to_export, out_w, out_h);
                int pct = 15 + ((i + 1) * 43 / (total_to_export > 0 ? total_to_export : 1));
                if (pct > 58) pct = 58;
                update_save_progress(pmsg, pct);
            }
        }

        update_save_progress("Assembling Animated WebP payload...", 58);

        if (scaled_bits) free(scaled_bits);
        if (canvas_bits) free(canvas_bits);

        WebPAnimEncoderAdd(enc, NULL, timestamp_ms, NULL);
        WebPData webp_data;
        WebPDataInit(&webp_data);
        bool ok = false;
        if (WebPAnimEncoderAssemble(enc, &webp_data)) {
            FILE *f = fopen(dest_filepath, "wb");
            if (f) {
                fwrite(webp_data.bytes, 1, webp_data.size, f);
                fclose(f);
                ok = true;
            }
            WebPDataClear(&webp_data);
        }
        WebPAnimEncoderDelete(enc);
        return ok;
    }

    /* 4. Single frame WebP export */
    const uint8_t *src_bits = (img->first_frame_bits) ? (const uint8_t*)img->first_frame_bits : (const uint8_t*)img->bits;
    if (!src_bits) return false;

    int place_x = 0, place_y = 0, place_w = out_w, place_h = out_h;
    if (resize) {
        get_canvas_image_dest_rect(out_w, out_h, img->width, img->height, g_canvas_fit_mode, g_canvas_alignment, &place_x, &place_y, &place_w, &place_h);
    }

    uint8_t *scaled_bits = (uint8_t*)malloc((size_t)place_w * place_h * 4);
    if (!scaled_bits) return false;

    scale_bgra_buffer(src_bits, img->width, img->height, scaled_bits, place_w, place_h, true);

    for (int p = 0; p < place_w * place_h; ++p) {
        uint8_t a = scaled_bits[p * 4 + 3];
        if (a > 0 && a < 255) {
            int b = (int)scaled_bits[p * 4 + 0] * 255 / a;
            int g = (int)scaled_bits[p * 4 + 1] * 255 / a;
            int r = (int)scaled_bits[p * 4 + 2] * 255 / a;
            scaled_bits[p * 4 + 0] = (uint8_t)(b > 255 ? 255 : b);
            scaled_bits[p * 4 + 1] = (uint8_t)(g > 255 ? 255 : g);
            scaled_bits[p * 4 + 2] = (uint8_t)(r > 255 ? 255 : r);
        } else if (a == 0) {
            scaled_bits[p * 4 + 0] = 0;
            scaled_bits[p * 4 + 1] = 0;
            scaled_bits[p * 4 + 2] = 0;
        }
    }

    const uint8_t *final_bits = scaled_bits;
    uint8_t *canvas_bits = NULL;
    if (place_w != out_w || place_h != out_h || place_x != 0 || place_y != 0) {
        canvas_bits = (uint8_t*)calloc((size_t)out_w * out_h * 4, 1);
        if (canvas_bits) {
            blit_bgra_to_canvas(scaled_bits, place_w, place_h, canvas_bits, out_w, out_h, place_x, place_y);
            final_bits = canvas_bits;
        }
    }

    uint8_t *output = NULL;
    size_t out_size = WebPEncodeLosslessBGRA(final_bits, out_w, out_h, out_w * 4, &output);
    bool ok = false;
    if (out_size > 0 && output) {
        FILE *f = fopen(dest_filepath, "wb");
        if (f) {
            fwrite(output, 1, out_size, f);
            fclose(f);
            ok = true;
        }
        WebPFree(output);
    }
    free(scaled_bits);
    if (canvas_bits) free(canvas_bits);
    return ok;
}

/* GIF Export Helper via WIC with full palette generation and 1-bit transparency support */
static bool export_gif_file(
    const CanvasImage *img,
    const char *dest_filepath,
    bool resize,
    int target_w,
    int target_h,
    int max_frames_limit
) {
    if (!img || !dest_filepath || dest_filepath[0] == '\0' || img->width <= 0 || img->height <= 0) return false;

    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr) || !factory) return false;

    IWICStream *stream = NULL;
    hr = factory->lpVtbl->CreateStream(factory, &stream);
    if (FAILED(hr) || !stream) {
        factory->lpVtbl->Release(factory);
        return false;
    }

    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_ACP, 0, dest_filepath, -1, wpath, MAX_PATH);

    hr = stream->lpVtbl->InitializeFromFilename(stream, wpath, GENERIC_WRITE);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    IWICBitmapEncoder *encoder = NULL;
    hr = CoCreateInstance(&CLSID_WICGifEncoder, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICBitmapEncoder, (void**)&encoder);
    if (FAILED(hr) || !encoder) {
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    hr = encoder->lpVtbl->Initialize(encoder, (IStream*)stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        encoder->lpVtbl->Release(encoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    int out_w = (resize && target_w > 0) ? target_w : img->width;
    int out_h = (resize && target_h > 0) ? target_h : img->height;
    if (out_w <= 0) out_w = 640;
    if (out_h <= 0) out_h = 480;

    int place_x = 0, place_y = 0, place_w = out_w, place_h = out_h;
    if (resize) {
        get_canvas_image_dest_rect(out_w, out_h, img->width, img->height, g_canvas_fit_mode, g_canvas_alignment, &place_x, &place_y, &place_w, &place_h);
    }

    bool is_anim = (img->is_animated && img->frames && img->frame_count > 1);
    int total_frames = 1;
    if (is_anim) {
        total_frames = (max_frames_limit > 0 && img->frame_count > max_frames_limit) ? max_frames_limit : img->frame_count;
    }

    uint8_t *scaled_bits = (uint8_t*)malloc((size_t)place_w * place_h * 4);
    uint8_t *px = (uint8_t*)calloc((size_t)out_w * out_h * 4, 1);
    if (!scaled_bits || !px) {
        if (scaled_bits) free(scaled_bits);
        if (px) free(px);
        encoder->lpVtbl->Release(encoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        return false;
    }

    bool global_palette_set = false;

    for (int fr = 0; fr < total_frames; ++fr) {
        const uint8_t *src_bits = NULL;
        if (is_anim && img->frames[fr].bits) {
            src_bits = (const uint8_t*)img->frames[fr].bits;
        } else if (img->first_frame_bits) {
            src_bits = (const uint8_t*)img->first_frame_bits;
        } else {
            src_bits = (const uint8_t*)img->bits;
        }
        if (!src_bits) continue;

        /* 1. Scale frame */
        scale_bgra_buffer(src_bits, img->width, img->height, scaled_bits, place_w, place_h, true);

        /* 2. Unpremultiply alpha */
        for (int p = 0; p < place_w * place_h; ++p) {
            uint8_t a = scaled_bits[p * 4 + 3];
            if (a > 0 && a < 255) {
                int b = (int)scaled_bits[p * 4 + 0] * 255 / a;
                int g = (int)scaled_bits[p * 4 + 1] * 255 / a;
                int r = (int)scaled_bits[p * 4 + 2] * 255 / a;
                scaled_bits[p * 4 + 0] = (uint8_t)(b > 255 ? 255 : b);
                scaled_bits[p * 4 + 1] = (uint8_t)(g > 255 ? 255 : g);
                scaled_bits[p * 4 + 2] = (uint8_t)(r > 255 ? 255 : r);
            } else if (a == 0) {
                scaled_bits[p * 4 + 0] = 0;
                scaled_bits[p * 4 + 1] = 0;
                scaled_bits[p * 4 + 2] = 0;
            }
        }

        /* 3. Blit to target canvas buffer */
        memset(px, 0, (size_t)out_w * out_h * 4);
        blit_bgra_to_canvas(scaled_bits, place_w, place_h, px, out_w, out_h, place_x, place_y);

        /* 4. Create new frame on encoder */
        IWICBitmapFrameEncode *frame = NULL;
        IPropertyBag2 *propBag = NULL;
        hr = encoder->lpVtbl->CreateNewFrame(encoder, &frame, &propBag);
        if (FAILED(hr) || !frame) continue;

        hr = frame->lpVtbl->Initialize(frame, propBag);
        frame->lpVtbl->SetSize(frame, (UINT)out_w, (UINT)out_h);

        /* 5. Create 32bpp WIC bitmap from px */
        IWICBitmap *bitmap = NULL;
        UINT stride32 = (UINT)out_w * 4;
        hr = factory->lpVtbl->CreateBitmapFromMemory(factory, (UINT)out_w, (UINT)out_h,
                                                     &GUID_WICPixelFormat32bppBGRA,
                                                     stride32, stride32 * (UINT)out_h,
                                                     px, &bitmap);
        if (SUCCEEDED(hr) && bitmap) {
            /* 6. Generate 256-color palette with transparency support */
            IWICPalette *palette = NULL;
            hr = factory->lpVtbl->CreatePalette(factory, &palette);
            if (SUCCEEDED(hr) && palette) {
                hr = palette->lpVtbl->InitializeFromBitmap(palette, (IWICBitmapSource*)bitmap, 256, TRUE);
                if (FAILED(hr)) {
                    palette->lpVtbl->InitializePredefined(palette, WICBitmapPaletteTypeFixedHalftone256, TRUE);
                }

                if (!global_palette_set) {
                    encoder->lpVtbl->SetPalette(encoder, palette);
                    global_palette_set = true;
                }
                frame->lpVtbl->SetPalette(frame, palette);

                WICPixelFormatGUID pfmt = GUID_WICPixelFormat8bppIndexed;
                frame->lpVtbl->SetPixelFormat(frame, &pfmt);

                /* 7. Convert 32bpp BGRA to 8bpp Indexed */
                IWICFormatConverter *converter = NULL;
                hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
                if (SUCCEEDED(hr) && converter) {
                    hr = converter->lpVtbl->Initialize(converter,
                                                       (IWICBitmapSource*)bitmap,
                                                       &GUID_WICPixelFormat8bppIndexed,
                                                       WICBitmapDitherTypeNone,
                                                       palette,
                                                       0.5f,
                                                       WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        frame->lpVtbl->WriteSource(frame, (IWICBitmapSource*)converter, NULL);
                    }
                    converter->lpVtbl->Release(converter);
                }
                palette->lpVtbl->Release(palette);
            }
            bitmap->lpVtbl->Release(bitmap);
        }

        /* 8. If animated, set GIF frame delay metadata */
        if (is_anim) {
            IWICMetadataQueryWriter *meta = NULL;
            if (SUCCEEDED(frame->lpVtbl->GetMetadataQueryWriter(frame, &meta)) && meta) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_UI2;
                int ms = (img->frames[fr].duration_ms > 0) ? img->frames[fr].duration_ms : 100;
                pv.uiVal = (USHORT)(ms / 10); /* 10ms units */
                meta->lpVtbl->SetMetadataByName(meta, L"/grctlext/Delay", &pv);
                meta->lpVtbl->Release(meta);
            }
        }

        frame->lpVtbl->Commit(frame);
        if (propBag) propBag->lpVtbl->Release(propBag);
        frame->lpVtbl->Release(frame);
    }

    free(scaled_bits);
    free(px);

    /* 9. Commit encoder to flush all GIF blocks to disk */
    hr = encoder->lpVtbl->Commit(encoder);

    encoder->lpVtbl->Release(encoder);
    stream->lpVtbl->Release(stream);
    factory->lpVtbl->Release(factory);

    return SUCCEEDED(hr);
}

bool image_export_to_file_ex(
    const CanvasImage *img,
    const char *dest_filepath,
    ImageExportFormat format,
    bool resize,
    int target_w,
    int target_h,
    int max_frames
) {
    if (!img || !dest_filepath || dest_filepath[0] == '\0') return false;

    /* 1. Fast direct copy if keeping original format and not resizing, dimensions unchanged, and not video */
    bool is_dim_changed = (img->orig_width > 0 && (img->width != img->orig_width || img->height != img->orig_height));
    if (format == IMG_EXPORT_KEEP_ORIGINAL && !resize && !is_dim_changed && !img->is_video) {
        if (img->filepath[0] != '\0' && GetFileAttributesA(img->filepath) != INVALID_FILE_ATTRIBUTES) {
            if (_stricmp(img->filepath, dest_filepath) == 0) {
                return true;
            }
            if (CopyFileA(img->filepath, dest_filepath, FALSE)) {
                return true;
            }
        }
    }

    /* 2. Determine target extension */
    char ext[16] = {0};
    image_get_extension(dest_filepath, ext, sizeof(ext));

    /* 3. Handle WebP Export */
    if (format == IMG_EXPORT_WEBP || _stricmp(ext, "webp") == 0) {
        return export_webp_file(img, dest_filepath, resize, target_w, target_h, max_frames);
    }

    /* 3.1 Handle GIF Export (Palette generation + Transparency) */
    if (format == IMG_EXPORT_GIF || _stricmp(ext, "gif") == 0) {
        return export_gif_file(img, dest_filepath, resize, target_w, target_h, max_frames);
    }

    /* 4. Determine target dimensions & source bits */
    const uint8_t *src_bits = (img->first_frame_bits) ? (const uint8_t*)img->first_frame_bits : (const uint8_t*)img->bits;
    if (!src_bits || img->width <= 0 || img->height <= 0) {
        return false;
    }

    int out_w = (resize && target_w > 0) ? target_w : img->width;
    int out_h = (resize && target_h > 0) ? target_h : img->height;
    if (out_w <= 0) out_w = 640;
    if (out_h <= 0) out_h = 480;

    CLSID encoder_clsid = CLSID_WICPngEncoder;
    bool is_jpeg = false;

    if (format == IMG_EXPORT_JPG || _stricmp(ext, "jpg") == 0 || _stricmp(ext, "jpeg") == 0) {
        encoder_clsid = CLSID_WICJpegEncoder;
        is_jpeg = true;
    } else if (format == IMG_EXPORT_BMP || _stricmp(ext, "bmp") == 0) {
        encoder_clsid = CLSID_WICBmpEncoder;
    } else {
        encoder_clsid = CLSID_WICPngEncoder;
    }

    /* 5. Scale to buffer and unpremultiply alpha for WIC encoder */
    int place_x = 0, place_y = 0, place_w = out_w, place_h = out_h;
    if (resize) {
        get_canvas_image_dest_rect(out_w, out_h, img->width, img->height, g_canvas_fit_mode, g_canvas_alignment, &place_x, &place_y, &place_w, &place_h);
    }

    uint8_t *scaled_bits = (uint8_t*)malloc((size_t)place_w * place_h * 4);
    if (!scaled_bits) return false;

    scale_bgra_buffer(src_bits, img->width, img->height, scaled_bits, place_w, place_h, true);

    for (int i = 0; i < place_w * place_h; ++i) {
        uint8_t a = scaled_bits[i * 4 + 3];
        if (a > 0 && a < 255) {
            int b = (int)scaled_bits[i * 4 + 0] * 255 / a;
            int g = (int)scaled_bits[i * 4 + 1] * 255 / a;
            int r = (int)scaled_bits[i * 4 + 2] * 255 / a;
            scaled_bits[i * 4 + 0] = (uint8_t)(b > 255 ? 255 : b);
            scaled_bits[i * 4 + 1] = (uint8_t)(g > 255 ? 255 : g);
            scaled_bits[i * 4 + 2] = (uint8_t)(r > 255 ? 255 : r);
        } else if (a == 0) {
            scaled_bits[i * 4 + 0] = 0;
            scaled_bits[i * 4 + 1] = 0;
            scaled_bits[i * 4 + 2] = 0;
        }
    }

    uint8_t *px = (uint8_t*)calloc((size_t)out_w * out_h * 4, 1);
    if (!px) {
        free(scaled_bits);
        return false;
    }

    blit_bgra_to_canvas(scaled_bits, place_w, place_h, px, out_w, out_h, place_x, place_y);
    free(scaled_bits);

    /* 6. Encode via WIC */
    IWICImagingFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr) || !factory) {
        free(px);
        return false;
    }

    IWICStream *stream = NULL;
    hr = factory->lpVtbl->CreateStream(factory, &stream);
    if (FAILED(hr) || !stream) {
        factory->lpVtbl->Release(factory);
        free(px);
        return false;
    }

    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_ACP, 0, dest_filepath, -1, wpath, MAX_PATH);

    hr = stream->lpVtbl->InitializeFromFilename(stream, wpath, GENERIC_WRITE);
    if (FAILED(hr)) {
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        free(px);
        return false;
    }

    IWICBitmapEncoder *encoder = NULL;
    hr = CoCreateInstance(&encoder_clsid, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICBitmapEncoder, (void**)&encoder);
    if (FAILED(hr) || !encoder) {
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        free(px);
        return false;
    }

    hr = encoder->lpVtbl->Initialize(encoder, (IStream*)stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        encoder->lpVtbl->Release(encoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        free(px);
        return false;
    }

    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *propBag = NULL;
    hr = encoder->lpVtbl->CreateNewFrame(encoder, &frame, &propBag);
    if (FAILED(hr) || !frame) {
        encoder->lpVtbl->Release(encoder);
        stream->lpVtbl->Release(stream);
        factory->lpVtbl->Release(factory);
        free(px);
        return false;
    }

    hr = frame->lpVtbl->Initialize(frame, propBag);
    frame->lpVtbl->SetSize(frame, (UINT)out_w, (UINT)out_h);

    if (is_jpeg) {
        WICPixelFormatGUID pfmt = GUID_WICPixelFormat24bppBGR;
        frame->lpVtbl->SetPixelFormat(frame, &pfmt);

        UINT stride = ((out_w * 3 + 3) / 4) * 4;
        BYTE *bgr = (BYTE*)calloc(1, stride * out_h);
        if (bgr) {
            for (int y = 0; y < out_h; ++y) {
                for (int x = 0; x < out_w; ++x) {
                    int s = (y * out_w + x) * 4;
                    int d = y * stride + x * 3;
                    bgr[d + 0] = px[s + 0];
                    bgr[d + 1] = px[s + 1];
                    bgr[d + 2] = px[s + 2];
                }
            }
            frame->lpVtbl->WritePixels(frame, out_h, stride, stride * out_h, bgr);
            free(bgr);
        }
    } else {
        WICPixelFormatGUID pfmt = GUID_WICPixelFormat32bppBGRA;
        frame->lpVtbl->SetPixelFormat(frame, &pfmt);
        UINT stride = out_w * 4;
        frame->lpVtbl->WritePixels(frame, out_h, stride, stride * out_h, (BYTE*)px);
    }

    frame->lpVtbl->Commit(frame);
    encoder->lpVtbl->Commit(encoder);

    if (propBag) propBag->lpVtbl->Release(propBag);
    frame->lpVtbl->Release(frame);
    encoder->lpVtbl->Release(encoder);
    stream->lpVtbl->Release(stream);
    factory->lpVtbl->Release(factory);
    free(px);

    return true;
}

bool image_export_to_file(
    const CanvasImage *img,
    const char *dest_filepath,
    ImageExportFormat format,
    bool resize,
    int target_w,
    int target_h
) {
    return image_export_to_file_ex(img, dest_filepath, format, resize, target_w, target_h, 0);
}
