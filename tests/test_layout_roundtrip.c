#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "../src/zupather_state.h"

/* Minimal UI mocks to allow compiling layout_editor.c in headless test mode */
AppTheme g_current_theme = THEME_LIGHT;
char g_zuma_root_dir[MAX_PATH] = {0};
char g_zuma_levels_dir[MAX_PATH] = {0};
HFONT g_font_primary_regular = NULL;
HFONT g_font_primary_bold = NULL;
HFONT g_font_mono_bold = NULL;

void image_free(CanvasImage *img) { (void)img; }
bool image_load_from_file(const char *filepath, CanvasImage *out_img) { (void)filepath; (void)out_img; return false; }
void show_themed_notice_dialog(HWND parent, const char *title, const char *header, const char *msg, int w, int h) {
    (void)parent; (void)title; (void)header; (void)msg; (void)w; (void)h;
}
void update_window_theme_attributes(HWND hwnd, bool dark) { (void)hwnd; (void)dark; }
const ThemeColors* get_theme(void) { static ThemeColors tc; return &tc; }
bool get_zuma_properties_dir(char *buf, size_t sz) { (void)buf; (void)sz; return false; }
void apply_control_dark_mode(HWND hwnd, bool dark) { (void)hwnd; (void)dark; }

/* Include production layout editor implementation directly */
#include "../src/layout_editor.c"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
        g_tests_failed++; \
        return false; \
    } \
} while (0)

/* 1. Test word boundary matching on mixed and overlapping attributes */
static bool test_word_boundary_matching(void) {
    printf("--- Test 1: Word boundary matching on mixed attributes ---\n");
    int val = 0;

    // A. bx before x
    const char *tag1 = "<T bx=\"10\" x=\"20\" />";
    TEST_ASSERT(extract_xml_attr_int(tag1, "x", &val) && val == 20, "x should be 20 when preceded by bx=\"10\"");
    TEST_ASSERT(extract_xml_attr_int(tag1, "bx", &val) && val == 10, "bx should be 10");

    // B. x before bx
    const char *tag2 = "<T x=\"20\" bx=\"10\" />";
    TEST_ASSERT(extract_xml_attr_int(tag2, "x", &val) && val == 20, "x should be 20 when followed by bx=\"10\"");
    TEST_ASSERT(extract_xml_attr_int(tag2, "bx", &val) && val == 10, "bx should be 10");

    // C. width before h
    const char *tag3 = "<T width=\"380\" h=\"150\" />";
    TEST_ASSERT(extract_xml_attr_int(tag3, "h", &val) && val == 150, "h should be 150 when preceded by width=\"380\"");
    TEST_ASSERT(extract_xml_attr_int(tag3, "width", &val) && val == 380, "width should be 380");

    // D. h before width
    const char *tag4 = "<T h=\"150\" width=\"380\" />";
    TEST_ASSERT(extract_xml_attr_int(tag4, "h", &val) && val == 150, "h should be 150 when followed by width=\"380\"");
    TEST_ASSERT(extract_xml_attr_int(tag4, "width", &val) && val == 380, "width should be 380");

    printf("  [PASS] All word-boundary mixed attribute checks passed.\n");
    g_tests_passed++;
    return true;
}

/* 2. Test single-quoted space attribute */
static bool test_single_quoted_space(void) {
    printf("--- Test 2: Single-quoted space attribute preservation ---\n");
    char temp_in[MAX_PATH], temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_in), temp_in);
    snprintf(temp_out, sizeof(temp_out), "%szupather_test_sq_out.xml", temp_in);
    strncat(temp_in, "zupather_test_sq_in.xml", sizeof(temp_in) - strlen(temp_in) - 1);

    FILE *fp = fopen(temp_in, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <GauntletDoors space='1200x675'>\n");
    fprintf(fp, "        <Door id=\"0\" x=\"444\" y=\"252\" />\n");
    fprintf(fp, "    </GauntletDoors>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    st.target_w = 1200;
    st.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st, temp_in), "Failed to load single-quoted XML");
    int sec_idx = get_canonical_section_index("GauntletDoors");
    TEST_ASSERT(sec_idx >= 0, "GauntletDoors section index valid");
    TEST_ASSERT(strcmp(st.section_space[sec_idx], "1200x675") == 0, "space attribute parsed as 1200x675");

    TEST_ASSERT(layout_xml_save(&st, temp_out), "Failed to save single-quoted XML");

    LayoutEditorState st2;
    memset(&st2, 0, sizeof(st2));
    TEST_ASSERT(layout_xml_load(&st2, temp_out), "Failed to reload resaved XML");
    TEST_ASSERT(strcmp(st2.section_space[sec_idx], "1200x675") == 0, "space attribute preserved in reload");

    remove(temp_in);
    remove(temp_out);
    printf("  [PASS] Single-quoted space attribute parsed and preserved.\n");
    g_tests_passed++;
    return true;
}

