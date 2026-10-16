#include "level_io.h"
#include "hud_overlay.h"
#include "dialog_modals.h"
#include "undo_redo.h"
#include <shobjidl.h>

/* Handle Universal Path Import (.dat & .svg) */
void handle_import_path(HWND hwnd) {
    char file_buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "All Supported Curve Paths (*.dat;*.svg)\0*.dat;*.svg\0Zuma Deluxe Path (*.dat)\0*.dat\0Scalable Vector Graphics (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = sizeof(file_buf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Import Curve Path";

    if (!GetOpenFileNameA(&ofn)) return;

    /* Detect file format from extension or binary signature */
    const char *ext = strrchr(file_buf, '.');
    bool is_dat = (ext && _stricmp(ext, ".dat") == 0);
    bool is_svg = (ext && _stricmp(ext, ".svg") == 0);

    if (!is_dat && !is_svg) {
        /* Sniff file signature */
        FILE *tf = fopen(file_buf, "rb");
        if (tf) {
            char magic[5] = {0};
            if (fread(magic, 1, 4, tf) == 4 && memcmp(magic, "CURV", 4) == 0) {
                is_dat = true;
            } else {
                is_svg = true;
            }
            fclose(tf);
        }
    }

    if (is_dat) {
        DatImportError err = DAT_ERR_OK;
        BezierPath imported_dat;
        bezier_path_init(&imported_dat, "curve");
        if (!bezier_path_import_dat_ex(&imported_dat, file_buf, 0.0f, &err)) {
            bezier_path_free(&imported_dat);
            char err_body[512];
            snprintf(err_body, sizeof(err_body),
                "Failed to import Zuma Deluxe curve path (.dat).\n\n"
                "File: %s\n"
                "Reason: %s",
                file_buf, dat_import_error_to_string(err));
            show_themed_notice_dialog(
                hwnd,
                "Import Path Failed - ZuPather",
                "Invalid Zuma Path Data",
                err_body,
                540, 260
            );
            return;
        }

        /* If canvas is not open yet, auto-create standard canvas 640x480 */
        if (!g_has_canvas) {
            g_canvas_w = 640;
            g_canvas_h = 480;
            g_has_canvas = true;
            const char *base = strrchr(file_buf, '\\');
            if (!base) base = strrchr(file_buf, '/');
            base = base ? base + 1 : file_buf;
            strncpy(g_canvas_name, base, sizeof(g_canvas_name) - 1);
            char *dot = strrchr(g_canvas_name, '.');
            if (dot) *dot = '\0';
        }

        bool replace = false;
        if (g_path_count > 0) {
            char choice_body[512];
            snprintf(choice_body, sizeof(choice_body),
                "Curve paths already exist in this project (%d track%s).\n\n"
                "Choose 'Replace' to overwrite existing project paths with the imported Zuma path,\n"
                "or 'Append' to add it as an additional track alongside existing paths.",
                g_path_count, g_path_count > 1 ? "s" : "");

            int res = show_themed_choice_dialog(
                hwnd,
                "Import Path - ZuPather",
                "Existing Curve Paths Detected",
                choice_body,
                "Replace",
                "Append",
                "Cancel"
            );
            if (res == 0) {
                bezier_path_free(&imported_dat);
                return;
            }
            replace = (res == 1);
        } else {
            replace = true;
        }

        undo_ensure_baseline();

        if (replace) {
            for (int p = 0; p < g_path_count; ++p) {
                bezier_path_free(&g_paths[p]);
                memset(&g_paths[p], 0, sizeof(BezierPath));
            }
            g_path_count = 0;
        } else {
            if (g_path_count >= 10) {
                bezier_path_free(&imported_dat);
                show_themed_notice_dialog(
                    hwnd,
                    "Path Limit Exceeded - ZuPather",
                    "Track Limit Reached (Max 10)",
                    "Cannot append additional paths: the project has reached the Zuma Compleluxe limit of 10 curve paths.",
                    520, 240
                );
                return;
            }
        }

        int dest_idx = g_path_count;
        g_paths[dest_idx] = imported_dat;
        if (!replace) {
            snprintf(g_paths[dest_idx].name, sizeof(g_paths[dest_idx].name), "%s-%d",
                     g_canvas_name[0] ? g_canvas_name : "curve", dest_idx + 1);
        } else {
            const char *base = strrchr(file_buf, '\\');
            if (!base) base = strrchr(file_buf, '/');
            base = base ? base + 1 : file_buf;
            strncpy(g_paths[dest_idx].name, base, sizeof(g_paths[dest_idx].name) - 1);
            char *dot = strrchr(g_paths[dest_idx].name, '.');
            if (dot) *dot = '\0';
        }

        path_cache_invalidate(&g_paths[dest_idx]);
        path_cache_update(&g_paths[dest_idx]);
        g_path_count++;

        g_active_path_idx = dest_idx;
        g_active_node_idx = -1;
        for (int p = 0; p < g_path_count; ++p) {
            for (int i = 0; i < g_paths[p].node_count; ++i) {
                g_paths[p].nodes[i].selected = false;
            }
        }

        InvalidateRect(hwnd, NULL, FALSE);
        update_status_bar();
        undo_snapshot_capture("Import Path");

        char msg[512];
        snprintf(msg, sizeof(msg),
            "Successfully imported Zuma Deluxe curve path (.dat).\n\n"
            "File: %s\n"
            "Track: %s (%d nodes, %d attribute spans, length: %.1f px)",
            file_buf, g_paths[dest_idx].name, g_paths[dest_idx].node_count,
            g_paths[dest_idx].span_count, g_paths[dest_idx].cache.total_length);
        show_themed_notice_dialog(
            hwnd,
            "Import Complete - ZuPather",
            "Zuma Deluxe Path Import Succeeded",
            msg,
            540, 260
        );
        return;
    }

    /* SVG Import Pipeline */
    int max_import = 2048;
    BezierPath *imported = (BezierPath *)calloc(max_import, sizeof(BezierPath));
    if (!imported) return;
    int imported_count = 0;
    SvgDimensions dims;
    memset(&dims, 0, sizeof(dims));

    if (!svg_path_import_from_file(file_buf, imported, max_import, &imported_count, &dims) || imported_count == 0) {
        free(imported);
        char err_body[512];
        snprintf(err_body, sizeof(err_body),
            "Could not find or parse any valid path data in the selected SVG file.\n\n"
            "File: %s\n\n"
            "Please check that the SVG contains standard <path d=\"...\"> elements.",
            file_buf);
        show_themed_notice_dialog(
            hwnd,
            "Import SVG Failed - ZuPather",
            "No Valid Path Data Found",
            err_body,
            540, 260
        );
        return;
    }

    /* If canvas is not open yet, auto-create transparent canvas from SVG dimensions */
    if (!g_has_canvas) {
        g_canvas_w = dims.has_viewbox ? (int)roundf(dims.view_w) : 640;
        g_canvas_h = dims.has_viewbox ? (int)roundf(dims.view_h) : 480;
        if (g_canvas_w < 100) g_canvas_w = 640;
        if (g_canvas_h < 100) g_canvas_h = 480;
        g_has_canvas = true;
        const char *base = strrchr(file_buf, '\\');
        if (!base) base = strrchr(file_buf, '/');
        base = base ? base + 1 : file_buf;
        strncpy(g_canvas_name, base, sizeof(g_canvas_name) - 1);
        char *dot = strrchr(g_canvas_name, '.');
        if (dot) *dot = '\0';
    }

    bool replace = false;
    if (g_path_count > 0) {
        char choice_body[512];
        if (g_path_count == 1) {
            snprintf(choice_body, sizeof(choice_body),
                "A curve path already exists in this project.\n\n"
                "Choose 'Replace' to overwrite existing project paths with the imported SVG,\n"
                "or 'Append' to add them as additional tracks alongside existing paths."
            );
        } else {
            snprintf(choice_body, sizeof(choice_body),
                "Curve paths already exist in this project (%d paths).\n\n"
                "Choose 'Replace' to overwrite existing project paths with the imported SVG,\n"
                "or 'Append' to add them as additional tracks alongside existing paths.",
                g_path_count
            );
        }

        int res = show_themed_choice_dialog(
            hwnd,
            "Import SVG Path - ZuPather",
            "Existing Curve Paths Detected",
            choice_body,
            "Replace",
            "Append",
            "Cancel"
        );
        if (res == 0) {
            for (int p = 0; p < imported_count; ++p) bezier_path_free(&imported[p]);
            free(imported);
            return;
        }
        replace = (res == 1);
    } else {
        replace = true;
    }

    int old_canvas_count = g_path_count;
    int total_resulting_paths = replace ? imported_count : (old_canvas_count + imported_count);
    if (total_resulting_paths > 10) {
        show_import_path_limit_warning(hwnd, old_canvas_count, imported_count, replace);
    }

    undo_ensure_baseline();

    if (replace) {
        for (int p = 0; p < g_path_count; ++p) {
            bezier_path_free(&g_paths[p]);
            memset(&g_paths[p], 0, sizeof(BezierPath));
        }
        g_path_count = 0;
    }

    int start_p = g_path_count;
    int can_add = 10 - g_path_count;
    if (can_add < 0) can_add = 0;
    int to_add = (imported_count > can_add) ? can_add : imported_count;

    for (int p = 0; p < to_add; ++p) {
        g_paths[g_path_count] = imported[p];
        if (!replace) {
            snprintf(g_paths[g_path_count].name, sizeof(g_paths[g_path_count].name), "%s-%d",
                     g_canvas_name[0] ? g_canvas_name : "curve", g_path_count + 1);
        }
        path_cache_invalidate(&g_paths[g_path_count]);
        path_cache_update(&g_paths[g_path_count]);
        g_path_count++;
    }

    /* Free all excess imported paths that were not added (path #11 onwards) */
    for (int p = to_add; p < imported_count; ++p) {
        bezier_path_free(&imported[p]);
    }
    free(imported);

    g_active_path_idx = start_p < g_path_count ? start_p : 0;
    g_active_node_idx = -1;
    for (int p = 0; p < g_path_count; ++p) {
        for (int i = 0; i < g_paths[p].node_count; ++i) {
            g_paths[p].nodes[i].selected = false;
        }
    }

    InvalidateRect(hwnd, NULL, FALSE);
    update_status_bar();
    undo_snapshot_capture("Import Path");

    char msg[384];
    if (total_resulting_paths > 10) {
        if (!replace) {
            snprintf(msg, sizeof(msg),
                "Import complete.\n\n"
                "Preserved %d existing canvas track%s and added first %d track%s from SVG (10 tracks total).\n"
                "(%d excess track%s from SVG discarded due to Zuma Compleluxe limit).",
                old_canvas_count, old_canvas_count > 1 ? "s" : "",
                to_add, to_add > 1 ? "s" : "",
                imported_count - to_add, (imported_count - to_add) > 1 ? "s" : ""
            );
        } else {
            snprintf(msg, sizeof(msg),
                "Import complete.\n\n"
                "Imported first 10 tracks from SVG.\n"
                "(%d excess track%s from SVG discarded due to Zuma Compleluxe limit).",
                imported_count - 10, (imported_count - 10) > 1 ? "s" : ""
            );
        }
    } else {
        snprintf(msg, sizeof(msg),
            "Successfully imported %d path%s from SVG.",
            to_add, to_add > 1 ? "s" : "");
    }
    show_themed_notice_dialog(
        hwnd,
        "Import Complete - ZuPather",
        "SVG Import Succeeded",
        msg,
        500, 250
    );
}

void handle_import_svg_path(HWND hwnd) {
    handle_import_path(hwnd);
}

