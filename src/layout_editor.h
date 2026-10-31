#ifndef LAYOUT_EDITOR_H
#define LAYOUT_EDITOR_H

#if defined(_WIN32) && !defined(__APPLE__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include "compat/win32_macos_shim.h"
#endif

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check whether properties/layout.xml is available in the linked Zuma folder */
bool layout_editor_is_available(char *out_path, size_t max_len);

/* Show the interactive Layout Editor dialog window */
void show_layout_editor_dialog(HWND parent_hwnd);

#ifdef __cplusplus
}
#endif

#endif /* LAYOUT_EDITOR_H */