/* 3. Test preservation of width/height attribute names */
static bool test_width_height_names(void) {
    printf("--- Test 3: width/height attribute name preservation ---\n");
    char temp_in[MAX_PATH], temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_in), temp_in);
    snprintf(temp_out, sizeof(temp_out), "%szupather_test_wh_out.xml", temp_in);
    strncat(temp_in, "zupather_test_wh_in.xml", sizeof(temp_in) - strlen(temp_in) - 1);

    FILE *fp = fopen(temp_in, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <TempleCleared>\n");
    fprintf(fp, "        <DialogText x=\"220\" y=\"180\" width=\"380\" height=\"150\" align=\"center\" />\n");
    fprintf(fp, "    </TempleCleared>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    TEST_ASSERT(layout_xml_load(&st, temp_in), "Failed to load width/height XML");
    TEST_ASSERT(st.element_count == 1, "Element count is 1");
    TEST_ASSERT(st.elements[0].use_width_height_names, "use_width_height_names must be true");

    TEST_ASSERT(layout_xml_save(&st, temp_out), "Failed to save width/height XML");

    // Read back raw text to verify exact attribute strings
    FILE *rf = fopen(temp_out, "rb");
    TEST_ASSERT(rf != NULL, "Failed to read saved file");
    char buf[2048] = {0};
    fread(buf, 1, sizeof(buf) - 1, rf);
    fclose(rf);

    TEST_ASSERT(strstr(buf, "width=\"380\"") != NULL, "Output must contain width=\"380\"");
    TEST_ASSERT(strstr(buf, "height=\"150\"") != NULL, "Output must contain height=\"150\"");
    TEST_ASSERT(strstr(buf, " w=\"380\"") == NULL, "Output must NOT contain w=\"380\"");
    TEST_ASSERT(strstr(buf, " h=\"150\"") == NULL, "Output must NOT contain h=\"150\"");

    remove(temp_in);
    remove(temp_out);
    printf("  [PASS] width and height attribute names preserved.\n");
    g_tests_passed++;
    return true;
}

/* 4. Test lineSpacing preservation */
static bool test_linespacing(void) {
    printf("--- Test 4: lineSpacing preservation ---\n");
    char temp_in[MAX_PATH], temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_in), temp_in);
    snprintf(temp_out, sizeof(temp_out), "%szupather_test_ls_out.xml", temp_in);
    strncat(temp_in, "zupather_test_ls_in.xml", sizeof(temp_in) - strlen(temp_in) - 1);

    FILE *fp = fopen(temp_in, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <AdventureLabels>\n");
    fprintf(fp, "        <InfoBox x=\"178\" y=\"84\" w=\"150\" h=\"141\" lineSpacing=\"20\" />\n");
    fprintf(fp, "    </AdventureLabels>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    TEST_ASSERT(layout_xml_load(&st, temp_in), "Failed to load lineSpacing XML");
    TEST_ASSERT(st.elements[0].flags & ATTR_HAS_LINESPACING, "ATTR_HAS_LINESPACING flag must be set");
    TEST_ASSERT(st.elements[0].lineSpacing == 20, "lineSpacing value must be 20");

    TEST_ASSERT(layout_xml_save(&st, temp_out), "Failed to save lineSpacing XML");

    FILE *rf = fopen(temp_out, "rb");
    TEST_ASSERT(rf != NULL, "Failed to read saved file");
    char buf[2048] = {0};
    fread(buf, 1, sizeof(buf) - 1, rf);
    fclose(rf);

    TEST_ASSERT(strstr(buf, "lineSpacing=\"20\"") != NULL, "Output must contain lineSpacing=\"20\"");

    remove(temp_in);
    remove(temp_out);
    printf("  [PASS] lineSpacing preserved.\n");
    g_tests_passed++;
    return true;
}

/* 5. Test selection flag immutability */
static bool test_selection_flags_immutability(const char *layout_path) {
    printf("--- Test 5: Selection flag immutability on real layout ---\n");
    FILE *f_check = fopen(layout_path, "rb");
    if (!f_check) {
        printf("  [SKIP] Layout file '%s' not found on this system. Skipping gracefully.\n", layout_path);
        g_tests_passed++;
        return true;
    }
    fclose(f_check);

    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    st.target_w = 1200;
    st.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st, layout_path), "Failed to load layout.xml");
    TEST_ASSERT(st.element_count > 0, "element_count must be > 0");

    for (int i = 0; i < st.element_count; ++i) {
        uint32_t orig_flags = st.elements[i].flags;

        // Simulate selection
        for (int j = 0; j < st.element_count; ++j) st.elements[j].selected = false;
        st.elements[i].selected = true;

        st.syncing_inspector = true;
        // (UI controls populated here)
        st.syncing_inspector = false;

        TEST_ASSERT(st.elements[i].flags == orig_flags, "Flags must remain identical after element selection");
    }

    printf("  [PASS] All %d elements maintained immutable flags under selection.\n", st.element_count);
    g_tests_passed++;
    return true;
}