/* Handle Universal Path Export (.dat & .svg) */
void handle_export_path(HWND hwnd) {
    if (g_path_count == 0) {
        show_themed_notice_dialog(
            hwnd,
            "Export Path - ZuPather",
            "No Curve Paths Found",
            "There are currently no curve paths in this project to export.\n\nPlease create or draw curve paths in Curve Mode first.",
            500, 250
        );
        return;
    }

    char file_buf[MAX_PATH] = {0};
    if (g_canvas_name[0]) {
        snprintf(file_buf, sizeof(file_buf), "%s.dat", g_canvas_name);
    } else {
        snprintf(file_buf, sizeof(file_buf), "curve.dat");
    }

    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Zuma Deluxe Path (*.dat)\0*.dat\0Scalable Vector Graphics (*.svg)\0*.svg\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = sizeof(file_buf);
    ofn.lpstrDefExt = "dat";
    ofn.Flags = OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Export Curve Path";

    if (!GetSaveFileNameA(&ofn)) return;

    const char *ext = strrchr(file_buf, '.');
    bool is_svg = (ext && _stricmp(ext, ".svg") == 0);

    if (!is_svg) {
        /* Zuma Deluxe Binary .dat Export */
        if (g_path_count == 1) {
            DWORD attrs = GetFileAttributesA(file_buf);
            bool file_already_exists = (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
            if (file_already_exists) {
                bool do_overwrite = show_themed_confirm_dialog(
                    hwnd,
                    "Export Zuma Path - ZuPather",
                    "Overwrite Existing File?",
                    "The selected file already exists.\n\nWould you like to replace it with the exported Zuma Deluxe curve path?",
                    false
                );
                if (!do_overwrite) return;
            }

            if (!bezier_path_export_dat(&g_paths[0], file_buf)) {
                show_themed_notice_dialog(
                    hwnd,
                    "Export Failed - ZuPather",
                    "File Write Error",
                    "Failed to write Zuma Deluxe binary path (.dat) file.\n\nPlease check write permissions or disk space for the target folder.",
                    520, 250
                );
                return;
            }

            char msg[512];
            snprintf(msg, sizeof(msg),
                "Export complete.\n\n"
                "Successfully exported 1 curve path to Zuma Deluxe binary format (.dat).\n\n"
                "File: %s\n"
                "Nodes: %d, Spans: %d, Length: %.1f px",
                file_buf, g_paths[0].node_count, g_paths[0].span_count, g_paths[0].cache.total_length);
            show_themed_notice_dialog(
                hwnd,
                "Export Complete - ZuPather",
                "Zuma Path Export Succeeded",
                msg,
                540, 270
            );
            return;
        } else {
            /* Multi-track export: export each track as [basename]-%d.dat */
            char dir_buf[MAX_PATH];
            char base_buf[MAX_PATH];
            strncpy(dir_buf, file_buf, sizeof(dir_buf) - 1);
            char *last_sep = strrchr(dir_buf, '\\');
            if (!last_sep) last_sep = strrchr(dir_buf, '/');
            if (last_sep) {
                *last_sep = '\0';
                strncpy(base_buf, last_sep + 1, sizeof(base_buf) - 1);
            } else {
                dir_buf[0] = '.'; dir_buf[1] = '\0';
                strncpy(base_buf, file_buf, sizeof(base_buf) - 1);
            }
            char *dot = strrchr(base_buf, '.');
            if (dot) *dot = '\0';

            /* Strip trailing -1 or _1 if user included it in filename */
            size_t blen = strlen(base_buf);
            if (blen > 2 && (base_buf[blen - 2] == '-' || base_buf[blen - 2] == '_') && isdigit((unsigned char)base_buf[blen - 1])) {
                base_buf[blen - 2] = '\0';
            }

            char confirm_msg[600];
            snprintf(confirm_msg, sizeof(confirm_msg),
                "This project contains %d curve tracks.\n\n"
                "ZuPather will export them into Zuma Deluxe multi-track format:\n"
                "  - %s-1.dat\n"
                "  - %s-2.dat\n"
                "  ...\n\n"
                "Target Directory: %s\n\n"
                "Do you want to proceed with exporting all %d tracks?",
                g_path_count, base_buf, base_buf, dir_buf, g_path_count);

            bool proceed = show_themed_confirm_dialog(
                hwnd,
                "Export Multi-Track Zuma Path - ZuPather",
                "Export All Tracks?",
                confirm_msg,
                false
            );
            if (!proceed) return;

            bool all_ok = true;
            char failed_file[MAX_PATH] = {0};
            for (int p = 0; p < g_path_count; ++p) {
                char track_path[MAX_PATH];
                snprintf(track_path, sizeof(track_path), "%s\\%s-%d.dat", dir_buf, base_buf, p + 1);
                if (!bezier_path_export_dat(&g_paths[p], track_path)) {
                    all_ok = false;
                    strncpy(failed_file, track_path, sizeof(failed_file) - 1);
                    break;
                }
            }

            if (!all_ok) {
                char err_msg[512];
                snprintf(err_msg, sizeof(err_msg),
                    "Failed to export multi-track path file.\n\n"
                    "File: %s\n\n"
                    "Please check write permissions for the target folder.",
                    failed_file);
                show_themed_notice_dialog(
                    hwnd,
                    "Export Failed - ZuPather",
                    "File Write Error",
                    err_msg,
                    520, 250
                );
                return;
            }

            char msg[600];
            snprintf(msg, sizeof(msg),
                "Multi-track export complete.\n\n"
                "Successfully exported %d curve tracks to Zuma Deluxe binary format (.dat).\n\n"
                "Directory: %s\n"
                "Files: %s-1.dat ... %s-%d.dat",
                g_path_count, dir_buf, base_buf, base_buf, g_path_count);
            show_themed_notice_dialog(
                hwnd,
                "Export Complete - ZuPather",
                "Multi-Track Zuma Export Succeeded",
                msg,
                560, 280
            );
            return;
        }
    }

    int cw = (g_has_canvas && g_canvas_w > 0) ? g_canvas_w : 640;
    int ch = (g_has_canvas && g_canvas_h > 0) ? g_canvas_h : 480;

    DWORD attrs = GetFileAttributesA(file_buf);
    bool file_already_exists = (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));

    int existing_count = 0;
    int max_existing = 2048;
    BezierPath *existing_paths = NULL;
    SvgDimensions existing_dims;
    memset(&existing_dims, 0, sizeof(existing_dims));

    if (file_already_exists) {
        existing_paths = (BezierPath *)calloc(max_existing, sizeof(BezierPath));
        if (existing_paths) {
            if (!svg_path_import_from_file(file_buf, existing_paths, max_existing, &existing_count, &existing_dims)) {
                existing_count = 0;
            }
        }
    }

    bool replace_file = false;
    bool append_file = false;

    if (file_already_exists) {
        if (existing_count > 0 && existing_paths) {
            char choice_body[512];
            if (existing_count == 1) {
                snprintf(choice_body, sizeof(choice_body),
                    "A curve path already exists in the target SVG file.\n\n"
                    "Choose 'Replace' to overwrite existing paths in the file with project curves,\n"
                    "or 'Append' to add them as additional tracks alongside existing paths."
                );
            } else {
                snprintf(choice_body, sizeof(choice_body),
                    "Curve paths already exist in the target SVG file (%d paths).\n\n"
                    "Choose 'Replace' to overwrite existing paths in the file with project curves,\n"
                    "or 'Append' to add them as additional tracks alongside existing paths.",
                    existing_count
                );
            }

            int res = show_themed_choice_dialog(
                hwnd,
                "Export SVG Path - ZuPather",
                "Existing Curve Paths Detected",
                choice_body,
                "Replace",
                "Append",
                "Cancel"
            );

            if (res == 0) {
                /* Cancelled */
                for (int i = 0; i < existing_count; ++i) {
                    bezier_path_free(&existing_paths[i]);
                }
                free(existing_paths);
                return;
            }
            if (res == 1) {
                replace_file = true;
            } else if (res == 2) {
                append_file = true;
            }
        } else {
            /* File exists but contains 0 paths */
            bool do_overwrite = show_themed_confirm_dialog(
                hwnd,
                "Export SVG Path - ZuPather",
                "Overwrite Existing File?",
                "The selected file already exists.\n\nWould you like to replace it with the exported SVG curve paths?",
                false
            );
            if (!do_overwrite) {
                if (existing_paths) free(existing_paths);
                return;
            }
            replace_file = true;
        }
    } else {
        replace_file = true;
    }

    bool success = false;
    char msg[384] = {0};

    if (append_file && existing_count > 0 && existing_paths) {
        /* Merge non-destructively: preserve existing paths in the file, and append current project paths */
        int combined_count = existing_count + g_path_count;
        BezierPath *combined = (BezierPath *)calloc(combined_count, sizeof(BezierPath));
        if (combined) {
            /* Copy existing paths */
            for (int i = 0; i < existing_count; ++i) {
                combined[i] = existing_paths[i];
            }
            /* Append project paths */
            for (int p = 0; p < g_path_count; ++p) {
                combined[existing_count + p] = g_paths[p];
            }

            int exp_w = (existing_dims.has_viewbox && existing_dims.view_w > 0) ? (int)roundf(existing_dims.view_w) : cw;
            int exp_h = (existing_dims.has_viewbox && existing_dims.view_h > 0) ? (int)roundf(existing_dims.view_h) : ch;
            if (cw > exp_w) exp_w = cw;
            if (ch > exp_h) exp_h = ch;

            success = svg_path_export_to_file(file_buf, combined, combined_count, exp_w, exp_h);
            free(combined);
        }

        /* Free temporary imported paths */
        for (int i = 0; i < existing_count; ++i) {
            bezier_path_free(&existing_paths[i]);
        }
        free(existing_paths);
        existing_paths = NULL;

        if (success) {
            snprintf(msg, sizeof(msg),
                "Export complete.\n\n"
                "Preserved %d existing path%s in target SVG file, and appended %d new project path%s.\n"
                "(Total: %d paths saved in '%s')",
                existing_count, existing_count > 1 ? "s" : "",
                g_path_count, g_path_count > 1 ? "s" : "",
                existing_count + g_path_count,
                file_buf
            );
        }
    } else {
        if (existing_paths) {
            for (int i = 0; i < existing_count; ++i) {
                bezier_path_free(&existing_paths[i]);
            }
            free(existing_paths);
            existing_paths = NULL;
        }
        success = svg_path_export_to_file(file_buf, g_paths, g_path_count, cw, ch);
        if (success) {
            snprintf(msg, sizeof(msg),
                "Export complete.\n\n"
                "Successfully exported %d path%s to SVG with transparent canvas.\n\n"
                "File: %s",
                g_path_count, g_path_count > 1 ? "s" : "",
                file_buf
            );
        }
    }

    if (success) {
        show_themed_notice_dialog(
            hwnd,
            "Export Complete - ZuPather",
            "SVG Export Succeeded",
            msg,
            540, 270
        );
    } else {
        show_themed_notice_dialog(
            hwnd,
            "Export Failed - ZuPather",
            "File Write Error",
            "Failed to write SVG file.\n\nPlease check write permissions for the destination folder.",
            500, 250
        );
    }
}

void handle_export_svg_path(HWND hwnd) {
    handle_export_path(hwnd);
}


/* Canvas Relative Path & settings.xml Serialization Helpers                 */
/* ========================================================================= */
void get_canvas_base_directory(char *out_path, size_t max_len) {
    if (!out_path || max_len == 0) return;
    char exe_path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    char *p1 = strrchr(exe_path, '\\');
    char *p2 = strrchr(exe_path, '/');
    char *last_slash = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
    if (last_slash) {
        *(last_slash + 1) = '\0';
    }
    snprintf(out_path, max_len, "%scanvas", exe_path);
    CreateDirectoryA(out_path, NULL);
}

void compact_empty_paths(void) {
    int write_idx = 0;
    int old_active = g_active_path_idx;
    int new_active = -1;

    for (int p = 0; p < g_path_count; ++p) {
        if (g_paths[p].node_count > 0) {
            if (p == old_active) {
                new_active = write_idx;
            }
            if (write_idx != p) {
                g_paths[write_idx] = g_paths[p];
            }
            write_idx++;
        } else {
            bezier_path_free(&g_paths[p]);
        }
    }

    /* Zero out trailing cleared paths */
    for (int p = write_idx; p < g_path_count; ++p) {
        memset(&g_paths[p], 0, sizeof(BezierPath));
    }
    g_path_count = write_idx;

    if (new_active >= 0) {
        g_active_path_idx = new_active;
    } else if (g_path_count > 0) {
        g_active_path_idx = g_path_count - 1;
    } else {
        g_active_path_idx = -1;
    }

    if (g_active_path_idx >= 0 && g_active_path_idx < g_path_count) {
        if (g_active_node_idx >= g_paths[g_active_path_idx].node_count) {
            g_active_node_idx = g_paths[g_active_path_idx].node_count - 1;
        }
    } else {
        g_active_node_idx = -1;
    }

    /* Update path naming based on current compacted count */
    const char *base = g_canvas_name[0] ? g_canvas_name : "curve";
    if (g_path_count == 1) {
        snprintf(g_paths[0].name, sizeof(g_paths[0].name), "%s", base);
    } else {
        for (int p = 0; p < g_path_count; ++p) {
            snprintf(g_paths[p].name, sizeof(g_paths[p].name), "%s-%d", base, p + 1);
        }
    }
}

bool save_canvas_settings_xml(
    const char *folder_path,
    const char *canvas_name,
    const char *canvas_title,
    int canvas_w,
    int canvas_h,
    const char *img_filename,
    int img_w,
    int img_h,
    bool img_resized
) {
    char xml_path[MAX_PATH] = {0};
    snprintf(xml_path, sizeof(xml_path), "%s\\settings.xml", folder_path);

    FILE *f = fopen(xml_path, "w");
    if (!f) return false;

    const char *safe_name = (canvas_name && canvas_name[0]) ? canvas_name : "Untitled";
    const char *safe_title = (canvas_title && canvas_title[0]) ? canvas_title : safe_name;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<!--\n");
    fprintf(f, "    ZuPather Canvas Configuration\n");
    fprintf(f, "    CanvasName: %s\n", safe_name);
    fprintf(f, "    CanvasTitle: %s\n", safe_title);
    fprintf(f, "    CanvasWidth: %d\n", canvas_w);
    fprintf(f, "    CanvasHeight: %d\n", canvas_h);
    if (img_filename && img_filename[0]) {
        fprintf(f, "    BackgroundImage: %s\n", img_filename);
        fprintf(f, "    ImageWidth: %d\n", img_w);
        fprintf(f, "    ImageHeight: %d\n", img_h);
        fprintf(f, "    ImageResized: %s\n", img_resized ? "true" : "false");
    } else {
        fprintf(f, "    BackgroundImage: none\n");
        fprintf(f, "    ImageWidth: 0\n");
        fprintf(f, "    ImageHeight: 0\n");
        fprintf(f, "    ImageResized: false\n");
    }
    fprintf(f, "    FrogGX: %d\n", g_frog_gx);
    fprintf(f, "    FrogGY: %d\n", g_frog_gy);
    fprintf(f, "    TreasurePointCount: %d\n", g_treasure_point_count);
    fprintf(f, "    CutoutCount: %d\n", g_cutout_count);
    fprintf(f, "    SpriteCount: %d\n", g_sprite_count);
    fprintf(f, "    BackgroundAlphaCount: %d\n", g_bg_alpha_count);
    int actual_paths = (g_path_count > 0 && g_path_count <= 10) ? g_path_count : 1;
    char prog_id[64];
    get_level_progression_id(actual_paths, g_level_difficulty, prog_id, sizeof(prog_id));
    const char *diff_str = level_difficulty_to_string(g_level_difficulty);
    fprintf(f, "    Progression: %s\n", prog_id);
    fprintf(f, "    Difficulty: %s\n", diff_str);
    fprintf(f, "-->\n");
    fprintf(f, "<Levels>\n");

    /* Zuma Deluxe levels.xml uses image name without file extension */
    char base_img_name[128] = {0};
    if (img_filename && img_filename[0]) {
        strncpy(base_img_name, img_filename, sizeof(base_img_name) - 1);
        char *dot = strrchr(base_img_name, '.');
        if (dot) *dot = '\0';
    } else {
        strncpy(base_img_name, safe_name, sizeof(base_img_name) - 1);
    }

    fprintf(f, "    <Graphics id=\"%s\" image=\"%s\"", safe_name, base_img_name);
    if (g_path_count <= 1) {
        fprintf(f, " curve=\"%s\"", safe_name);
    } else {
        for (int p = 0; p < g_path_count; ++p) {
            char cname[128];
            snprintf(cname, sizeof(cname), "%s-%d", safe_name, p + 1);
            if (p == 0) {
                fprintf(f, " curve=\"%s\"", cname);
            } else {
                fprintf(f, " curve%d=\"%s\"", p + 1, cname);
            }
        }
    }
    fprintf(f, " dispname=\"%s\" gx=\"%d\" gy=\"%d\"", safe_title, g_frog_gx, g_frog_gy);
    if (g_map_has_skullrot) {
        fprintf(f, " skullrot=\"%.1f\"", g_map_skullrot);
    }
    if (g_map_is_space) {
        fprintf(f, " space=\"true\"");
    }
    if (g_dev_has_drawcurve) {
        fprintf(f, " drawcurve=\"%s\"", g_dev_drawcurve ? "true" : "false");
    }
    if (g_dev_has_drawtunnels) {
        fprintf(f, " drawtunnels=\"%s\"", g_dev_drawtunnels ? "true" : "false");
    }
    if (g_dev_has_curvecolor && g_dev_curvecolor[0]) {
        fprintf(f, " curvecolor=\"%s\"", g_dev_curvecolor);
    }
    if (g_dev_has_bgcolor && g_dev_bgcolor[0]) {
        fprintf(f, " bgcolor=\"%s\"", g_dev_bgcolor);
    }
    if (g_dev_has_huerot) {
        fprintf(f, " huerot=\"%.1f\"", g_dev_huerot);
    }
    fprintf(f, ">\n");
    fprintf(f, "        <!-- Zuma Deluxe Visual Elements (Child tags ordered: BackgroundAlpha -> Sprite -> Cutout -> TreasurePoint) -->\n");

    if (g_bg_alpha_count > 0) {
        for (int i = 0; i < g_bg_alpha_count; ++i) {
            char ba_name[64];
            if (g_bg_alphas[i].image_name[0]) {
                strncpy(ba_name, g_bg_alphas[i].image_name, sizeof(ba_name) - 1);
                ba_name[sizeof(ba_name) - 1] = '\0';
            } else if (g_bg_alpha_count == 1) {
                snprintf(ba_name, sizeof(ba_name), "quake");
            } else {
                snprintf(ba_name, sizeof(ba_name), "quake%d", i + 1);
            }
            /* In XML, the image attribute MUST NOT have a leading underscore */
            const char *xml_img = (ba_name[0] == '_') ? (ba_name + 1) : ba_name;
            fprintf(f, "        <BackgroundAlpha image=\"%s\" x=\"%d\" y=\"%d\" vx=\"%d\" vy=\"%d\" />\n",
                    xml_img, g_bg_alphas[i].x, g_bg_alphas[i].y, g_bg_alphas[i].vx, g_bg_alphas[i].vy);
        }
    } else {
        fprintf(f, "        <!-- <BackgroundAlpha image=\"water\" x=\"0\" y=\"0\" vx=\"0\" vy=\"1\" /> -->\n");
    }

    if (g_sprite_count > 0) {
        for (int i = 0; i < g_sprite_count; ++i) {
            char sp_name[64];
            if (g_sprites[i].image_name[0]) {
                strncpy(sp_name, g_sprites[i].image_name, sizeof(sp_name) - 1);
                sp_name[sizeof(sp_name) - 1] = '\0';
            } else if (g_sprite_count == 1) {
                snprintf(sp_name, sizeof(sp_name), "sprite");
            } else {
                snprintf(sp_name, sizeof(sp_name), "sprite%d", i + 1);
            }
            int pri = (g_sprites[i].pri >= 0 && g_sprites[i].pri <= 99) ? g_sprites[i].pri : 1;
            fprintf(f, "        <Sprite pri=\"%d\" image=\"%s\" x=\"%d\" y=\"%d\" />\n",
                    pri, sp_name, g_sprites[i].x, g_sprites[i].y);
        }
    } else {
        fprintf(f, "        <!-- <Sprite image=\"tree\" x=\"100\" y=\"200\" /> -->\n");
    }

    if (g_cutout_count > 0) {
        for (int i = 0; i < g_cutout_count; ++i) {
            char co_name[64];
            if (g_cutouts[i].image_name[0]) {
                strncpy(co_name, g_cutouts[i].image_name, sizeof(co_name) - 1);
                co_name[sizeof(co_name) - 1] = '\0';
            } else if (g_cutout_count == 1) {
                snprintf(co_name, sizeof(co_name), "tunnel");
            } else {
                snprintf(co_name, sizeof(co_name), "tunnel%d", i + 1);
            }
            int pri = (g_cutouts[i].pri >= 0 && g_cutouts[i].pri <= 99) ? g_cutouts[i].pri : 1;
            fprintf(f, "        <Cutout pri=\"%d\" image=\"%s\" x=\"%d\" y=\"%d\" />\n",
                    pri, co_name, g_cutouts[i].x, g_cutouts[i].y);
        }
    } else {
        fprintf(f, "        <!-- <Cutout pri=\"1\" image=\"tunnel\" x=\"0\" y=\"0\" /> -->\n");
    }

    if (g_treasure_point_count > 0) {
        for (int i = 0; i < g_treasure_point_count; ++i) {
            fprintf(f, "        <TreasurePoint x=\"%d\" y=\"%d\" dist1=\"%d\" />\n",
                    g_treasure_points[i].x, g_treasure_points[i].y,
                    g_treasure_points[i].dist1 > 0 ? g_treasure_points[i].dist1 : 35);
        }
    } else {
        fprintf(f, "        <!-- <TreasurePoint x=\"100\" y=\"100\" dist1=\"35\" /> -->\n");
    }
    fprintf(f, "    </Graphics>\n");
    fprintf(f, "</Levels>\n");

    fclose(f);
    return true;
}

