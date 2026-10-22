#ifndef UNDO_REDO_H
#define UNDO_REDO_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_UNDO_STACK 256

/* Initialize in-memory undo subsystem (up to 256 turns capacity, zero disk cache) */
void undo_redo_init(void);

/* Set runtime undo history limit (10 to 256 turns) and prune older states if needed */
void undo_set_history_limit(int limit);

/* Ensure at least one initial/baseline state is saved before any modification */
void undo_ensure_baseline(void);

/* Capture a new undo snapshot after an operation */
void undo_snapshot_capture(const char *action_name);

/* Check if undo or redo are available */
bool undo_can_undo(void);
bool undo_can_redo(void);

/* Get display names of actions for menu labels */
const char *undo_get_undo_name(void);
const char *undo_get_redo_name(void);

/* Perform undo or redo operations */
bool undo_perform(HWND hwnd_main);
bool undo_redo_perform(HWND hwnd_main);

/* Update Edit menu items (Undo / Redo state and accelerator labels) */
void undo_redo_update_menu(HWND hwnd_main);

/* Populate and refresh the Action History submenu items */
void undo_redo_populate_history_menu(HMENU hhistory);

/* Jump directly to a specific state in the action history */
bool undo_jump_to_index(int target_idx, HWND hwnd_main);

/* Reset all undo history (e.g. when opening a brand new project session) */
void undo_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* UNDO_REDO_H */