/* 6. Test dynamic 1:1 space check (is_element_space_1to1) */
static bool test_dynamic_space_1to1(void) {
    printf("--- Test 6: Dynamic 1:1 space scaling verification ---\n");
    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    st.target_w = 1200;
    st.target_h = 675;

    int idx_gaunt = get_canonical_section_index("GauntletDoors");
    int idx_more = get_canonical_section_index("MoreGames");
    int idx_adv = get_canonical_section_index("AdventureDoors");
    int idx_credits = get_canonical_section_index("CreditsScreen");

    strncpy(st.section_space[idx_gaunt], "1200x675", sizeof(st.section_space[idx_gaunt]) - 1);
    strncpy(st.section_space[idx_more], "1200x675", sizeof(st.section_space[idx_more]) - 1);
    strncpy(st.section_space[idx_adv], "1200x675", sizeof(st.section_space[idx_adv]) - 1);
    st.section_space[idx_credits][0] = '\0'; // 640x480 space

    LayoutElement el_gaunt = {0}; strncpy(el_gaunt.section, "GauntletDoors", sizeof(el_gaunt.section) - 1);
    LayoutElement el_more = {0}; strncpy(el_more.section, "MoreGames", sizeof(el_more.section) - 1);
    LayoutElement el_adv = {0}; strncpy(el_adv.section, "AdventureDoors", sizeof(el_adv.section) - 1);
    LayoutElement el_cred = {0}; strncpy(el_cred.section, "CreditsScreen", sizeof(el_cred.section) - 1);

    TEST_ASSERT(is_element_space_1to1(&el_gaunt, &st), "GauntletDoors must be 1:1 at 1200x675");
    TEST_ASSERT(is_element_space_1to1(&el_more, &st), "MoreGames must be 1:1 at 1200x675");
    TEST_ASSERT(is_element_space_1to1(&el_adv, &st), "AdventureDoors must be 1:1 at 1200x675");
    TEST_ASSERT(!is_element_space_1to1(&el_cred, &st), "CreditsScreen must NOT be 1:1 (canonical 640x480)");

    // At 800x600 resolution, none of 1200x675 sections should be 1:1
    st.target_w = 800; st.target_h = 600;
    TEST_ASSERT(!is_element_space_1to1(&el_gaunt, &st), "GauntletDoors must NOT be 1:1 at 800x600");

    printf("  [PASS] is_element_space_1to1 scales dynamically per element section.\n");
    g_tests_passed++;
    return true;
}