/* Prune any obsolete / unregistered files ("file sampah") in folder_path that are not in settings.xml */
void prune_unregistered_canvas_files(const char *folder_path, const char *safe_name) {
    if (!folder_path || !folder_path[0]) return;

    char xml_path[MAX_PATH];
    snprintf(xml_path, sizeof(xml_path), "%s\\settings.xml", folder_path);

    FILE *f = fopen(xml_path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return;
    }

    char *xml_content = (char *)malloc(sz + 1);
    if (!xml_content) {
        fclose(f);
        return;
    }
    size_t rd = fread(xml_content, 1, sz, f);
    xml_content[rd] = '\0';
    fclose(f);

    #define MAX_WHITELIST_FILES 512
    char whitelist[MAX_WHITELIST_FILES][MAX_PATH];
    int whitelist_count = 0;

    /* 1. settings.xml is always kept */
    strncpy(whitelist[whitelist_count++], "settings.xml", MAX_PATH - 1);

    /* 2. [safe_name].zupath vector sidecar is strictly preserved! */
    if (safe_name && safe_name[0]) {
        char zup[MAX_PATH];
        snprintf(zup, sizeof(zup), "%s.zupath", safe_name);
        if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], zup, MAX_PATH - 1);
    }
    if (g_canvas_name[0]) {
        char zup2[MAX_PATH];
        snprintf(zup2, sizeof(zup2), "%s.zupath", g_canvas_name);
        if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], zup2, MAX_PATH - 1);
    }

    /* 3. BackgroundImage from header comment: "BackgroundImage: <filename>" */
    char *p_bg = strstr(xml_content, "BackgroundImage:");
    if (p_bg) {
        p_bg += 16;
        while (*p_bg == ' ' || *p_bg == '\t') p_bg++;
        char *eol = strpbrk(p_bg, "\r\n");
        if (eol) {
            char bg_name[MAX_PATH] = {0};
            size_t l = eol - p_bg;
            if (l >= sizeof(bg_name)) l = sizeof(bg_name) - 1;
            strncpy(bg_name, p_bg, l);
            bg_name[l] = '\0';
            if (_stricmp(bg_name, "none") != 0 && bg_name[0] && whitelist_count < MAX_WHITELIST_FILES) {
                strncpy(whitelist[whitelist_count++], bg_name, MAX_PATH - 1);
            }
        }
    }

    /* 4. Curves: scan for curve="<name>" and curve%d="<name>" */
    char *cur_ptr = xml_content;
    while ((cur_ptr = strstr(cur_ptr, "curve")) != NULL) {
        char *eq = strchr(cur_ptr, '=');
        char *q1 = eq ? strchr(eq, '"') : NULL;
        char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (eq && q1 && q2 && (q1 - eq <= 3)) {
            char cname[MAX_PATH];
            size_t l = q2 - (q1 + 1);
            if (l >= sizeof(cname)) l = sizeof(cname) - 1;
            strncpy(cname, q1 + 1, l);
            cname[l] = '\0';
            char dat_file[MAX_PATH];
            snprintf(dat_file, sizeof(dat_file), "%s.dat", cname);
            if (whitelist_count < MAX_WHITELIST_FILES) {
                strncpy(whitelist[whitelist_count++], dat_file, MAX_PATH - 1);
            }
            cur_ptr = q2 + 1;
        } else {
            cur_ptr += 5;
        }
    }

    const char *img_exts[] = {"png", "jpg", "jpeg", "bmp", "webp", "gif"};

    /* 5. Cutout tags: <Cutout ... image="<name>" ... />
          On disk: _<name>.<ext> or <name>.<ext> */
    char *co_ptr = xml_content;
    while ((co_ptr = strstr(co_ptr, "<Cutout")) != NULL) {
        char *tag_end = strchr(co_ptr, '>');
        if (!tag_end) break;
        char *img_attr = strstr(co_ptr, "image=\"");
        if (img_attr && img_attr < tag_end) {
            char *q1 = img_attr + 7;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 < tag_end) {
                char iname[64];
                size_t l = q2 - q1;
                if (l >= sizeof(iname)) l = sizeof(iname) - 1;
                strncpy(iname, q1, l);
                iname[l] = '\0';
                const char *clean = (iname[0] == '_') ? (iname + 1) : iname;

                for (int e = 0; e < 6; ++e) {
                    char fn1[MAX_PATH], fn2[MAX_PATH];
                    snprintf(fn1, sizeof(fn1), "_%s.%s", clean, img_exts[e]);
                    snprintf(fn2, sizeof(fn2), "%s.%s", clean, img_exts[e]);
                    if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], fn1, MAX_PATH - 1);
                    if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], fn2, MAX_PATH - 1);
                }
            }
        }
        co_ptr = tag_end + 1;
    }

    /* 6. Sprite tags: <Sprite ... image="<name>" ... />
          On disk: <name>.<ext> */
    char *sp_ptr = xml_content;
    while ((sp_ptr = strstr(sp_ptr, "<Sprite")) != NULL) {
        char *tag_end = strchr(sp_ptr, '>');
        if (!tag_end) break;
        char *img_attr = strstr(sp_ptr, "image=\"");
        if (img_attr && img_attr < tag_end) {
            char *q1 = img_attr + 7;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 < tag_end) {
                char iname[64];
                size_t l = q2 - q1;
                if (l >= sizeof(iname)) l = sizeof(iname) - 1;
                strncpy(iname, q1, l);
                iname[l] = '\0';
                const char *clean = (iname[0] == '_') ? (iname + 1) : iname;

                for (int e = 0; e < 6; ++e) {
                    char fn[MAX_PATH];
                    snprintf(fn, sizeof(fn), "%s.%s", clean, img_exts[e]);
                    if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], fn, MAX_PATH - 1);
                }
            }
        }
        sp_ptr = tag_end + 1;
    }

    /* 7. BackgroundAlpha tags: <BackgroundAlpha ... image="<name>" ... />
          CRITICAL: On disk it MUST have leading underscore _<name>.<ext>!
          E.g. <BackgroundAlpha image="quake" /> -> whitelist: _quake.png, _quake.jpg, etc.
          Also whitelist quake.png just in case. */
    char *ba_ptr = xml_content;
    while ((ba_ptr = strstr(ba_ptr, "<BackgroundAlpha")) != NULL) {
        char *tag_end = strchr(ba_ptr, '>');
        if (!tag_end) break;
        char *img_attr = strstr(ba_ptr, "image=\"");
        if (img_attr && img_attr < tag_end) {
            char *q1 = img_attr + 7;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 < tag_end) {
                char iname[64];
                size_t l = q2 - q1;
                if (l >= sizeof(iname)) l = sizeof(iname) - 1;
                strncpy(iname, q1, l);
                iname[l] = '\0';
                const char *clean = (iname[0] == '_') ? (iname + 1) : iname;

                for (int e = 0; e < 6; ++e) {
                    char fn1[MAX_PATH], fn2[MAX_PATH];
                    snprintf(fn1, sizeof(fn1), "_%s.%s", clean, img_exts[e]);
                    snprintf(fn2, sizeof(fn2), "%s.%s", clean, img_exts[e]);
                    if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], fn1, MAX_PATH - 1);
                    if (whitelist_count < MAX_WHITELIST_FILES) strncpy(whitelist[whitelist_count++], fn2, MAX_PATH - 1);
                }
            }
        }
        ba_ptr = tag_end + 1;
    }

    free(xml_content);

    /* Enumerate files in folder_path and remove any file not present in whitelist */
    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.*", folder_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue; /* Never delete folders */
            }

            /* Extra protection: Never delete any .zupath sidecar! */
            const char *dot = strrchr(fd.cFileName, '.');
            if (dot && _stricmp(dot, ".zupath") == 0) {
                continue;
            }

            bool is_whitelisted = false;
            for (int r = 0; r < whitelist_count; ++r) {
                if (_stricmp(fd.cFileName, whitelist[r]) == 0) {
                    is_whitelisted = true;
                    break;
                }
            }

            if (!is_whitelisted) {
                char del_path[MAX_PATH];
                snprintf(del_path, sizeof(del_path), "%s\\%s", folder_path, fd.cFileName);
                DeleteFileA(del_path);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

bool load_canvas_settings_xml(
    const char *folder_path,
    char *out_name, size_t max_name,
    char *out_title, size_t max_title,
    int *out_w, int *out_h,
    char *out_img_filename, size_t max_img,
    int *out_img_w, int *out_img_h,
    bool *out_img_resized,
    int *out_gx, int *out_gy,
    TreasurePoint *out_coins, int *out_coin_count, int max_coins,
    LevelCutout *out_cutouts, int *out_cutout_count, int max_cutouts,
    LevelSprite *out_sprites, int *out_sprite_count, int max_sprites,
    LevelBackgroundAlpha *out_bgalphas, int *out_bgalpha_count, int max_bgalphas
) {
    char xml_path[MAX_PATH] = {0};
    snprintf(xml_path, sizeof(xml_path), "%s\\settings.xml", folder_path);

    FILE *f = fopen(xml_path, "r");
    if (!f) return false;

    if (out_name) out_name[0] = '\0';
    if (out_title) out_title[0] = '\0';
    if (out_img_filename) out_img_filename[0] = '\0';
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (out_img_w) *out_img_w = 0;
    if (out_img_h) *out_img_h = 0;
    if (out_img_resized) *out_img_resized = false;
    if (out_gx) *out_gx = -1;
    if (out_gy) *out_gy = -1;
    if (out_coin_count) *out_coin_count = 0;
    if (out_cutout_count) *out_cutout_count = 0;
    if (out_sprite_count) *out_sprite_count = 0;
    if (out_bgalpha_count) *out_bgalpha_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        char *nl = strchr(p, '\r');
        if (nl) *nl = '\0';
        nl = strchr(p, '\n');
        if (nl) *nl = '\0';

        if (strncmp(p, "CanvasName:", 11) == 0) {
            char *val = p + 11;
            while (*val == ' ' || *val == '\t') val++;
            if (out_name && max_name > 0) {
                strncpy(out_name, val, max_name - 1);
                out_name[max_name - 1] = '\0';
            }
        } else if (strncmp(p, "CanvasTitle:", 12) == 0) {
            char *val = p + 12;
            while (*val == ' ' || *val == '\t') val++;
            if (out_title && max_title > 0) {
                strncpy(out_title, val, max_title - 1);
                out_title[max_title - 1] = '\0';
            }
        } else if (strncmp(p, "CanvasWidth:", 12) == 0) {
            char *val = p + 12;
            if (out_w) *out_w = atoi(val);
        } else if (strncmp(p, "CanvasHeight:", 13) == 0) {
            char *val = p + 13;
            if (out_h) *out_h = atoi(val);
        } else if (strncmp(p, "BackgroundImage:", 16) == 0) {
            char *val = p + 16;
            while (*val == ' ' || *val == '\t') val++;
            if (_stricmp(val, "none") != 0 && out_img_filename && max_img > 0) {
                strncpy(out_img_filename, val, max_img - 1);
                out_img_filename[max_img - 1] = '\0';
            }
        } else if (strncmp(p, "ImageWidth:", 11) == 0) {
            char *val = p + 11;
            if (out_img_w) *out_img_w = atoi(val);
        } else if (strncmp(p, "ImageHeight:", 12) == 0) {
            char *val = p + 12;
            if (out_img_h) *out_img_h = atoi(val);
        } else if (strncmp(p, "ImageResized:", 13) == 0) {
            char *val = p + 13;
            while (*val == ' ' || *val == '\t') val++;
            if (out_img_resized) *out_img_resized = (_stricmp(val, "true") == 0);
        } else if (strncmp(p, "FrogGX:", 7) == 0) {
            char *val = p + 7;
            if (out_gx) *out_gx = atoi(val);
        } else if (strncmp(p, "FrogGY:", 7) == 0) {
            char *val = p + 7;
            if (out_gy) *out_gy = atoi(val);
        } else if (strncmp(p, "Difficulty:", 11) == 0) {
            char *val = p + 11;
            while (*val == ' ' || *val == '\t') val++;
            g_level_difficulty = level_difficulty_from_string(val);
        } else if (strncmp(p, "Progression:", 12) == 0) {
            char *val = p + 12;
            while (*val == ' ' || *val == '\t') val++;
            char *dash = strchr(val, '-');
            if (dash) {
                g_level_difficulty = level_difficulty_from_string(dash + 1);
            }
        }

        /* Zuma Deluxe Graphics Tag Detection */
        char *gfx_tag = strstr(p, "<Graphics");
        if (gfx_tag) {
            if (out_name && out_name[0] == '\0') {
                char *id_attr = strstr(gfx_tag, "id=\"");
                if (id_attr) {
                    char *quote = strchr(id_attr + 4, '"');
                    if (quote) {
                        size_t len = quote - (id_attr + 4);
                        if (len >= max_name) len = max_name - 1;
                        strncpy(out_name, id_attr + 4, len);
                        out_name[len] = '\0';
                    }
                }
            }
            if (out_title && out_title[0] == '\0') {
                char *disp_attr = strstr(gfx_tag, "dispname=\"");
                if (disp_attr) {
                    char *quote = strchr(disp_attr + 10, '"');
                    if (quote) {
                        size_t len = quote - (disp_attr + 10);
                        if (len >= max_title) len = max_title - 1;
                        strncpy(out_title, disp_attr + 10, len);
                        out_title[len] = '\0';
                    }
                }
            }
            if (out_img_filename && out_img_filename[0] == '\0') {
                char *img_attr = strstr(gfx_tag, "image=\"");
                if (img_attr) {
                    char *quote = strchr(img_attr + 7, '"');
                    if (quote) {
                        size_t len = quote - (img_attr + 7);
                        if (len >= max_img) len = max_img - 1;
                        strncpy(out_img_filename, img_attr + 7, len);
                        out_img_filename[len] = '\0';
                    }
                }
            }
            if (out_gx && *out_gx < 0) {
                char *gx_attr = strstr(gfx_tag, "gx=\"");
                if (gx_attr) {
                    *out_gx = atoi(gx_attr + 4);
                }
            }
            if (out_gy && *out_gy < 0) {
                char *gy_attr = strstr(gfx_tag, "gy=\"");
                if (gy_attr) {
                    *out_gy = atoi(gy_attr + 4);
                }
            }

            /* Parse Map & Developer Attributes (only when opening canvas into active editor) */
            if (out_coins != NULL || out_cutouts != NULL || out_sprites != NULL || out_bgalphas != NULL) {
                reset_map_and_dev_attributes();

                char *sk_attr = strstr(gfx_tag, "skullrot=\"");
                if (sk_attr) {
                    g_map_has_skullrot = true;
                    g_map_skullrot = (float)atof(sk_attr + 10);
                }
                char *sp_attr = strstr(gfx_tag, "space=\"");
                if (sp_attr && strncmp(sp_attr + 7, "true", 4) == 0) {
                    g_map_is_space = true;
                }
                char *dc_attr = strstr(gfx_tag, "drawcurve=\"");
                if (dc_attr) {
                    g_dev_has_drawcurve = true;
                    g_dev_drawcurve = (strncmp(dc_attr + 11, "false", 5) != 0);
                }
                char *dt_attr = strstr(gfx_tag, "drawtunnels=\"");
                if (dt_attr) {
                    g_dev_has_drawtunnels = true;
                    g_dev_drawtunnels = (strncmp(dt_attr + 13, "false", 5) != 0);
                }
                char *cc_attr = strstr(gfx_tag, "curvecolor=\"");
                if (cc_attr) {
                    char *q = strchr(cc_attr + 12, '"');
                    if (q) {
                        size_t l = q - (cc_attr + 12);
                        if (l >= sizeof(g_dev_curvecolor)) l = sizeof(g_dev_curvecolor) - 1;
                        strncpy(g_dev_curvecolor, cc_attr + 12, l);
                        g_dev_curvecolor[l] = '\0';
                        g_dev_has_curvecolor = true;
                    }
                }
                char *bg_attr = strstr(gfx_tag, "bgcolor=\"");
                if (bg_attr) {
                    char *q = strchr(bg_attr + 9, '"');
                    if (q) {
                        size_t l = q - (bg_attr + 9);
                        if (l >= sizeof(g_dev_bgcolor)) l = sizeof(g_dev_bgcolor) - 1;
                        strncpy(g_dev_bgcolor, bg_attr + 9, l);
                        g_dev_bgcolor[l] = '\0';
                        g_dev_has_bgcolor = true;
                    }
                }
                char *da_attr = strstr(gfx_tag, "destroyall=\"");
                if (da_attr) {
                    g_dev_has_destroyall = true;
                    g_dev_destroyall = (strncmp(da_attr + 12, "false", 5) != 0);
                }
                char *hr_attr = strstr(gfx_tag, "huerot=\"");
                if (hr_attr) {
                    g_dev_has_huerot = true;
                    g_dev_huerot = (float)atof(hr_attr + 8);
                }
                char *tf_attr = strstr(gfx_tag, "treasurefreq=\"");
                if (tf_attr) {
                    g_dev_has_treasurefreq = true;
                    g_dev_treasurefreq = atoi(tf_attr + 14);
                }
                char *pt_attr = strstr(gfx_tag, "partime=\"");
                if (pt_attr) {
                    g_dev_has_partime = true;
                    g_dev_partime = atoi(pt_attr + 9);
                }
            }
        }

        /* Zuma Deluxe BackgroundAlpha tag detection */
        char *ba_tag = strstr(p, "<BackgroundAlpha");
        if (ba_tag && !strstr(p, "<!--")) {
            char *x_attr = strstr(ba_tag, "x=\"");
            char *y_attr = strstr(ba_tag, "y=\"");
            char *vx_attr = strstr(ba_tag, "vx=\"");
            char *vy_attr = strstr(ba_tag, "vy=\"");
            char *p_attr = strstr(ba_tag, "pri=\"");
            char *img_attr = strstr(ba_tag, "image=\"");
            if (x_attr && y_attr) {
                int bx = atoi(x_attr + 3);
                int by = atoi(y_attr + 3);
                int bvx = vx_attr ? atoi(vx_attr + 4) : 0;
                int bvy = vy_attr ? atoi(vy_attr + 4) : 0;
                int bpri = p_attr ? atoi(p_attr + 5) : 1;
                char bimg[64] = "quake";
                if (img_attr) {
                    char *q = strchr(img_attr + 7, '"');
                    if (q) {
                        const char *src = img_attr + 7;
                        if (*src == '_') src++;
                        size_t l = q - src;
                        if (l >= sizeof(bimg)) l = sizeof(bimg) - 1;
                        strncpy(bimg, src, l);
                        bimg[l] = '\0';
                    }
                }
                if (out_bgalphas && out_bgalpha_count && *out_bgalpha_count < max_bgalphas) {
                    LevelBackgroundAlpha *ba = &out_bgalphas[*out_bgalpha_count];
                    memset(ba, 0, sizeof(LevelBackgroundAlpha));
                    ba->x = bx;
                    ba->y = by;
                    ba->vx = bvx;
                    ba->vy = bvy;
                    ba->pri = (bpri >= 0 && bpri <= 99) ? bpri : 1;
                    strncpy(ba->image_name, bimg, sizeof(ba->image_name) - 1);
                }
                if (out_bgalpha_count) {
                    (*out_bgalpha_count)++;
                }
            }
        }

        /* Zuma Deluxe Cutout tag detection */
        char *co_tag = strstr(p, "<Cutout");
        if (co_tag && !strstr(p, "<!--")) {
            char *x_attr = strstr(co_tag, "x=\"");
            char *y_attr = strstr(co_tag, "y=\"");
            char *p_attr = strstr(co_tag, "pri=\"");
            char *img_attr = strstr(co_tag, "image=\"");
            if (x_attr && y_attr) {
                int cx = atoi(x_attr + 3);
                int cy = atoi(y_attr + 3);
                int cpri = p_attr ? atoi(p_attr + 5) : 1;
                char cimg[64] = "tunnel";
                if (img_attr) {
                    char *q = strchr(img_attr + 7, '"');
                    if (q) {
                        size_t l = q - (img_attr + 7);
                        if (l >= sizeof(cimg)) l = sizeof(cimg) - 1;
                        strncpy(cimg, img_attr + 7, l);
                        cimg[l] = '\0';
                    }
                }
                if (out_cutouts && out_cutout_count && *out_cutout_count < max_cutouts) {
                    LevelCutout *co = &out_cutouts[*out_cutout_count];
                    memset(co, 0, sizeof(LevelCutout));
                    co->x = cx;
                    co->y = cy;
                    co->pri = (cpri >= 0 && cpri <= 99) ? cpri : 1;
                    strncpy(co->image_name, cimg, sizeof(co->image_name) - 1);
                }
                if (out_cutout_count) {
                    (*out_cutout_count)++;
                }
            }
        }

        /* Zuma Deluxe Sprite tag detection */
        char *sp_tag = strstr(p, "<Sprite");
        if (sp_tag && !strstr(p, "<!--")) {
            char *x_attr = strstr(sp_tag, "x=\"");
            char *y_attr = strstr(sp_tag, "y=\"");
            char *p_attr = strstr(sp_tag, "pri=\"");
            char *img_attr = strstr(sp_tag, "image=\"");
            if (x_attr && y_attr) {
                int sx = atoi(x_attr + 3);
                int sy = atoi(y_attr + 3);
                int spri = p_attr ? atoi(p_attr + 5) : 1;
                char simg[64] = "sprite";
                if (img_attr) {
                    char *q = strchr(img_attr + 7, '"');
                    if (q) {
                        size_t l = q - (img_attr + 7);
                        if (l >= sizeof(simg)) l = sizeof(simg) - 1;
                        strncpy(simg, img_attr + 7, l);
                        simg[l] = '\0';
                    }
                }
                if (out_sprites && out_sprite_count && *out_sprite_count < max_sprites) {
                    LevelSprite *sp = &out_sprites[*out_sprite_count];
                    memset(sp, 0, sizeof(LevelSprite));
                    sp->x = sx;
                    sp->y = sy;
                    sp->pri = (spri >= 0 && spri <= 99) ? spri : 1;
                    strncpy(sp->image_name, simg, sizeof(sp->image_name) - 1);
                }
                if (out_sprite_count) {
                    (*out_sprite_count)++;
                }
            }
        }

        /* Zuma Deluxe TreasurePoint tag detection */
        char *tp_tag = strstr(p, "<TreasurePoint");
        if (tp_tag && !strstr(p, "<!--")) {
            char *x_attr = strstr(tp_tag, "x=\"");
            char *y_attr = strstr(tp_tag, "y=\"");
            char *d_attr = strstr(tp_tag, "dist1=\"");
            if (!d_attr) d_attr = strstr(tp_tag, "dist2=\"");
            if (x_attr && y_attr) {
                int cx = atoi(x_attr + 3);
                int cy = atoi(y_attr + 3);
                int cd = d_attr ? atoi(d_attr + 7) : 35;
                if (out_coins && out_coin_count && *out_coin_count < max_coins) {
                    out_coins[*out_coin_count].x = cx;
                    out_coins[*out_coin_count].y = cy;
                    out_coins[*out_coin_count].dist1 = cd;
                }
                if (out_coin_count) {
                    (*out_coin_count)++;
                }
            }
        }

        /* Fallback legacy XML tag detection */
        char *lvl_tag = strstr(p, "<Level id=\"");
        if (lvl_tag && out_name && out_name[0] == '\0') {
            char *quote = strchr(lvl_tag + 11, '"');
            if (quote) {
                size_t len = quote - (lvl_tag + 11);
                if (len >= max_name) len = max_name - 1;
                strncpy(out_name, lvl_tag + 11, len);
                out_name[len] = '\0';
            }
        }
        char *frog_tag = strstr(p, "<Frog");
        if (frog_tag && !strstr(p, "<!--")) {
            char *x_attr = strstr(frog_tag, "x=\"");
            if (x_attr && out_gx && *out_gx < 0) *out_gx = atoi(x_attr + 3);
            char *y_attr = strstr(frog_tag, "y=\"");
            if (y_attr && out_gy && *out_gy < 0) *out_gy = atoi(y_attr + 3);
        }
        char *lvl_prog = strstr(p, "<Level ");
        if (lvl_prog && !strstr(p, "<!--")) {
            char *prog_attr = strstr(lvl_prog, "progression=\"");
            if (prog_attr) {
                char *q = strchr(prog_attr + 13, '"');
                if (q) {
                    char prog_val[64];
                    size_t l = q - (prog_attr + 13);
                    if (l >= sizeof(prog_val)) l = sizeof(prog_val) - 1;
                    strncpy(prog_val, prog_attr + 13, l);
                    prog_val[l] = '\0';
                    char *dash = strchr(prog_val, '-');
                    if (dash) {
                        g_level_difficulty = level_difficulty_from_string(dash + 1);
                    }
                }
            }
        }
    }
    if (out_title && out_title[0] == '\0' && out_name && out_name[0] != '\0') {
        strncpy(out_title, out_name, max_title - 1);
        out_title[max_title - 1] = '\0';
    }
    fclose(f);
    return true;
}

bool find_image_in_folder(const char *folder_path, const char *preferred_name, char *out_filename, size_t max_len) {
    if (!folder_path || !out_filename || max_len == 0) return false;
    out_filename[0] = '\0';

    const char *exts[] = {"png", "jpg", "jpeg", "gif", "webp", "bmp"};
    char check_path[MAX_PATH];

    if (preferred_name && preferred_name[0]) {
        for (int i = 0; i < 6; ++i) {
            snprintf(check_path, sizeof(check_path), "%s\\%s.%s", folder_path, preferred_name, exts[i]);
            if (GetFileAttributesA(check_path) != INVALID_FILE_ATTRIBUTES) {
                snprintf(out_filename, max_len, "%s.%s", preferred_name, exts[i]);
                return true;
            }
        }
    }

    char search_pattern[MAX_PATH];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\*.*", folder_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char ext[16] = {0};
                image_get_extension(fd.cFileName, ext, sizeof(ext));
                for (int i = 0; i < 5; ++i) {
                    if (_stricmp(ext, exts[i]) == 0) {
                        strncpy(out_filename, fd.cFileName, max_len - 1);
                        out_filename[max_len - 1] = '\0';
                        FindClose(hFind);
                        return true;
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return false;
}

bool execute_open_canvas_folder(HWND hwnd, const char *folder_path) {
    if (!folder_path || folder_path[0] == '\0') return false;

    update_open_progress("Parsing canvas settings.xml...", 8);

    char canvas_name[128] = {0};
    char canvas_title[128] = {0};
    int canvas_w = 0, canvas_h = 0;
    char img_filename[MAX_PATH] = {0};
    int img_w = 0, img_h = 0;
    bool img_resized = false;
    int frog_gx = -1, frog_gy = -1;

    TreasurePoint loaded_coins[MAX_TREASURE_POINTS];
    int loaded_coin_count = 0;
    LevelCutout loaded_cutouts[MAX_CUTOUTS];
    int loaded_cutout_count = 0;
    LevelSprite loaded_sprites[MAX_SPRITES];
    int loaded_sprite_count = 0;
    LevelBackgroundAlpha loaded_bgalphas[MAX_BG_ALPHAS];
    int loaded_bgalpha_count = 0;

    load_canvas_settings_xml(
        folder_path, canvas_name, sizeof(canvas_name),
        canvas_title, sizeof(canvas_title),
        &canvas_w, &canvas_h, img_filename, sizeof(img_filename),
        &img_w, &img_h, &img_resized,
        &frog_gx, &frog_gy,
        loaded_coins, &loaded_coin_count, MAX_TREASURE_POINTS,
        loaded_cutouts, &loaded_cutout_count, MAX_CUTOUTS,
        loaded_sprites, &loaded_sprite_count, MAX_SPRITES,
        loaded_bgalphas, &loaded_bgalpha_count, MAX_BG_ALPHAS
    );

    if (canvas_name[0] == '\0') {
        const char *p1 = strrchr(folder_path, '\\');
        const char *p2 = strrchr(folder_path, '/');
        const char *name_start = (p2 && (!p1 || p2 > p1)) ? (p2 + 1) : (p1 ? (p1 + 1) : folder_path);
        strncpy(canvas_name, name_start, sizeof(canvas_name) - 1);
    }

    update_open_progress("Locating background media...", 20);

    if (img_filename[0] == '\0') {
        find_image_in_folder(folder_path, canvas_name, img_filename, sizeof(img_filename));
    }

    if (g_has_bg_image) {
        image_free(&g_canvas_img);
        g_has_bg_image = false;
    }

    if (img_filename[0] != '\0') {
        char full_img_path[MAX_PATH];
        snprintf(full_img_path, sizeof(full_img_path), "%s\\%s", folder_path, img_filename);
        char pmsg[256];
        snprintf(pmsg, sizeof(pmsg), "Loading background media (%s)...", img_filename);
        update_open_progress(pmsg, 32);
        if (image_load_from_file(full_img_path, &g_canvas_img)) {
            g_has_bg_image = true;
            if (canvas_w <= 0) canvas_w = g_canvas_img.width;
            if (canvas_h <= 0) canvas_h = g_canvas_img.height;
        }
    }

    if (canvas_w <= 0) canvas_w = DEFAULT_CANVAS_WIDTH;
    if (canvas_h <= 0) canvas_h = DEFAULT_CANVAS_HEIGHT;

    if (frog_gx >= 0 && frog_gy >= 0) {
        g_frog_gx = frog_gx;
        g_frog_gy = frog_gy;
    } else {
        g_frog_gx = canvas_w / 2;
        g_frog_gy = canvas_h / 2;
    }

    update_open_progress("Loading frog and coin anchors...", 48);
    g_has_frog = true;
    g_placing_frog = false;
    g_frog_hovered = false;
    load_frog_sprite();

    g_treasure_point_count = loaded_coin_count;
    for (int i = 0; i < loaded_coin_count; ++i) {
        g_treasure_points[i] = loaded_coins[i];
    }
    g_placing_coin = false;
    g_hovered_coin_idx = -1;
    load_coin_sprite();

    /* Clear and populate cutouts */
    for (int i = 0; i < g_cutout_count; ++i) {
        free_cutout(&g_cutouts[i]);
    }
    g_cutout_count = 0;
    g_placing_cutout = false;
    g_hovered_cutout_idx = -1;

    update_open_progress("Loading cutout masks & overlay layers...", 60);
    for (int i = 0; i < loaded_cutout_count && i < MAX_CUTOUTS; ++i) {
        g_cutouts[i] = loaded_cutouts[i];
        if (resolve_cutout_image_in_folder(folder_path, g_cutouts[i].image_name, &g_cutouts[i].img)) {
            g_cutouts[i].has_img = true;
            strncpy(g_cutouts[i].source_filepath, g_cutouts[i].img.filepath, sizeof(g_cutouts[i].source_filepath) - 1);
        }
        cutout_generate_masks(&g_cutouts[i]);
        g_cutout_count++;
    }
    sync_cutout_image_names();
    g_selected_cutout_idx = 0;

    /* Clear and populate sprites */
    for (int i = 0; i < g_sprite_count; ++i) {
        free_sprite(&g_sprites[i]);
    }
    g_sprite_count = 0;
    g_placing_sprite = false;
    g_hovered_sprite_idx = -1;

    update_open_progress("Loading sprite overlay assets...", 72);
    for (int i = 0; i < loaded_sprite_count && i < MAX_SPRITES; ++i) {
        g_sprites[i] = loaded_sprites[i];
        if (resolve_sprite_image_in_folder(folder_path, g_sprites[i].image_name, &g_sprites[i].img)) {
            g_sprites[i].has_img = true;
            strncpy(g_sprites[i].source_filepath, g_sprites[i].img.filepath, sizeof(g_sprites[i].source_filepath) - 1);
        }
        g_sprite_count++;
    }
    sync_sprite_image_names();
    g_selected_sprite_idx = 0;

    /* Clear and populate bg_alphas */
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        free_bgalpha(&g_bg_alphas[i]);
    }
    g_bg_alpha_count = 0;
    g_placing_bgalpha = false;
    g_placing_bgalpha_vector = false;
    g_hovered_bgalpha_idx = -1;

    update_open_progress("Loading background alpha masks...", 82);
    for (int i = 0; i < loaded_bgalpha_count && i < MAX_BG_ALPHAS; ++i) {
        g_bg_alphas[i] = loaded_bgalphas[i];
        if (resolve_bgalpha_image_in_folder(folder_path, g_bg_alphas[i].image_name, &g_bg_alphas[i].img)) {
            g_bg_alphas[i].has_img = true;
            strncpy(g_bg_alphas[i].source_filepath, g_bg_alphas[i].img.filepath, sizeof(g_bg_alphas[i].source_filepath) - 1);
        }
        bgalpha_generate_masks(&g_bg_alphas[i]);
        g_bg_alpha_count++;
    }
    sync_bgalpha_image_names();
    g_selected_bgalpha_idx = 0;

    strncpy(g_canvas_folder, folder_path, sizeof(g_canvas_folder) - 1);

    /* Clear and load vector curve paths */
    update_open_progress("Loading vector curve paths (.zupath / .dat)...", 92);
    for (int p = 0; p < g_path_count; ++p) {
        bezier_path_free(&g_paths[p]);
    }
    g_path_count = 0;
    g_active_path_idx = -1;

    char zupath_file[MAX_PATH];
    snprintf(zupath_file, sizeof(zupath_file), "%s\\%s.zupath", folder_path, canvas_name);
    if (GetFileAttributesA(zupath_file) == INVALID_FILE_ATTRIBUTES) {
        /* Search for any .zupath in folder */
        char search_zu[MAX_PATH];
        snprintf(search_zu, sizeof(search_zu), "%s\\*.zupath", folder_path);
        WIN32_FIND_DATAA fd_zu;
        HANDLE hFindZu = FindFirstFileA(search_zu, &fd_zu);
        if (hFindZu != INVALID_HANDLE_VALUE) {
            snprintf(zupath_file, sizeof(zupath_file), "%s\\%s", folder_path, fd_zu.cFileName);
            FindClose(hFindZu);
        }
    }

    if (GetFileAttributesA(zupath_file) != INVALID_FILE_ATTRIBUTES) {
        BezierPath *loaded_p = NULL;
        int loaded_c = 0;
        if (bezier_path_load_zupath(&loaded_p, &loaded_c, zupath_file) && loaded_c > 0) {
            for (int i = 0; i < loaded_c && i < MAX_PATHS; ++i) {
                g_paths[i] = loaded_p[i];
                g_path_count++;
            }
            g_active_path_idx = 0;
            free(loaded_p);
        }
    } else {
        /* Fallback: try loading .dat binary files */
        int loaded_dats = 0;
        /* A. Check multi-track files: [canvas]-1.dat, [canvas]-2.dat, ... */
        for (int k = 1; k <= MAX_PATHS; ++k) {
            char multi_dat[MAX_PATH];
            snprintf(multi_dat, sizeof(multi_dat), "%s\\%s-%d.dat", folder_path, canvas_name, k);
            if (GetFileAttributesA(multi_dat) != INVALID_FILE_ATTRIBUTES) {
                char path_name[64];
                snprintf(path_name, sizeof(path_name), "%s-%d", canvas_name, k);
                bezier_path_init(&g_paths[loaded_dats], path_name);
                if (bezier_path_import_dat(&g_paths[loaded_dats], multi_dat, 0.0f)) {
                    loaded_dats++;
                } else {
                    bezier_path_free(&g_paths[loaded_dats]);
                    break;
                }
            } else {
                break;
            }
        }

        if (loaded_dats > 0) {
            g_path_count = loaded_dats;
            g_active_path_idx = 0;
        } else {
            /* B. Check single-track file: [canvas].dat */
            char single_dat[MAX_PATH];
            snprintf(single_dat, sizeof(single_dat), "%s\\%s.dat", folder_path, canvas_name);
            if (GetFileAttributesA(single_dat) != INVALID_FILE_ATTRIBUTES) {
                bezier_path_init(&g_paths[0], canvas_name);
                if (bezier_path_import_dat(&g_paths[0], single_dat, 0.0f)) {
                    g_path_count = 1;
                    g_active_path_idx = 0;
                } else {
                    bezier_path_free(&g_paths[0]);
                }
            } else {
                /* C. Search for any *.dat in the folder */
                char search_dat[MAX_PATH];
                snprintf(search_dat, sizeof(search_dat), "%s\\*.dat", folder_path);
                WIN32_FIND_DATAA fd_dat;
                HANDLE hFindDat = FindFirstFileA(search_dat, &fd_dat);
                if (hFindDat != INVALID_HANDLE_VALUE) {
                    char fallback_dat[MAX_PATH];
                    snprintf(fallback_dat, sizeof(fallback_dat), "%s\\%s", folder_path, fd_dat.cFileName);
                    FindClose(hFindDat);
                    bezier_path_init(&g_paths[0], canvas_name);
                    if (bezier_path_import_dat(&g_paths[0], fallback_dat, 0.0f)) {
                        g_path_count = 1;
                        g_active_path_idx = 0;
                    } else {
                        bezier_path_free(&g_paths[0]);
                    }
                }
            }
        }
    }

    for (int p = 0; p < g_path_count; ++p) {
        path_cache_invalidate(&g_paths[p]);
        path_cache_update(&g_paths[p]);
    }

    update_open_progress("Finalizing canvas configuration...", 98);
    g_has_canvas = true;
    strncpy(g_canvas_name, canvas_name, sizeof(g_canvas_name) - 1);
    sync_canvas_title_everywhere((canvas_title[0] != '\0') ? canvas_title : canvas_name, NULL);
    set_canvas_size(hwnd, canvas_w, canvas_h);
    sync_all_map_attr_edits_from_state();

    zoom_to_fit(hwnd);
    update_status_bar();
    InvalidateRect(hwnd, NULL, FALSE);
    update_open_progress("Canvas Project loaded successfully!", 100);
    return true;
}

static const char *get_ext_for_format(ImageExportFormat fmt, const char *orig_ext) {
    if (fmt == IMG_EXPORT_WEBP) return "webp";
    if (fmt == IMG_EXPORT_PNG)  return "png";
    if (fmt == IMG_EXPORT_JPG)  return "jpg";
    if (fmt == IMG_EXPORT_GIF)  return "gif";
    if (fmt == IMG_EXPORT_BMP)  return "bmp";
    if (orig_ext && orig_ext[0]) return orig_ext;
    return "png";
}

static void cleanup_alternate_ext_files(const char *folder_path, const char *prefix, const char *name, const char *current_ext) {
    const char *common_exts[] = {"png", "webp", "jpg", "jpeg", "gif", "bmp"};
    for (int e = 0; e < 6; ++e) {
        if (_stricmp(common_exts[e], current_ext) != 0) {
            char old_file[MAX_PATH];
            snprintf(old_file, sizeof(old_file), "%s\\%s%s.%s", folder_path, prefix ? prefix : "", name, common_exts[e]);
            DeleteFileA(old_file);
        }
    }
}

void build_save_canvas_asset_plan(SaveCanvasAssetPlan *out_plan) {
    if (!out_plan) return;
    memset(out_plan, 0, sizeof(*out_plan));

    /* 1. Background Image */
    if (g_has_bg_image && g_canvas_img.hbm) {
        SaveCanvasAssetItem *item = &out_plan->items[out_plan->count++];
        item->type = SAVE_ASSET_BACKGROUND;
        item->index = 0;
        strncpy(item->label, "Background", sizeof(item->label) - 1);
        strncpy(item->id_name, g_canvas_name[0] ? g_canvas_name : "level1", sizeof(item->id_name) - 1);
        strncpy(item->source_path, g_canvas_img.filepath, sizeof(item->source_path) - 1);
        image_get_extension(g_canvas_img.filepath, item->orig_ext, sizeof(item->orig_ext));
        if (item->orig_ext[0] == '\0') strcpy(item->orig_ext, "png");

        item->width = g_canvas_img.width;
        item->height = g_canvas_img.height;
        item->is_video = g_canvas_img.is_video;
        item->is_animated = g_canvas_img.is_animated;
        item->frame_count = g_canvas_img.frame_count;
        item->img_ref = &g_canvas_img;

        bool is_zuma = image_is_zuma_native_ext(item->orig_ext);
        bool can_keep = is_zuma && !item->is_video && (_stricmp(item->orig_ext, "bmp") != 0);
        item->can_keep_orig = can_keep;
        item->keep_orig = can_keep;
        if (item->is_video) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else if (_stricmp(item->orig_ext, "jpg") == 0 || _stricmp(item->orig_ext, "jpeg") == 0) {
            item->selected_format = IMG_EXPORT_JPG;
        } else if (_stricmp(item->orig_ext, "gif") == 0) {
            item->selected_format = IMG_EXPORT_GIF;
        } else if (_stricmp(item->orig_ext, "webp") == 0) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else {
            item->selected_format = IMG_EXPORT_PNG;
        }
        item->resize = false;
    }

    /* 2. Cutouts */
    for (int i = 0; i < g_cutout_count; ++i) {
        if (out_plan->count >= MAX_SAVE_CANVAS_ASSETS) break;
        LevelCutout *co = &g_cutouts[i];
        SaveCanvasAssetItem *item = &out_plan->items[out_plan->count++];
        item->type = SAVE_ASSET_CUTOUT;
        item->index = i;
        if (g_cutout_count == 1) {
            snprintf(item->label, sizeof(item->label), "Cutout");
            snprintf(item->id_name, sizeof(item->id_name), "tunnel");
        } else {
            snprintf(item->label, sizeof(item->label), "Cutout #%d", i + 1);
            snprintf(item->id_name, sizeof(item->id_name), "tunnel%d", i + 1);
        }
        if (co->image_name[0]) {
            strncpy(item->id_name, co->image_name, sizeof(item->id_name) - 1);
        }

        const char *src_p = co->source_filepath[0] ? co->source_filepath : co->img.filepath;
        strncpy(item->source_path, src_p, sizeof(item->source_path) - 1);
        image_get_extension(src_p, item->orig_ext, sizeof(item->orig_ext));
        if (item->orig_ext[0] == '\0') strcpy(item->orig_ext, "png");

        item->is_video = (co->has_img && co->img.is_video) || image_is_video_ext(src_p);
        item->is_animated = (co->has_img && co->img.is_animated);
        item->frame_count = co->has_img ? co->img.frame_count : 1;
        item->width = co->has_img ? co->img.width : (g_has_default_cutout_img ? g_default_cutout_img.width : 160);
        item->height = co->has_img ? co->img.height : (g_has_default_cutout_img ? g_default_cutout_img.height : 140);
        item->img_ref = co->has_img ? &co->img : (g_has_default_cutout_img ? &g_default_cutout_img : NULL);

        bool is_zuma = image_is_zuma_native_ext(item->orig_ext);
        bool can_keep = is_zuma && !item->is_video && (_stricmp(item->orig_ext, "bmp") != 0);
        item->can_keep_orig = can_keep;
        item->keep_orig = can_keep;
        if (item->is_video) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else if (_stricmp(item->orig_ext, "jpg") == 0 || _stricmp(item->orig_ext, "jpeg") == 0) {
            item->selected_format = IMG_EXPORT_JPG;
        } else if (_stricmp(item->orig_ext, "gif") == 0) {
            item->selected_format = IMG_EXPORT_GIF;
        } else if (_stricmp(item->orig_ext, "webp") == 0) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else {
            item->selected_format = IMG_EXPORT_PNG;
        }
        item->resize = false;
    }

    /* 3. Sprites */
    for (int i = 0; i < g_sprite_count; ++i) {
        if (out_plan->count >= MAX_SAVE_CANVAS_ASSETS) break;
        LevelSprite *sp = &g_sprites[i];
        SaveCanvasAssetItem *item = &out_plan->items[out_plan->count++];
        item->type = SAVE_ASSET_SPRITE;
        item->index = i;
        if (g_sprite_count == 1) {
            snprintf(item->label, sizeof(item->label), "Sprite");
            snprintf(item->id_name, sizeof(item->id_name), "sprite");
        } else {
            snprintf(item->label, sizeof(item->label), "Sprite #%d", i + 1);
            snprintf(item->id_name, sizeof(item->id_name), "sprite%d", i + 1);
        }
        if (sp->image_name[0]) {
            strncpy(item->id_name, sp->image_name, sizeof(item->id_name) - 1);
        }

        const char *src_p = sp->source_filepath[0] ? sp->source_filepath : sp->img.filepath;
        strncpy(item->source_path, src_p, sizeof(item->source_path) - 1);
        image_get_extension(src_p, item->orig_ext, sizeof(item->orig_ext));
        if (item->orig_ext[0] == '\0') strcpy(item->orig_ext, "png");

        item->is_video = (sp->has_img && sp->img.is_video) || image_is_video_ext(src_p);
        item->is_animated = (sp->has_img && sp->img.is_animated);
        item->frame_count = sp->has_img ? sp->img.frame_count : 1;
        item->width = sp->has_img ? sp->img.width : (g_has_default_sprite_img ? g_default_sprite_img.width : 120);
        item->height = sp->has_img ? sp->img.height : (g_has_default_sprite_img ? g_default_sprite_img.height : 120);
        item->img_ref = sp->has_img ? &sp->img : (g_has_default_sprite_img ? &g_default_sprite_img : NULL);

        bool is_zuma = image_is_zuma_native_ext(item->orig_ext);
        bool can_keep = is_zuma && !item->is_video && (_stricmp(item->orig_ext, "bmp") != 0);
        item->can_keep_orig = can_keep;
        item->keep_orig = can_keep;
        if (item->is_video) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else if (_stricmp(item->orig_ext, "jpg") == 0 || _stricmp(item->orig_ext, "jpeg") == 0) {
            item->selected_format = IMG_EXPORT_JPG;
        } else if (_stricmp(item->orig_ext, "gif") == 0) {
            item->selected_format = IMG_EXPORT_GIF;
        } else if (_stricmp(item->orig_ext, "webp") == 0) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else {
            item->selected_format = IMG_EXPORT_PNG;
        }
        item->resize = false;
    }

    /* 4. BackgroundAlphas */
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        if (out_plan->count >= MAX_SAVE_CANVAS_ASSETS) break;
        LevelBackgroundAlpha *ba = &g_bg_alphas[i];
        SaveCanvasAssetItem *item = &out_plan->items[out_plan->count++];
        item->type = SAVE_ASSET_BGALPHA;
        item->index = i;
        if (g_bg_alpha_count == 1) {
            snprintf(item->label, sizeof(item->label), "BgAlpha");
            snprintf(item->id_name, sizeof(item->id_name), "quake");
        } else {
            snprintf(item->label, sizeof(item->label), "BgAlpha #%d", i + 1);
            snprintf(item->id_name, sizeof(item->id_name), "quake%d", i + 1);
        }
        if (ba->image_name[0]) {
            const char *cn = (ba->image_name[0] == '_') ? (ba->image_name + 1) : ba->image_name;
            strncpy(item->id_name, cn, sizeof(item->id_name) - 1);
        }

        const char *src_p = ba->source_filepath[0] ? ba->source_filepath : ba->img.filepath;
        strncpy(item->source_path, src_p, sizeof(item->source_path) - 1);
        image_get_extension(src_p, item->orig_ext, sizeof(item->orig_ext));
        if (item->orig_ext[0] == '\0') strcpy(item->orig_ext, "png");

        item->is_video = (ba->has_img && ba->img.is_video) || image_is_video_ext(src_p);
        item->is_animated = (ba->has_img && ba->img.is_animated);
        item->frame_count = ba->has_img ? ba->img.frame_count : 1;
        item->width = ba->has_img ? ba->img.width : (g_has_default_bgalpha_img ? g_default_bgalpha_img.width : 160);
        item->height = ba->has_img ? ba->img.height : (g_has_default_bgalpha_img ? g_default_bgalpha_img.height : 140);
        item->img_ref = ba->has_img ? &ba->img : (g_has_default_bgalpha_img ? &g_default_bgalpha_img : NULL);

        bool is_zuma = image_is_zuma_native_ext(item->orig_ext);
        bool can_keep = is_zuma && !item->is_video && (_stricmp(item->orig_ext, "bmp") != 0);
        item->can_keep_orig = can_keep;
        item->keep_orig = can_keep;
        if (item->is_video) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else if (_stricmp(item->orig_ext, "jpg") == 0 || _stricmp(item->orig_ext, "jpeg") == 0) {
            item->selected_format = IMG_EXPORT_JPG;
        } else if (_stricmp(item->orig_ext, "gif") == 0) {
            item->selected_format = IMG_EXPORT_GIF;
        } else if (_stricmp(item->orig_ext, "webp") == 0) {
            item->selected_format = IMG_EXPORT_WEBP;
        } else {
            item->selected_format = IMG_EXPORT_PNG;
        }
        item->resize = false;
    }
}