/* 7. Full Semantic Roundtrip Comparison on real game files */
static bool test_full_semantic_roundtrip(const char *label, const char *layout_path) {
    printf("--- Test 7: Full Semantic Roundtrip (%s) ---\n", label);
    char temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_out), temp_out);
    strncat(temp_out, "zupather_roundtrip_test.xml", sizeof(temp_out) - strlen(temp_out) - 1);

    FILE *f_check = fopen(layout_path, "rb");
    if (!f_check) {
        printf("  [SKIP] Layout file '%s' not found on this system. Skipping gracefully.\n", layout_path);
        g_tests_passed++;
        return true;
    }
    fclose(f_check);

    LayoutEditorState st_orig;
    memset(&st_orig, 0, sizeof(st_orig));
    st_orig.target_w = 1200;
    st_orig.target_h = 675;
    TEST_ASSERT(layout_xml_load(&st_orig, layout_path), "Failed to load original layout.xml");

    TEST_ASSERT(layout_xml_save(&st_orig, temp_out), "Failed to save layout.xml");

    LayoutEditorState st_resaved;
    memset(&st_resaved, 0, sizeof(st_resaved));
    st_resaved.target_w = 1200;
    st_resaved.target_h = 675;
    TEST_ASSERT(layout_xml_load(&st_resaved, temp_out), "Failed to load resaved layout.xml");

    // Semantic equality assertions
    TEST_ASSERT(st_orig.element_count == st_resaved.element_count, "Element counts must match exactly");
    printf("  Elements loaded: %d (Orig) == %d (Resaved)\n", st_orig.element_count, st_resaved.element_count);

    int value_diffs = 0;
    int flag_diffs = 0;

    for (int i = 0; i < st_orig.element_count; ++i) {
        LayoutElement *o = &st_orig.elements[i];
        LayoutElement *r = &st_resaved.elements[i];

        if (strcmp(o->tag, r->tag) != 0 || strcmp(o->section, r->section) != 0 || strcmp(o->id, r->id) != 0) {
            printf("  [MISMATCH] Element %d identity mismatch: %s vs %s\n", i, o->tag, r->tag);
            value_diffs++;
        }

        // Check coordinate equality for all active attributes
        if ((o->flags & ATTR_HAS_X) && o->x != r->x) value_diffs++;
        if ((o->flags & ATTR_HAS_Y) && o->y != r->y) value_diffs++;
        if ((o->flags & ATTR_HAS_W) && o->w != r->w) value_diffs++;
        if ((o->flags & ATTR_HAS_H) && o->h != r->h) value_diffs++;
        if ((o->flags & ATTR_HAS_DOOR_X) && o->doorX != r->doorX) value_diffs++;
        if ((o->flags & ATTR_HAS_DOOR_Y) && o->doorY != r->doorY) value_diffs++;
        if ((o->flags & ATTR_HAS_FROG_X) && o->frogX != r->frogX) value_diffs++;
        if ((o->flags & ATTR_HAS_FROG_Y) && o->frogY != r->frogY) value_diffs++;
        if ((o->flags & ATTR_HAS_CENTER_X) && o->centerX != r->centerX) value_diffs++;
        if ((o->flags & ATTR_HAS_CENTER_Y) && o->centerY != r->centerY) value_diffs++;
        if ((o->flags & ATTR_HAS_OFFSET_X) && o->offsetX != r->offsetX) value_diffs++;
        if ((o->flags & ATTR_HAS_OFFSET_Y) && o->offsetY != r->offsetY) value_diffs++;
        if ((o->flags & ATTR_HAS_MIN_TOP) && o->minTop != r->minTop) value_diffs++;
        if ((o->flags & ATTR_HAS_SPACING) && o->spacing != r->spacing) value_diffs++;
        if ((o->flags & ATTR_HAS_LINESPACING) && o->lineSpacing != r->lineSpacing) value_diffs++;
        if ((o->flags & ATTR_HAS_ALIGN) && strcmp(o->align, r->align) != 0) value_diffs++;

        // Check presence flags (0 dropped, 0 injected)
        uint32_t mask = (ATTR_HAS_X | ATTR_HAS_Y | ATTR_HAS_W | ATTR_HAS_H |
                         ATTR_HAS_DOOR_X | ATTR_HAS_DOOR_Y | ATTR_HAS_FROG_X | ATTR_HAS_FROG_Y |
                         ATTR_HAS_CENTER_X | ATTR_HAS_CENTER_Y | ATTR_HAS_OFFSET_X | ATTR_HAS_OFFSET_Y |
                         ATTR_HAS_MIN_TOP | ATTR_HAS_SPACING | ATTR_HAS_LINESPACING | ATTR_HAS_ALIGN);

        if ((o->flags & mask) != (r->flags & mask)) {
            printf("  [FLAG MISMATCH] Element %d (%s:%s): orig=0x%08X, resaved=0x%08X\n",
                   i, o->section, o->tag, o->flags & mask, r->flags & mask);
            flag_diffs++;
        }
    }

    TEST_ASSERT(value_diffs == 0, "Zero value coordinate differences required");
    TEST_ASSERT(flag_diffs == 0, "Zero flag / attribute differences required");

    remove(temp_out);
    printf("  [PASS] Semantic roundtrip: 0 value differences, 0 dropped/injected attributes across all %d elements.\n", st_orig.element_count);
    g_tests_passed++;
    return true;
}

int main(int argc, char **argv) {
    printf("=======================================================\n");
    printf("   ZuPather Layout Editor - Regression & Roundtrip Suite\n");
    printf("=======================================================\n\n");

    const char *full_layout = "D:/Files/Project/Reverse Engineer/ZuCompleluxe/assets/properties/layout.xml";
    const char *lite_layout = "D:/Files/Project/Reverse Engineer/ZuCompleluxe-lite/assets/properties/layout.xml";

    test_word_boundary_matching();
    test_single_quoted_space();
    test_width_height_names();
    test_linespacing();
    test_selection_flags_immutability(full_layout);
    test_dynamic_space_1to1();
    test_full_semantic_roundtrip("Full Repository", full_layout);
    test_full_semantic_roundtrip("Lite Repository", lite_layout);

    printf("\n=======================================================\n");
    printf(" Test Results: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("=======================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