bool execute_save_canvas_with_plan(
    HWND hwnd,
    const char *canvas_name,
    const char *canvas_title,
    const SaveCanvasAssetPlan *plan
) {
    if (!g_has_canvas) return false;
    if (!canvas_name || canvas_name[0] == '\0') canvas_name = "level1";

    /* Sanitize level ID for Zuma Deluxe compatibility */
    char safe_name[128] = {0};
    int dst = 0;
    for (const char *p = canvas_name; *p && dst < 32; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            safe_name[dst++] = (char)(c + ('a' - 'A'));
        } else if (c == ' ') {
            safe_name[dst++] = '_';
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            safe_name[dst++] = c;
        } else {
            safe_name[dst++] = '_';
        }
    }
    safe_name[dst] = '\0';

    if (safe_name[0] == '\0') {
        strcpy(safe_name, "level1");
    } else if (safe_name[0] >= '0' && safe_name[0] <= '9') {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "lvl_%s", safe_name);
        tmp[32] = '\0';
        strcpy(safe_name, tmp);
    }

    if (!canvas_title || canvas_title[0] == '\0') canvas_title = safe_name;
    char safe_title[128] = {0};
    int t_dst = 0;
    for (const char *t = canvas_title; *t && t_dst < 64; ++t) {
        char c = *t;
        if (c == '"' || c == '<' || c == '>' || c == '&' || c == '\r' || c == '\n') {
            safe_title[t_dst++] = ' ';
        } else {
            safe_title[t_dst++] = c;
        }
    }
    safe_title[t_dst] = '\0';

    char *st = safe_title;
    while (*st == ' ' || *st == '\t') st++;
    if (*st == '\0') {
        strcpy(safe_title, safe_name);
    } else {
        if (st != safe_title) memmove(safe_title, st, strlen(st) + 1);
        char *et = safe_title + strlen(safe_title) - 1;
        while (et >= safe_title && (*et == ' ' || *et == '\t')) {
            *et = '\0';
            et--;
        }
    }

    char base_dir[MAX_PATH] = {0};
    get_canvas_base_directory(base_dir, sizeof(base_dir));

    char folder_path[MAX_PATH] = {0};
    snprintf(folder_path, sizeof(folder_path), "%s\\%s", base_dir, safe_name);
    CreateDirectoryA(folder_path, NULL);
    update_save_progress("Initializing canvas export folder...", 5);

    /* Background item in plan */
    const SaveCanvasAssetItem *bg_plan_item = NULL;
    if (plan) {
        for (int k = 0; k < plan->count; ++k) {
            if (plan->items[k].type == SAVE_ASSET_BACKGROUND) {
                bg_plan_item = &plan->items[k];
                break;
            }
        }
    }

    char final_img_filename[MAX_PATH] = {0};
    int final_img_w = 0;
    int final_img_h = 0;
    bool bg_resized = false;

    if (g_has_bg_image && g_canvas_img.hbm) {
        ImageExportFormat bg_fmt = IMG_EXPORT_KEEP_ORIGINAL;
        bool resize = false;
        char ext[16] = "png";

        if (bg_plan_item) {
            bg_fmt = bg_plan_item->keep_orig ? IMG_EXPORT_KEEP_ORIGINAL : bg_plan_item->selected_format;
            resize = (g_canvas_w > 0 && g_canvas_h > 0);
            const char *chosen_ext = get_ext_for_format(bg_fmt, bg_plan_item->orig_ext);
            strncpy(ext, chosen_ext, sizeof(ext) - 1);
        } else {
            image_get_extension(g_canvas_img.filepath, ext, sizeof(ext));
            if (ext[0] == '\0') strcpy(ext, "png");
            resize = (g_canvas_w > 0 && g_canvas_h > 0);
        }
        bg_resized = resize;

        if (g_canvas_img.is_video) {
            update_save_progress("Encoding video frames to Animated WebP (Zuma Compleluxe)...", 15);
        } else {
            update_save_progress("Exporting background image...", 15);
        }

        snprintf(final_img_filename, sizeof(final_img_filename), "%s.%s", safe_name, ext);
        char dest_img_path[MAX_PATH] = {0};
        snprintf(dest_img_path, sizeof(dest_img_path), "%s\\%s", folder_path, final_img_filename);

        /* Clean up any older background files with different extensions */
        cleanup_alternate_ext_files(folder_path, NULL, safe_name, ext);

        int max_fr = (bg_plan_item) ? bg_plan_item->max_export_frames : 0;
        if (image_export_to_file_ex(&g_canvas_img, dest_img_path, bg_fmt, resize, g_canvas_w, g_canvas_h, max_fr)) {
            strncpy(g_canvas_img.filepath, dest_img_path, sizeof(g_canvas_img.filepath) - 1);
            strncpy(g_canvas_img.filename, final_img_filename, sizeof(g_canvas_img.filename) - 1);
            strncpy(g_canvas_img.display_name, safe_name, sizeof(g_canvas_img.display_name) - 1);
            if (resize) {
                image_free(&g_canvas_img);
                image_load_from_file(dest_img_path, &g_canvas_img);
                strncpy(g_canvas_img.display_name, safe_name, sizeof(g_canvas_img.display_name) - 1);
            }
            final_img_w = g_canvas_img.width;
            final_img_h = g_canvas_img.height;
            if (_stricmp(ext, "webp") == 0) {
                g_canvas_img.is_video = false;
                if (g_canvas_img.frame_count > 1) {
                    g_canvas_img.is_animated = true;
                }
            }
        }
    }

    /* 1. Defensively compact empty paths before generating settings.xml and exporting */
    compact_empty_paths();

    /* 2. Update path identifiers based on safe_name */
    if (g_path_count == 1) {
        snprintf(g_paths[0].name, sizeof(g_paths[0].name), "%s", safe_name);
    } else {
        for (int p = 0; p < g_path_count; ++p) {
            snprintf(g_paths[p].name, sizeof(g_paths[p].name), "%s-%d", safe_name, p + 1);
        }
    }

    /* Save Cutout assets to folder_path and sync image names */
    if (g_cutout_count > 0) {
        update_save_progress("Exporting tunnel & cutout overlay assets...", 62);
    }
    for (int i = 0; i < g_cutout_count; ++i) {
        char co_name[64];
        if (g_cutout_count == 1) {
            snprintf(co_name, sizeof(co_name), "tunnel");
        } else {
            snprintf(co_name, sizeof(co_name), "tunnel%d", i + 1);
        }
        strncpy(g_cutouts[i].image_name, co_name, sizeof(g_cutouts[i].image_name) - 1);

        const SaveCanvasAssetItem *co_plan_item = NULL;
        if (plan) {
            for (int k = 0; k < plan->count; ++k) {
                if (plan->items[k].type == SAVE_ASSET_CUTOUT && plan->items[k].index == i) {
                    co_plan_item = &plan->items[k];
                    break;
                }
            }
        }

        ImageExportFormat co_fmt = IMG_EXPORT_PNG;
        char ext[16] = "png";
        bool is_video = (g_cutouts[i].has_img && g_cutouts[i].img.is_video) ||
                        (g_cutouts[i].source_filepath[0] && image_is_video_ext(g_cutouts[i].source_filepath));

        if (co_plan_item) {
            co_fmt = co_plan_item->keep_orig ? IMG_EXPORT_KEEP_ORIGINAL : co_plan_item->selected_format;
            const char *chosen_ext = get_ext_for_format(co_fmt, co_plan_item->orig_ext);
            strncpy(ext, chosen_ext, sizeof(ext) - 1);
        } else {
            if (is_video) {
                strcpy(ext, "webp");
                co_fmt = IMG_EXPORT_WEBP;
            } else if (g_cutouts[i].source_filepath[0]) {
                image_get_extension(g_cutouts[i].source_filepath, ext, sizeof(ext));
                co_fmt = IMG_EXPORT_KEEP_ORIGINAL;
            } else {
                strcpy(ext, "png");
                co_fmt = IMG_EXPORT_PNG;
            }
        }
        if (ext[0] == '\0' || _stricmp(ext, "bmp") == 0) strcpy(ext, "png");

        char dest_path[MAX_PATH];
        snprintf(dest_path, sizeof(dest_path), "%s\\_%s.%s", folder_path, co_name, ext);

        /* Clean up any older cutout files with different extensions */
        cleanup_alternate_ext_files(folder_path, "_", co_name, ext);
        cleanup_alternate_ext_files(folder_path, NULL, co_name, ext);

        if (co_fmt == IMG_EXPORT_KEEP_ORIGINAL && !is_video && g_cutouts[i].source_filepath[0] &&
            GetFileAttributesA(g_cutouts[i].source_filepath) != INVALID_FILE_ATTRIBUTES &&
            _stricmp(g_cutouts[i].source_filepath, dest_path) != 0 &&
            image_is_zuma_native_ext(ext)) {
            CopyFileA(g_cutouts[i].source_filepath, dest_path, FALSE);
            strncpy(g_cutouts[i].source_filepath, dest_path, sizeof(g_cutouts[i].source_filepath) - 1);
            strncpy(g_cutouts[i].img.filepath, dest_path, sizeof(g_cutouts[i].img.filepath) - 1);
        } else {
            if ((!g_cutouts[i].has_img || !g_cutouts[i].img.hbm) && g_cutouts[i].source_filepath[0] &&
                GetFileAttributesA(g_cutouts[i].source_filepath) != INVALID_FILE_ATTRIBUTES) {
                if (image_load_from_file(g_cutouts[i].source_filepath, &g_cutouts[i].img)) {
                    g_cutouts[i].has_img = true;
                }
            }
            if (g_cutouts[i].has_img && g_cutouts[i].img.hbm) {
                int max_fr = (co_plan_item) ? co_plan_item->max_export_frames : 0;
                image_export_to_file_ex(&g_cutouts[i].img, dest_path, co_fmt, false, 0, 0, max_fr);
                strncpy(g_cutouts[i].source_filepath, dest_path, sizeof(g_cutouts[i].source_filepath) - 1);
                strncpy(g_cutouts[i].img.filepath, dest_path, sizeof(g_cutouts[i].img.filepath) - 1);
                if (_stricmp(ext, "webp") == 0) {
                    g_cutouts[i].img.is_video = false;
                    if (g_cutouts[i].img.frame_count > 1) g_cutouts[i].img.is_animated = true;
                }
            }
        }
    }

    /* Save Sprite assets to folder_path and sync image names */
    if (g_sprite_count > 0) {
        update_save_progress("Exporting sprite overlay assets...", 75);
    }
    for (int i = 0; i < g_sprite_count; ++i) {
        char sp_name[64];
        if (g_sprite_count == 1) {
            snprintf(sp_name, sizeof(sp_name), "sprite");
        } else {
            snprintf(sp_name, sizeof(sp_name), "sprite%d", i + 1);
        }
        strncpy(g_sprites[i].image_name, sp_name, sizeof(g_sprites[i].image_name) - 1);

        const SaveCanvasAssetItem *sp_plan_item = NULL;
        if (plan) {
            for (int k = 0; k < plan->count; ++k) {
                if (plan->items[k].type == SAVE_ASSET_SPRITE && plan->items[k].index == i) {
                    sp_plan_item = &plan->items[k];
                    break;
                }
            }
        }

        ImageExportFormat sp_fmt = IMG_EXPORT_PNG;
        char ext[16] = "png";
        bool is_video = (g_sprites[i].has_img && g_sprites[i].img.is_video) ||
                        (g_sprites[i].source_filepath[0] && image_is_video_ext(g_sprites[i].source_filepath));

        if (sp_plan_item) {
            sp_fmt = sp_plan_item->keep_orig ? IMG_EXPORT_KEEP_ORIGINAL : sp_plan_item->selected_format;
            const char *chosen_ext = get_ext_for_format(sp_fmt, sp_plan_item->orig_ext);
            strncpy(ext, chosen_ext, sizeof(ext) - 1);
        } else {
            if (is_video) {
                strcpy(ext, "webp");
                sp_fmt = IMG_EXPORT_WEBP;
            } else if (g_sprites[i].source_filepath[0]) {
                image_get_extension(g_sprites[i].source_filepath, ext, sizeof(ext));
                sp_fmt = IMG_EXPORT_KEEP_ORIGINAL;
            } else {
                strcpy(ext, "png");
                sp_fmt = IMG_EXPORT_PNG;
            }
        }
        if (ext[0] == '\0' || _stricmp(ext, "bmp") == 0) strcpy(ext, "png");

        char dest_path[MAX_PATH];
        snprintf(dest_path, sizeof(dest_path), "%s\\%s.%s", folder_path, sp_name, ext);

        /* Clean up any older sprite files with different extensions */
        cleanup_alternate_ext_files(folder_path, NULL, sp_name, ext);

        if (sp_fmt == IMG_EXPORT_KEEP_ORIGINAL && !is_video && g_sprites[i].source_filepath[0] &&
            GetFileAttributesA(g_sprites[i].source_filepath) != INVALID_FILE_ATTRIBUTES &&
            _stricmp(g_sprites[i].source_filepath, dest_path) != 0 &&
            image_is_zuma_native_ext(ext)) {
            CopyFileA(g_sprites[i].source_filepath, dest_path, FALSE);
            strncpy(g_sprites[i].source_filepath, dest_path, sizeof(g_sprites[i].source_filepath) - 1);
            strncpy(g_sprites[i].img.filepath, dest_path, sizeof(g_sprites[i].img.filepath) - 1);
        } else {
            if ((!g_sprites[i].has_img || !g_sprites[i].img.hbm) && g_sprites[i].source_filepath[0] &&
                GetFileAttributesA(g_sprites[i].source_filepath) != INVALID_FILE_ATTRIBUTES) {
                if (image_load_from_file(g_sprites[i].source_filepath, &g_sprites[i].img)) {
                    g_sprites[i].has_img = true;
                }
            }
            if (g_sprites[i].has_img && g_sprites[i].img.hbm) {
                int max_fr = (sp_plan_item) ? sp_plan_item->max_export_frames : 0;
                image_export_to_file_ex(&g_sprites[i].img, dest_path, sp_fmt, false, 0, 0, max_fr);
                strncpy(g_sprites[i].source_filepath, dest_path, sizeof(g_sprites[i].source_filepath) - 1);
                strncpy(g_sprites[i].img.filepath, dest_path, sizeof(g_sprites[i].img.filepath) - 1);
                if (_stricmp(ext, "webp") == 0) {
                    g_sprites[i].img.is_video = false;
                    if (g_sprites[i].img.frame_count > 1) g_sprites[i].img.is_animated = true;
                }
            }
        }
    }

    /* Save BackgroundAlpha assets to folder_path and sync image names */
    if (g_bg_alpha_count > 0) {
        update_save_progress("Exporting BackgroundAlpha overlays...", 85);
    }
    for (int i = 0; i < g_bg_alpha_count; ++i) {
        char ba_name[64];
        if (g_bg_alphas[i].image_name[0]) {
            strncpy(ba_name, g_bg_alphas[i].image_name, sizeof(ba_name) - 1);
            ba_name[sizeof(ba_name) - 1] = '\0';
        } else if (g_bg_alpha_count == 1) {
            snprintf(ba_name, sizeof(ba_name), "quake");
        } else {
            snprintf(ba_name, sizeof(ba_name), "quake%d", i + 1);
        }
        const char *clean_name = (ba_name[0] == '_') ? (ba_name + 1) : ba_name;
        strncpy(g_bg_alphas[i].image_name, clean_name, sizeof(g_bg_alphas[i].image_name) - 1);

        const SaveCanvasAssetItem *ba_plan_item = NULL;
        if (plan) {
            for (int k = 0; k < plan->count; ++k) {
                if (plan->items[k].type == SAVE_ASSET_BGALPHA && plan->items[k].index == i) {
                    ba_plan_item = &plan->items[k];
                    break;
                }
            }
        }

        ImageExportFormat ba_fmt = IMG_EXPORT_PNG;
        char ext[16] = "png";
        bool is_video = (g_bg_alphas[i].has_img && g_bg_alphas[i].img.is_video) ||
                        (g_bg_alphas[i].source_filepath[0] && image_is_video_ext(g_bg_alphas[i].source_filepath));

        if (ba_plan_item) {
            ba_fmt = ba_plan_item->keep_orig ? IMG_EXPORT_KEEP_ORIGINAL : ba_plan_item->selected_format;
            const char *chosen_ext = get_ext_for_format(ba_fmt, ba_plan_item->orig_ext);
            strncpy(ext, chosen_ext, sizeof(ext) - 1);
        } else {
            if (is_video) {
                strcpy(ext, "webp");
                ba_fmt = IMG_EXPORT_WEBP;
            } else if (g_bg_alphas[i].source_filepath[0]) {
                image_get_extension(g_bg_alphas[i].source_filepath, ext, sizeof(ext));
                ba_fmt = IMG_EXPORT_KEEP_ORIGINAL;
            } else {
                strcpy(ext, "png");
                ba_fmt = IMG_EXPORT_PNG;
            }
        }
        if (ext[0] == '\0' || _stricmp(ext, "bmp") == 0) strcpy(ext, "png");

        char dest_path[MAX_PATH];
        snprintf(dest_path, sizeof(dest_path), "%s\\_%s.%s", folder_path, clean_name, ext);

        /* Clean up any older bgalpha files with different extensions */
        cleanup_alternate_ext_files(folder_path, "_", clean_name, ext);
        cleanup_alternate_ext_files(folder_path, NULL, clean_name, ext);

        if (ba_fmt == IMG_EXPORT_KEEP_ORIGINAL && !is_video && g_bg_alphas[i].source_filepath[0] &&
            GetFileAttributesA(g_bg_alphas[i].source_filepath) != INVALID_FILE_ATTRIBUTES &&
            _stricmp(g_bg_alphas[i].source_filepath, dest_path) != 0 &&
            image_is_zuma_native_ext(ext)) {
            CopyFileA(g_bg_alphas[i].source_filepath, dest_path, FALSE);
            strncpy(g_bg_alphas[i].source_filepath, dest_path, sizeof(g_bg_alphas[i].source_filepath) - 1);
            strncpy(g_bg_alphas[i].img.filepath, dest_path, sizeof(g_bg_alphas[i].img.filepath) - 1);
        } else {
            if ((!g_bg_alphas[i].has_img || !g_bg_alphas[i].img.hbm) && g_bg_alphas[i].source_filepath[0] &&
                GetFileAttributesA(g_bg_alphas[i].source_filepath) != INVALID_FILE_ATTRIBUTES) {
                if (image_load_from_file(g_bg_alphas[i].source_filepath, &g_bg_alphas[i].img)) {
                    g_bg_alphas[i].has_img = true;
                }
            }
            if (g_bg_alphas[i].has_img && g_bg_alphas[i].img.hbm) {
                int max_fr = (ba_plan_item) ? ba_plan_item->max_export_frames : 0;
                image_export_to_file_ex(&g_bg_alphas[i].img, dest_path, ba_fmt, false, 0, 0, max_fr);
                strncpy(g_bg_alphas[i].source_filepath, dest_path, sizeof(g_bg_alphas[i].source_filepath) - 1);
                strncpy(g_bg_alphas[i].img.filepath, dest_path, sizeof(g_bg_alphas[i].img.filepath) - 1);
                if (_stricmp(ext, "webp") == 0) {
                    g_bg_alphas[i].img.is_video = false;
                    if (g_bg_alphas[i].img.frame_count > 1) g_bg_alphas[i].img.is_animated = true;
                }
            }
        }
    }

    /* 3. Save settings.xml with up-to-date curve count & names */
    update_save_progress("Writing curves (.dat & .zupath) and settings.xml...", 92);
    save_canvas_settings_xml(
        folder_path, safe_name, safe_title, g_canvas_w, g_canvas_h,
        final_img_filename, final_img_w, final_img_h, bg_resized
    );

    /* 4. Save .zupath vector sidecar & export .dat binaries */
    char zupath_filepath[MAX_PATH];
    snprintf(zupath_filepath, sizeof(zupath_filepath), "%s\\%s.zupath", folder_path, safe_name);

    if (g_path_count > 0) {
        for (int p = 0; p < g_path_count; ++p) {
            bezier_path_prune_collapsed_spans(&g_paths[p]);
        }
        bezier_path_save_zupath(g_paths, g_path_count, zupath_filepath);

        if (g_path_count == 1) {
            /* Exactly 1 path: export [safe_name].dat */
            char dat_filepath[MAX_PATH];
            snprintf(dat_filepath, sizeof(dat_filepath), "%s\\%s.dat", folder_path, safe_name);
            if (g_paths[0].node_count >= 2) {
                bezier_path_export_dat(&g_paths[0], dat_filepath);
            } else {
                remove(dat_filepath);
            }
            /* Clean up any obsolete multi-track dat files: [safe_name]-*.dat */
            for (int k = 1; k <= 64; ++k) {
                char old_dat[MAX_PATH];
                snprintf(old_dat, sizeof(old_dat), "%s\\%s-%d.dat", folder_path, safe_name, k);
                remove(old_dat);
            }
        } else {
            /* Multiple paths: export [safe_name]-1.dat, [safe_name]-2.dat, etc. */
            for (int p = 0; p < g_path_count; ++p) {
                char dat_filepath[MAX_PATH];
                snprintf(dat_filepath, sizeof(dat_filepath), "%s\\%s-%d.dat", folder_path, safe_name, p + 1);
                if (g_paths[p].node_count >= 2) {
                    bezier_path_export_dat(&g_paths[p], dat_filepath);
                } else {
                    remove(dat_filepath);
                }
            }
            /* Clean up obsolete single-track dat file: [safe_name].dat */
            char old_dat[MAX_PATH];
            snprintf(old_dat, sizeof(old_dat), "%s\\%s.dat", folder_path, safe_name);
            remove(old_dat);

            /* Clean up obsolete higher index dat files: [safe_name]-k.dat for k > g_path_count */
            for (int k = g_path_count + 1; k <= 64; ++k) {
                char stale_dat[MAX_PATH];
                snprintf(stale_dat, sizeof(stale_dat), "%s\\%s-%d.dat", folder_path, safe_name, k);
                remove(stale_dat);
            }
        }
    } else {
        /* g_path_count == 0: preserve vector sidecar with 0 paths */
        bezier_path_save_zupath(g_paths, 0, zupath_filepath);

        char old_dat[MAX_PATH];
        snprintf(old_dat, sizeof(old_dat), "%s\\%s.dat", folder_path, safe_name);
        remove(old_dat);

        for (int k = 1; k <= 64; ++k) {
            char stale_dat[MAX_PATH];
            snprintf(stale_dat, sizeof(stale_dat), "%s\\%s-%d.dat", folder_path, safe_name, k);
            remove(stale_dat);
        }
    }

    /* Prune any leftover unregistered files in the folder ("file sampah") */
    prune_unregistered_canvas_files(folder_path, safe_name);

    strncpy(g_canvas_name, safe_name, sizeof(g_canvas_name) - 1);
    sync_canvas_title_everywhere(safe_title, NULL);
    strncpy(g_canvas_folder, folder_path, sizeof(g_canvas_folder) - 1);
    sync_all_map_attr_edits_from_state();

    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg), "Saved: ./canvas/%s/", safe_name);
    SendMessageA(g_hwnd_status, SB_SETTEXTA, 0 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)status_msg);

    update_status_bar();
    InvalidateRect(hwnd, NULL, FALSE);
    update_save_progress("Canvas project saved successfully!", 100);
    return true;
}

bool browse_for_folder(HWND hwnd_owner, char *out_folder, size_t max_len) {
    if (!out_folder || max_len == 0) return false;
    out_folder[0] = '\0';
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = hwnd_owner;
    bi.lpszTitle = "Select Canvas Project Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        if (SHGetPathFromIDListA(pidl, out_folder)) {
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
}

/* Validate Zuma Deluxe levels.xml */
bool validate_zuma_levels_xml(const char *file_path, char *out_error, size_t max_err_len, int *out_level_count) {
    if (out_error && max_err_len > 0) out_error[0] = '\0';
    if (out_level_count) *out_level_count = 0;

    if (!file_path || file_path[0] == '\0') {
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "File path is empty or NULL.");
        }
        return false;
    }

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "Could not open file:\n%s\n\nError: %s", file_path, strerror(errno));
        }
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 16) {
        fclose(f);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "File is too small or empty (%ld bytes).\nA valid Zuma Deluxe levels.xml must contain level definitions.", sz);
        }
        return false;
    }

    if (sz > 16 * 1024 * 1024) { /* 16 MB max */
        fclose(f);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "File size (%ld bytes) exceeds maximum allowable size (16 MB).", sz);
        }
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "Memory allocation error while reading levels.xml.");
        }
        return false;
    }

    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    /* Check for root <Levels> tag (case-insensitive) */
    char *p_open = NULL;
    char *p_close = NULL;
    for (char *p = buf; *p; ++p) {
        if (*p == '<' && strncasecmp(p, "<Levels", 7) == 0) {
            p_open = p;
            break;
        }
    }
    if (!p_open) {
        free(buf);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "Missing root element <Levels>.\n\nA valid Zuma Deluxe levels definition must start with <Levels>.");
        }
        return false;
    }

    for (char *p = buf; *p; ++p) {
        if (*p == '<' && strncasecmp(p, "</Levels>", 9) == 0) {
            p_close = p;
        }
    }
    if (!p_close || p_close <= p_open) {
        free(buf);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "Missing closing root element </Levels>.\n\nThe file appears to be corrupted, truncated, or improperly formatted.");
        }
        return false;
    }

    /* Count <Graphics definitions, ignoring commented regions <!-- ... --> */
    int graphics_count = 0;
    bool has_valid_graphics_attrs = false;
    bool in_comment = false;

    for (char *p = buf; *p; ++p) {
        if (!in_comment && p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            in_comment = true;
            p += 3;
            continue;
        }
        if (in_comment && p[0] == '-' && p[1] == '-' && p[2] == '>') {
            in_comment = false;
            p += 2;
            continue;
        }
        if (in_comment) continue;

        if (p[0] == '<' && strncasecmp(p, "<Graphics", 9) == 0) {
            /* Find closing bracket > of this Graphics tag */
            char *tag_end = strchr(p, '>');
            if (tag_end) {
                graphics_count++;
                /* Verify essential attributes: id= and (curve= or image=) */
                int tag_len = (int)(tag_end - p);
                char tag_buf[512] = {0};
                if (tag_len > 510) tag_len = 510;
                memcpy(tag_buf, p, tag_len);
                tag_buf[tag_len] = '\0';

                if (strstr(tag_buf, "id=") != NULL && (strstr(tag_buf, "curve=") != NULL || strstr(tag_buf, "image=") != NULL)) {
                    has_valid_graphics_attrs = true;
                }
            }
        }
    }

    if (graphics_count == 0) {
        free(buf);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "No active <Graphics> level definitions found.\n\nA valid Zuma Deluxe levels.xml must declare at least one level using <Graphics id=\"...\" curve=\"...\" image=\"...\">.");
        }
        return false;
    }

    if (!has_valid_graphics_attrs) {
        free(buf);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "Malformed <Graphics> tag: missing essential attributes (id, curve, or image).");
        }
        return false;
    }

    /* Verify presence of either <Settings or <StageProgression */
    bool has_settings = (strstr(buf, "<Settings") != NULL);
    bool has_progression = (strstr(buf, "<StageProgression") != NULL);

    if (!has_settings && !has_progression) {
        free(buf);
        if (out_error && max_err_len > 0) {
            snprintf(out_error, max_err_len, "Missing game parameter sections.\n\nNeither <Settings> nor <StageProgression> was found in levels.xml. A genuine Zuma Deluxe levels.xml requires difficulty settings and progression mapping.");
        }
        return false;
    }

    if (out_level_count) *out_level_count = graphics_count;
    free(buf);
    return true;
}

/* Thread hook to ensure IFileOpenDialog window and explorer controls match current dark/light theme */
static HHOOK s_hCbtDialogHook = NULL;
static LRESULT CALLBACK CbtDialogHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_CREATEWND || nCode == HCBT_ACTIVATE) {
        HWND hwnd = (HWND)wParam;
        if (hwnd && IsWindow(hwnd)) {
            update_window_theme_attributes(hwnd, g_current_theme == THEME_DARK);
        }
    }
    return CallNextHookEx(s_hCbtDialogHook, nCode, wParam, lParam);
}

/* Browse for Zuma Deluxe Folder using modern Windows File Explorer dialog with 'Set Folder' button */
bool browse_for_zuma_folder(HWND hwnd_owner, char *out_folder, size_t max_len) {
    if (!out_folder || max_len == 0) return false;
    out_folder[0] = '\0';

    HRESULT hr_co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    static const GUID s_CLSID_FileOpenDialog = { 0xdc1c5a9c, 0xe88a, 0x4dde, { 0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7 } };
    static const GUID s_IID_IFileOpenDialog  = { 0xd57c7288, 0xd4ad, 0x4768, { 0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60 } };
    static const GUID s_IID_IShellItem       = { 0x43826d1e, 0xe718, 0x42ee, { 0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe } };

    IFileOpenDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&s_CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &s_IID_IFileOpenDialog, (void **)&pfd);
    bool picked = false;

    if (SUCCEEDED(hr) && pfd) {
        DWORD dwFlags = 0;
        pfd->lpVtbl->GetOptions(pfd, &dwFlags);
        pfd->lpVtbl->SetOptions(pfd, dwFlags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        pfd->lpVtbl->SetTitle(pfd, L"Select Zuma Deluxe Game or Levels Folder");
        pfd->lpVtbl->SetOkButtonLabel(pfd, L"Set Folder");

        /* Set initial directory if already configured */
        if (g_has_zuma_dir && g_zuma_levels_dir[0] != '\0') {
            typedef HRESULT (WINAPI *PFN_SHCreateItemFromParsingName)(PCWSTR, void*, const GUID*, void**);
            HMODULE hShell = GetModuleHandleA("shell32.dll");
            if (hShell) {
                PFN_SHCreateItemFromParsingName pfnSHCreate = (PFN_SHCreateItemFromParsingName)GetProcAddress(hShell, "SHCreateItemFromParsingName");
                if (pfnSHCreate) {
                    wchar_t winit[MAX_PATH];
                    MultiByteToWideChar(CP_ACP, 0, g_zuma_levels_dir, -1, winit, MAX_PATH);
                    IShellItem *psi_folder = NULL;
                    if (SUCCEEDED(pfnSHCreate(winit, NULL, &s_IID_IShellItem, (void **)&psi_folder)) && psi_folder) {
                        pfd->lpVtbl->SetFolder(pfd, psi_folder);
                        psi_folder->lpVtbl->Release(psi_folder);
                    }
                }
            }
        }

        /* Enforce current theme on dialog window via CBT hook */
        update_window_theme_attributes(hwnd_owner, g_current_theme == THEME_DARK);
        s_hCbtDialogHook = SetWindowsHookExA(WH_CBT, CbtDialogHookProc, NULL, GetCurrentThreadId());

        hr = pfd->lpVtbl->Show(pfd, hwnd_owner);

        if (s_hCbtDialogHook) {
            UnhookWindowsHookEx(s_hCbtDialogHook);
            s_hCbtDialogHook = NULL;
        }

        if (SUCCEEDED(hr)) {
            IShellItem *psi = NULL;
            hr = pfd->lpVtbl->GetResult(pfd, &psi);
            if (SUCCEEDED(hr) && psi) {
                PWSTR pszPath = NULL;
                hr = psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &pszPath);
                if (SUCCEEDED(hr) && pszPath) {
                    WideCharToMultiByte(CP_ACP, 0, pszPath, -1, out_folder, (int)max_len, NULL, NULL);
                    CoTaskMemFree(pszPath);
                    picked = true;
                }
                psi->lpVtbl->Release(psi);
            }
        }
        pfd->lpVtbl->Release(pfd);
    }

    if (SUCCEEDED(hr_co)) {
        CoUninitialize();
    }
    return picked;
}

/* Handle Set Zuma Folder Action with smart resolution and adaptive validation dialogs */
void handle_set_zuma_folder(HWND hwnd) {
    char selected_folder[MAX_PATH] = {0};
    if (!browse_for_zuma_folder(hwnd, selected_folder, sizeof(selected_folder))) {
        /* User cancelled dialog */
        return;
    }

    /* Trim trailing slashes or backslashes */
    size_t len = strlen(selected_folder);
    while (len > 0 && (selected_folder[len - 1] == '\\' || selected_folder[len - 1] == '/')) {
        selected_folder[--len] = '\0';
    }

    if (selected_folder[0] == '\0') return;

    /* Resolution Logic:
       Check 1: Does selected_folder/levels/levels.xml exist? (User selected Zuma root)
       Check 2: Does selected_folder/levels.xml exist? (User selected 'levels' folder directly)
    */
    char candidate_levels_dir[MAX_PATH] = {0};
    char candidate_root_dir[MAX_PATH] = {0};
    char candidate_xml_file[MAX_PATH] = {0};

    char test_sub[MAX_PATH];
    snprintf(test_sub, sizeof(test_sub), "%s\\levels\\levels.xml", selected_folder);

    char test_direct[MAX_PATH];
    snprintf(test_direct, sizeof(test_direct), "%s\\levels.xml", selected_folder);

    if (GetFileAttributesA(test_sub) != INVALID_FILE_ATTRIBUTES) {
        /* User selected Zuma root */
        snprintf(candidate_levels_dir, sizeof(candidate_levels_dir), "%s\\levels", selected_folder);
        strncpy(candidate_root_dir, selected_folder, sizeof(candidate_root_dir) - 1);
        strncpy(candidate_xml_file, test_sub, sizeof(candidate_xml_file) - 1);
    } else if (GetFileAttributesA(test_direct) != INVALID_FILE_ATTRIBUTES) {
        /* User selected levels folder directly */
        strncpy(candidate_levels_dir, selected_folder, sizeof(candidate_levels_dir) - 1);
        strncpy(candidate_xml_file, test_direct, sizeof(candidate_xml_file) - 1);
        strncpy(candidate_root_dir, selected_folder, sizeof(candidate_root_dir) - 1);
        char *p1 = strrchr(candidate_root_dir, '\\');
        char *p2 = strrchr(candidate_root_dir, '/');
        char *ps = (p2 && (!p1 || p2 > p1)) ? p2 : p1;
        if (ps) *ps = '\0';
    } else {
        /* Not a valid levels folder or Zuma root */
        char body[1024];
        snprintf(body, sizeof(body),
            "Selected folder:\n%s\n\n"
            "ZuPather could not locate the Zuma Deluxe 'levels' folder or 'levels.xml'.\n\n"
            "Please ensure you select either:\n"
            " 1. The Zuma Deluxe root game folder (which contains 'levels' and 'images' subfolders)\n"
            " 2. The Zuma 'levels' subfolder directly (containing 'levels.xml')",
            selected_folder
        );
        show_themed_notice_dialog(
            hwnd,
            "Invalid Zuma Folder - ZuPather",
            "Zuma Deluxe Levels Folder Not Found",
            body,
            580, 360
        );
        return;
    }

    /* Rigorous validation of levels.xml */
    char err_buf[512] = {0};
    int level_count = 0;
    if (!validate_zuma_levels_xml(candidate_xml_file, err_buf, sizeof(err_buf), &level_count)) {
        char body[1024];
        snprintf(body, sizeof(body),
            "Location:\n%s\n\n"
            "Validation Error Details:\n%s\n\n"
            "ZuPather rejected this folder because the levels.xml file does not meet the structural criteria of Zuma Deluxe.",
            candidate_xml_file,
            err_buf[0] != '\0' ? err_buf : "Unknown structural error."
        );
        show_themed_notice_dialog(
            hwnd,
            "levels.xml Validation Error - ZuPather",
            "Invalid Zuma Deluxe levels.xml",
            body,
            600, 380
        );
        return;
    }

    /* Validated successfully! Save to global state and config */
    strncpy(g_zuma_levels_dir, candidate_levels_dir, sizeof(g_zuma_levels_dir) - 1);
    strncpy(g_zuma_root_dir, candidate_root_dir, sizeof(g_zuma_root_dir) - 1);
    g_has_zuma_dir = true;
    save_zuma_config();

    /* Show confirmation dialog */
    char success_body[1024];
    snprintf(success_body, sizeof(success_body),
        "Zuma Deluxe levels folder has been set and verified!\n\n"
        "Levels Directory:\n%s\n\n"
        "Root Directory:\n%s\n\n"
        "Verified levels.xml: %d level definitions found.\n\n"
        "This configuration is saved in canvas\\zupathconf.inf and ready for level import/export.",
        g_zuma_levels_dir,
        g_zuma_root_dir[0] != '\0' ? g_zuma_root_dir : "(same as levels)",
        level_count
    );
    show_themed_notice_dialog(
        hwnd,
        "Zuma Folder Set - ZuPather",
        "Zuma Deluxe Folder Confirmed",
        success_body,
        580, 360
    );

    /* Update status bar */
    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg), "Zuma Folder: %s", g_zuma_levels_dir);
    SendMessageA(g_hwnd_status, SB_SETTEXTA, 0 | SBT_OWNERDRAW | SBT_NOBORDERS, (LPARAM)status_msg);
    update_status_bar();
}

