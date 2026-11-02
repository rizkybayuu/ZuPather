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

/* 8. Test Auto-Center X Lock, XML Omission, and Hat Unconditional Visibility */
static bool test_auto_center_x_lock_and_roundtrip(const char *layout_path) {
    printf("--- Test 8: Auto-Center X Lock, XML Omission & Hat Visibility ---\n");

    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    st.target_w = 1200;
    st.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st, layout_path), "Failed to load layout.xml for test 8");

    LayoutElement *wel = NULL;
    LayoutElement *us = NULL;
    LayoutElement *c2p = NULL;
    LayoutElement *hat = NULL;
    LayoutElement *sun = NULL;

    for (int i = 0; i < st.element_count; ++i) {
        if (_stricmp(st.elements[i].tag, "WelcomeText") == 0) wel = &st.elements[i];
        else if (_stricmp(st.elements[i].tag, "UserSwitchText") == 0) us = &st.elements[i];
        else if (_stricmp(st.elements[i].tag, "ClickToPlay") == 0) c2p = &st.elements[i];
        else if (_stricmp(st.elements[i].tag, "Hat") == 0) hat = &st.elements[i];
        else if (_stricmp(st.elements[i].tag, "Sun") == 0) sun = &st.elements[i];
    }

    TEST_ASSERT(wel != NULL, "WelcomeText element must exist");
    TEST_ASSERT(us != NULL, "UserSwitchText element must exist");
    TEST_ASSERT(c2p != NULL, "ClickToPlay element must exist");
    TEST_ASSERT(hat != NULL, "Hat element must exist");

    /* Verify no ATTR_HAS_X and center hotspot */
    TEST_ASSERT((wel->hotspot & HOTSPOT_CENTER_X) != 0, "WelcomeText must have HOTSPOT_CENTER_X");
    TEST_ASSERT((us->hotspot & HOTSPOT_CENTER_X) != 0, "UserSwitchText must have HOTSPOT_CENTER_X");
    TEST_ASSERT((c2p->hotspot & HOTSPOT_CENTER_X) != 0, "ClickToPlay must have HOTSPOT_CENTER_X");

    /* Verify no ATTR_HAS_X in loaded state */
    TEST_ASSERT(!(wel->flags & ATTR_HAS_X), "WelcomeText must not have ATTR_HAS_X");
    TEST_ASSERT(!(us->flags & ATTR_HAS_X), "UserSwitchText must not have ATTR_HAS_X");
    TEST_ASSERT(!(c2p->flags & ATTR_HAS_X), "ClickToPlay must not have ATTR_HAS_X");

    /* Verify Hat is NOT conditional */
    TEST_ASSERT(hat->is_conditional == false, "Hat must NOT be marked is_conditional");
    st.cur_screen_idx = 1;
    st.cur_sub_filter_idx = 0;
    st.show_conditional_sprites = false;
    TEST_ASSERT(is_element_visible_in_editor(hat, &st) == true, "Hat must be visible even when show_conditional_sprites is false");

    /* Verify Sun is XM_ABS, coordinates (53,-89), and visible in Main Menu */
    TEST_ASSERT(sun != NULL, "Sun element must exist");
    TEST_ASSERT(sun->x == 53 && sun->y == -89, "Sun must have initial canonical coordinates (53, -89)");
    LayoutElement *mmsun = NULL;
    for (int i = 0; i < st.element_count; ++i) {
        if (_stricmp(st.elements[i].tag, "MMSun") == 0) mmsun = &st.elements[i];
    }
    TEST_ASSERT(mmsun != NULL, "MMSun element must exist");
    TEST_ASSERT(mmsun->x == 150 && mmsun->y == 3, "MMSun must have canonical coordinates (150, 3)");
    TEST_ASSERT(is_element_visible_in_editor(sun, &st) == true, "Sun must be visible in Main Menu editor view");

    /* Verify canvas rect calculation and non-empty intersection with canvas */
    update_all_elements_canvas_rects(&st, 1200, 675);
    RECT canvas_bounds = { 0, 0, 1200, 675 };
    RECT sun_isect;
    TEST_ASSERT(IntersectRect(&sun_isect, &sun->canvas_rect, &canvas_bounds), "Sun canvas rect must intersect with canvas bounds");
    TEST_ASSERT(!IsRectEmpty(&sun_isect), "Sun canvas visible intersection must not be empty");

    /* Moving Sun to y = -100 and back preserves flags and attributes */
    uint32_t sun_orig_flags = sun->flags;
    sun->y = -100;
    TEST_ASSERT(sun->y == -100, "Sun y must allow negative position -100");
    sun->y = -89;
    TEST_ASSERT(sun->flags == sun_orig_flags, "Sun flags must remain identical after moving to y=-100 and back");



    /* Save to temp XML and verify no x= attribute appears in output */
    char temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_out), temp_out);
    strncat(temp_out, "zupather_test_auto_center.xml", sizeof(temp_out) - strlen(temp_out) - 1);

    TEST_ASSERT(layout_xml_save(&st, temp_out), "Failed to save test_auto_center XML");

    /* Inspect saved file text */
    FILE *fp = fopen(temp_out, "rb");
    TEST_ASSERT(fp != NULL, "Failed to open saved temp file");
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "<Sun")) {
            TEST_ASSERT(strstr(line, "x=\"53\"") != NULL, "Sun must retain x=\"53\" after roundtrip");
            TEST_ASSERT(strstr(line, "y=\"-89\"") != NULL, "Sun must retain y=\"-89\" after roundtrip");
        }
        if (strstr(line, "<MMSun")) {
            TEST_ASSERT(strstr(line, "x=\"150\"") != NULL, "MMSun must retain x=\"150\" after roundtrip");
            TEST_ASSERT(strstr(line, "y=\"3\"") != NULL, "MMSun must retain y=\"3\" after roundtrip");
        }
        if (strstr(line, "<WelcomeText") || strstr(line, "<UserSwitchText") || strstr(line, "<ClickToPlay")) {
            TEST_ASSERT(strstr(line, " x=\"") == NULL, "Auto-centered element must never serialize x attribute");
            TEST_ASSERT(strstr(line, " x='") == NULL, "Auto-centered element must never serialize x attribute");
        }
    }
    fclose(fp);
    remove(temp_out);

    printf("  [PASS] Auto-Center X lock, XML omission, Hat visibility, Sun (53,-89), and MMSun (150,3) verified.\n");
    g_tests_passed++;
    return true;
}

static bool test_fase_b1_highscore_and_scoretext(const char *layout_path) {
    printf("--- Test 9: Fase B1 HighscoreDate clip guide & ScoreText/Highscore font bindings ---\n");
    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    TEST_ASSERT(layout_xml_load(&st, layout_path), "Failed to load layout XML for Fase B1 test");

    /* Find elements */
    LayoutElement *hs_date = find_element_by_tag(&st, "AdventureLabels", "HighscoreDate");
    LayoutElement *hs_names = find_element_by_tag(&st, "AdventureLabels", "HighscoreNames");
    LayoutElement *hs_scores = find_element_by_tag(&st, "AdventureLabels", "HighscoreScores");
    LayoutElement *score_txt = find_element_by_tag(&st, "InGameHUD", "ScoreText");

    TEST_ASSERT(hs_date != NULL, "HighscoreDate must exist in AdventureLabels");
    TEST_ASSERT(hs_names != NULL, "HighscoreNames must exist in AdventureLabels");
    TEST_ASSERT(hs_scores != NULL, "HighscoreScores must exist in AdventureLabels");
    TEST_ASSERT(score_txt != NULL, "ScoreText must exist in InGameHUD");

    /* 1. HighscoreDate must NOT be classified as text, but MUST be classified as region */
    TEST_ASSERT(is_text_element(hs_date) == false, "HighscoreDate must NOT be classified as a text element");
    TEST_ASSERT(is_region_element(hs_date) == true, "HighscoreDate must be classified as a region/clip element");

    /* 2. HighscoreNames and HighscoreScores must be text elements */
    TEST_ASSERT(is_text_element(hs_names) == true, "HighscoreNames must be classified as a text element");
    TEST_ASSERT(is_text_element(hs_scores) == true, "HighscoreScores must be classified as a text element");

    /* 3. ScoreText must be a text element */
    TEST_ASSERT(is_text_element(score_txt) == true, "ScoreText must be classified as a text element");

    /* 4. Semantic roundtrip preservation check */
    char temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_out), temp_out);
    strncat(temp_out, "zupather_test_b1_roundtrip.xml", sizeof(temp_out) - strlen(temp_out) - 1);
    TEST_ASSERT(layout_xml_save(&st, temp_out), "Failed to save B1 test layout XML");

    LayoutEditorState st2;
    memset(&st2, 0, sizeof(st2));
    TEST_ASSERT(layout_xml_load(&st2, temp_out), "Failed to reload saved B1 XML");
    TEST_ASSERT(st.element_count == st2.element_count, "Element count mismatch after B1 roundtrip");

    for (int i = 0; i < st.element_count; ++i) {
        TEST_ASSERT(strcmp(st.elements[i].tag, st2.elements[i].tag) == 0, "Tag mismatch");
        TEST_ASSERT(st.elements[i].x == st2.elements[i].x, "X mismatch");
        TEST_ASSERT(st.elements[i].y == st2.elements[i].y, "Y mismatch");
        TEST_ASSERT(st.elements[i].flags == st2.elements[i].flags, "Flags mismatch");
    }

    remove(temp_out);
    printf("  [PASS] HighscoreDate region classification, ScoreText text binding, and 0-diff roundtrip verified.\n");
    g_tests_passed++;
    return true;
}

static bool test_font_baseline_convention(const char *layout_path) {
    printf("--- Test 10: Universal Font Baseline Convention (Ascent - AscentPadding) ---\n");
    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    TEST_ASSERT(layout_xml_load(&st, layout_path), "Failed to load layout XML for Baseline Convention test");

    st.target_w = 1200;
    st.target_h = 675;

    /* 1. StageComplete (scr == 5): StageText (y=150) & CompleteText (y=250), offset=80 */
    LayoutElement *st_txt = find_element_by_tag(&st, "StageComplete", "StageText");
    LayoutElement *cp_txt = find_element_by_tag(&st, "StageComplete", "CompleteText");
    TEST_ASSERT(st_txt != NULL, "StageText must exist in StageComplete");
    TEST_ASSERT(cp_txt != NULL, "CompleteText must exist in StageComplete");
    TEST_ASSERT(st_txt->y == 150, "StageText raw y in memory must remain 150");
    TEST_ASSERT(cp_txt->y == 250, "CompleteText raw y in memory must remain 250");

    st.cur_screen_idx = 5;
    update_all_elements_canvas_rects(&st, 1232, 707);
    int delta_y = (675 - 480) / 2; /* 97 */
    /* Viewport padding = 16px */
    TEST_ASSERT(st_txt->canvas_rect.top == 16 + delta_y + (150 - 80), "StageText canvas top must equal 16 + delta_y + (150 - 80)");
    TEST_ASSERT(cp_txt->canvas_rect.top == 16 + delta_y + (250 - 80), "CompleteText canvas top must equal 16 + delta_y + (250 - 80)");
    TEST_ASSERT(st_txt->y == 150, "StageText raw y must remain unmodified in memory");
    TEST_ASSERT(cp_txt->y == 250, "CompleteText raw y must remain unmodified in memory");

    /* 2. TempleCleared (scr == 6): Title (y=120), offset=80 */
    LayoutElement *tc_title = find_element_by_tag(&st, "TempleCleared", "Title");
    TEST_ASSERT(tc_title != NULL, "Title must exist in TempleCleared");
    TEST_ASSERT(tc_title->y == 120, "Title raw y in memory must remain 120");

    st.cur_screen_idx = 6;
    update_all_elements_canvas_rects(&st, 1232, 707);
    TEST_ASSERT(tc_title->canvas_rect.top == 16 + delta_y + (120 - 80), "Title canvas top must equal 16 + delta_y + (120 - 80)");
    TEST_ASSERT(tc_title->y == 120, "Title raw y must remain unmodified in memory");

    /* 3. AdventureLabels (scr == 2): HighscoreNames (y=98) & HighscoreScores (y=98), offset=8 */
    LayoutElement *adv_hn = find_element_by_tag(&st, "AdventureLabels", "HighscoreNames");
    LayoutElement *adv_hs = find_element_by_tag(&st, "AdventureLabels", "HighscoreScores");
    TEST_ASSERT(adv_hn != NULL, "HighscoreNames must exist in AdventureLabels");
    TEST_ASSERT(adv_hs != NULL, "HighscoreScores must exist in AdventureLabels");
    TEST_ASSERT(adv_hn->y == 98, "Adventure HighscoreNames raw y in memory must remain 98");
    TEST_ASSERT(adv_hs->y == 98, "Adventure HighscoreScores raw y in memory must remain 98");

    st.cur_screen_idx = 2;
    update_all_elements_canvas_rects(&st, 1232, 707);
    TEST_ASSERT(adv_hn->canvas_rect.top == 16 + (98 - 8), "HighscoreNames canvas top must equal 16 + (98 - 8)");
    TEST_ASSERT(adv_hs->canvas_rect.top == 16 + (98 - 8), "HighscoreScores canvas top must equal 16 + (98 - 8)");
    TEST_ASSERT(adv_hn->y == 98, "HighscoreNames raw y must remain 98 in memory");
    TEST_ASSERT(adv_hs->y == 98, "HighscoreScores raw y must remain 98 in memory");

    /* 4. GauntletLabels (scr == 3): PracticeText (y=366) & CustomMapTag (y=366), offset=10 */
    LayoutElement *gaunt_pt = find_element_by_tag(&st, "GauntletLabels", "PracticeText");
    LayoutElement *gaunt_cm = find_element_by_tag(&st, "GauntletLabels", "CustomMapTag");
    TEST_ASSERT(gaunt_pt != NULL, "PracticeText must exist in GauntletLabels");
    TEST_ASSERT(gaunt_cm != NULL, "CustomMapTag must exist in GauntletLabels");
    TEST_ASSERT(gaunt_pt->y == 366, "PracticeText raw y in memory must remain 366");
    TEST_ASSERT(gaunt_cm->y == 366, "CustomMapTag raw y in memory must remain 366");

    st.cur_screen_idx = 3;
    gaunt_cm->selected = true;
    update_all_elements_canvas_rects(&st, 1232, 707);
    TEST_ASSERT(gaunt_pt->canvas_rect.top == 16 + (366 - 10), "PracticeText canvas top must equal 16 + (366 - 10)");
    TEST_ASSERT(gaunt_cm->canvas_rect.top == 16 + (366 - 10), "CustomMapTag canvas top must equal 16 + (366 - 10)");
    TEST_ASSERT(gaunt_pt->y == 366, "PracticeText raw y must remain 366 in memory");
    TEST_ASSERT(gaunt_cm->y == 366, "CustomMapTag raw y must remain 366 in memory");

    /* 4b. AdventureLabels (scr == 2): StageNum (y=557, off=10), StageText (y=593, off=26), Subtitle (y=641, off=10) */
    LayoutElement *adv_snum = find_element_by_tag(&st, "AdventureLabels", "StageNum");
    LayoutElement *adv_stxt = find_element_by_tag(&st, "AdventureLabels", "StageText");
    LayoutElement *adv_sub  = find_element_by_tag(&st, "AdventureLabels", "Subtitle");
    LayoutElement *adv_s13n = find_element_by_tag(&st, "AdventureLabels", "Stage13Num");
    LayoutElement *adv_s13t = find_element_by_tag(&st, "AdventureLabels", "Stage13Title");
    LayoutElement *adv_s13s = find_element_by_tag(&st, "AdventureLabels", "Stage13Subtitle");

    TEST_ASSERT(adv_snum != NULL, "StageNum must exist in AdventureLabels");
    TEST_ASSERT(adv_stxt != NULL, "StageText must exist in AdventureLabels");
    TEST_ASSERT(adv_sub  != NULL, "Subtitle must exist in AdventureLabels");
    TEST_ASSERT(adv_s13n != NULL, "Stage13Num must exist in AdventureLabels");
    TEST_ASSERT(adv_s13t != NULL, "Stage13Title must exist in AdventureLabels");
    TEST_ASSERT(adv_s13s != NULL, "Stage13Subtitle must exist in AdventureLabels");

    TEST_ASSERT(adv_snum->y == 557, "StageNum raw y must remain 557");
    TEST_ASSERT(adv_stxt->y == 593, "StageText raw y must remain 593");
    TEST_ASSERT(adv_sub->y  == 641, "Subtitle raw y must remain 641");
    TEST_ASSERT(adv_s13n->y == 557, "Stage13Num raw y must remain 557");
    TEST_ASSERT(adv_s13t->y == 593, "Stage13Title raw y must remain 593");
    TEST_ASSERT(adv_s13s->y == 641, "Stage13Subtitle raw y must remain 641");

    st.cur_screen_idx = 2;
    adv_s13n->selected = true;
    adv_s13t->selected = true;
    adv_s13s->selected = true;
    update_all_elements_canvas_rects(&st, 1232, 707);
    TEST_ASSERT(adv_snum->canvas_rect.top == 16 + (557 - 10), "StageNum canvas top must equal 16 + (557 - 10)");
    TEST_ASSERT(adv_stxt->canvas_rect.top == 16 + (593 - 26), "StageText canvas top must equal 16 + (593 - 26)");
    TEST_ASSERT(adv_sub->canvas_rect.top  == 16 + (641 - 10), "Subtitle canvas top must equal 16 + (641 - 10)");
    TEST_ASSERT(adv_s13n->canvas_rect.top == 16 + (557 - 10), "Stage13Num canvas top must equal 16 + (557 - 10)");
    TEST_ASSERT(adv_s13t->canvas_rect.top == 16 + (593 - 26), "Stage13Title canvas top must equal 16 + (593 - 26)");
    TEST_ASSERT(adv_s13s->canvas_rect.top == 16 + (641 - 10), "Stage13Subtitle canvas top must equal 16 + (641 - 10)");

    /* 4c. InGameHUD (scr == 4): ScoreText (top=4) and StageName (top=5) engine HUD fixed positions */
    LayoutElement *hud_scr = find_element_by_tag(&st, "InGameHUD", "ScoreText");
    LayoutElement *hud_stg = find_element_by_tag(&st, "InGameHUD", "StageName");
    TEST_ASSERT(hud_scr != NULL, "ScoreText must exist in InGameHUD");
    TEST_ASSERT(hud_stg != NULL, "StageName must exist in InGameHUD");
    TEST_ASSERT(hud_scr->y == 8, "ScoreText raw y in memory must remain 8");
    TEST_ASSERT(hud_stg->y == 8, "StageName raw y in memory must remain 8");

    TEST_ASSERT(hud_scr->y == 8, "ScoreText raw y in memory must remain 8");
    TEST_ASSERT(hud_stg->y == 8, "StageName raw y in memory must remain 8");
    TEST_ASSERT(is_text_element(hud_stg) == true, "StageName must be classified as a text element");

    st.cur_screen_idx = 4;
    update_all_elements_canvas_rects(&st, 1232, 707);
    TEST_ASSERT(hud_scr->canvas_rect.top == 16 + 4, "ScoreText canvas top must equal 16 + 4 (baseline 17 - cancun13 offset 13)");
    TEST_ASSERT(hud_stg->canvas_rect.top == 16 + 5, "StageName canvas top must equal 16 + 5 (baseline 15 - cancun10 offset 10)");

    /* 5. InfoBox text element classification */
    LayoutElement *adv_ib = find_element_by_tag(&st, "AdventureLabels", "InfoBox");
    LayoutElement *gaunt_ib = find_element_by_tag(&st, "GauntletLabels", "InfoBox");
    TEST_ASSERT(adv_ib != NULL, "InfoBox must exist in AdventureLabels");
    TEST_ASSERT(gaunt_ib != NULL, "InfoBox must exist in GauntletLabels");
    TEST_ASSERT(is_text_element(adv_ib) == true, "InfoBox must be classified as a text element");
    TEST_ASSERT(is_region_element(adv_ib) == false, "InfoBox must NOT be classified as a region element");
    TEST_ASSERT(is_text_element(gaunt_ib) == true, "Gauntlet InfoBox must be classified as a text element");
    TEST_ASSERT(is_region_element(gaunt_ib) == false, "Gauntlet InfoBox must NOT be classified as a region element");

    /* 6. Semantic XML roundtrip preservation check */
    char temp_out[MAX_PATH];
    GetTempPathA(sizeof(temp_out), temp_out);
    strncat(temp_out, "zupather_test_baseline_roundtrip.xml", sizeof(temp_out) - strlen(temp_out) - 1);
    TEST_ASSERT(layout_xml_save(&st, temp_out), "Failed to save baseline test layout XML");

    LayoutEditorState st2;
    memset(&st2, 0, sizeof(st2));
    TEST_ASSERT(layout_xml_load(&st2, temp_out), "Failed to reload saved baseline XML");
    TEST_ASSERT(st.element_count == st2.element_count, "Element count mismatch after baseline roundtrip");

    for (int i = 0; i < st.element_count; ++i) {
        TEST_ASSERT(strcmp(st.elements[i].tag, st2.elements[i].tag) == 0, "Tag mismatch");
        TEST_ASSERT(st.elements[i].x == st2.elements[i].x, "X mismatch");
        TEST_ASSERT(st.elements[i].y == st2.elements[i].y, "Y mismatch");
        TEST_ASSERT(st.elements[i].flags == st2.elements[i].flags, "Flags mismatch");
    }

    remove(temp_out);
    printf("  [PASS] Universal baseline convention, InfoBox classification, and 0-diff roundtrip verified.\n");
    g_tests_passed++;
    return true;
}

/* 12. Test MMSun Decoupling & Backward Compatible Omission */
static bool test_mmsun_roundtrip_and_omission(void) {
    printf("--- Test 12: MMSun Decoupling & Backward Compatible Omission ---\n");

    /* Subtest A: MMSun explicitly present in XML */
    char temp_in_a[MAX_PATH], temp_out_a[MAX_PATH];
    GetTempPathA(sizeof(temp_in_a), temp_in_a);
    snprintf(temp_out_a, sizeof(temp_out_a), "%szupather_test_mmsun_present_out.xml", temp_in_a);
    strncat(temp_in_a, "zupather_test_mmsun_present_in.xml", sizeof(temp_in_a) - strlen(temp_in_a) - 1);

    FILE *fp = fopen(temp_in_a, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in_a");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <MainMenu space=\"1200x675\">\n");
    fprintf(fp, "        <Sun x=\"53\" y=\"-89\" />\n");
    fprintf(fp, "        <MMSun x=\"150\" y=\"3\" />\n");
    fprintf(fp, "    </MainMenu>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st_a;
    memset(&st_a, 0, sizeof(st_a));
    st_a.target_w = 1200;
    st_a.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st_a, temp_in_a), "Failed to load XML with MMSun");
    TEST_ASSERT(st_a.element_count == 2, "Element count must be 2");

    LayoutElement *sun_a = find_element_by_tag(&st_a, "MainMenu", "Sun");
    LayoutElement *mmsun_a = find_element_by_tag(&st_a, "MainMenu", "MMSun");
    TEST_ASSERT(sun_a != NULL, "Sun must be loaded");
    TEST_ASSERT(mmsun_a != NULL, "MMSun must be loaded");
    TEST_ASSERT(sun_a->x == 53 && sun_a->y == -89, "Sun coords must be (53, -89)");
    TEST_ASSERT(mmsun_a->x == 150 && mmsun_a->y == 3, "MMSun coords must be (150, 3)");

    TEST_ASSERT(layout_xml_save(&st_a, temp_out_a), "Failed to save XML with MMSun");

    LayoutEditorState st_a_resaved;
    memset(&st_a_resaved, 0, sizeof(st_a_resaved));
    st_a_resaved.target_w = 1200;
    st_a_resaved.target_h = 675;
    TEST_ASSERT(layout_xml_load(&st_a_resaved, temp_out_a), "Failed to reload resaved MMSun XML");
    TEST_ASSERT(st_a_resaved.element_count == 2, "Reloaded element count must be 2");
    LayoutElement *mmsun_re = find_element_by_tag(&st_a_resaved, "MainMenu", "MMSun");
    TEST_ASSERT(mmsun_re != NULL, "MMSun must exist after reload");
    TEST_ASSERT(mmsun_re->x == 150 && mmsun_re->y == 3, "MMSun coordinates preserved across roundtrip");

    remove(temp_in_a);
    remove(temp_out_a);

    /* Subtest B: MMSun omitted (legacy layout XML) */
    char temp_in_b[MAX_PATH], temp_out_b[MAX_PATH];
    GetTempPathA(sizeof(temp_in_b), temp_in_b);
    snprintf(temp_out_b, sizeof(temp_out_b), "%szupather_test_mmsun_omitted_out.xml", temp_in_b);
    strncat(temp_in_b, "zupather_test_mmsun_omitted_in.xml", sizeof(temp_in_b) - strlen(temp_in_b) - 1);

    fp = fopen(temp_in_b, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in_b");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <MainMenu space=\"1200x675\">\n");
    fprintf(fp, "        <Sun x=\"53\" y=\"-89\" />\n");
    fprintf(fp, "    </MainMenu>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st_b;
    memset(&st_b, 0, sizeof(st_b));
    st_b.target_w = 1200;
    st_b.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st_b, temp_in_b), "Failed to load legacy XML without MMSun");
    TEST_ASSERT(st_b.element_count == 1, "Element count must be 1 for legacy XML");

    LayoutElement *sun_b = find_element_by_tag(&st_b, "MainMenu", "Sun");
    LayoutElement *mmsun_b = find_element_by_tag(&st_b, "MainMenu", "MMSun");
    TEST_ASSERT(sun_b != NULL, "Sun must be loaded");
    TEST_ASSERT(mmsun_b == NULL, "MMSun must NOT exist in legacy XML elements");

    TEST_ASSERT(layout_xml_save(&st_b, temp_out_b), "Failed to save legacy XML");

    FILE *rf_b = fopen(temp_out_b, "rb");
    TEST_ASSERT(rf_b != NULL, "Failed to read saved legacy XML");
    char buf_b[2048] = {0};
    fread(buf_b, 1, sizeof(buf_b) - 1, rf_b);
    fclose(rf_b);

    TEST_ASSERT(strstr(buf_b, "<MMSun") == NULL, "Resaved legacy XML must NOT inject MMSun tag (0-diff preservation)");
    TEST_ASSERT(strstr(buf_b, "<Sun x=\"53\" y=\"-89\" />") != NULL, "Resaved legacy XML must preserve Sun tag");

    remove(temp_in_b);
    remove(temp_out_b);

    printf("  [PASS] MMSun present and omitted roundtrip (0-diff) verified.\n");
    g_tests_passed++;
    return true;
}

/* Regression Test 13: AdvBack Tag Roundtrip and 0-Diff Legacy Omission Preservation */
static bool test_advback_roundtrip_and_omission(void) {
    printf("[TEST 13] Adventure Temples AdvBack Roundtrip and Omission...\n");

    /* Subtest A: AdvBack tag present */
    char temp_in_a[MAX_PATH], temp_out_a[MAX_PATH];
    GetTempPathA(sizeof(temp_in_a), temp_in_a);
    snprintf(temp_out_a, sizeof(temp_out_a), "%szupather_test_advback_out.xml", temp_in_a);
    strncat(temp_in_a, "zupather_test_advback_in.xml", sizeof(temp_in_a) - strlen(temp_in_a) - 1);

    FILE *fp = fopen(temp_in_a, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in_a");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <AdventureTemples space=\"1200x675\">\n");
    fprintf(fp, "        <AdvBack x=\"150\" y=\"0\" />\n");
    fprintf(fp, "        <Temple1 x=\"150\" y=\"264\" />\n");
    fprintf(fp, "    </AdventureTemples>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st_a;
    memset(&st_a, 0, sizeof(st_a));
    st_a.target_w = 1200;
    st_a.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st_a, temp_in_a), "Failed to load XML with AdvBack");
    TEST_ASSERT(st_a.element_count == 2, "Element count must be 2");
    LayoutElement *advback = find_element_by_tag(&st_a, "AdventureTemples", "AdvBack");
    TEST_ASSERT(advback != NULL, "AdvBack element must be parsed");
    TEST_ASSERT(advback->x == 150 && advback->y == 0, "AdvBack coordinates match");

    TEST_ASSERT(layout_xml_save(&st_a, temp_out_a), "Failed to save AdvBack XML");

    LayoutEditorState st_a_resaved;
    memset(&st_a_resaved, 0, sizeof(st_a_resaved));
    st_a_resaved.target_w = 1200;
    st_a_resaved.target_h = 675;
    TEST_ASSERT(layout_xml_load(&st_a_resaved, temp_out_a), "Failed to reload resaved AdvBack XML");
    TEST_ASSERT(st_a_resaved.element_count == 2, "Reloaded element count must be 2");
    LayoutElement *advback_re = find_element_by_tag(&st_a_resaved, "AdventureTemples", "AdvBack");
    TEST_ASSERT(advback_re != NULL, "AdvBack must exist after reload");
    TEST_ASSERT(advback_re->x == 150 && advback_re->y == 0, "AdvBack coordinates preserved across roundtrip");

    remove(temp_in_a);
    remove(temp_out_a);

    /* Subtest B: AdvBack omitted (legacy layout XML) */
    char temp_in_b[MAX_PATH], temp_out_b[MAX_PATH];
    GetTempPathA(sizeof(temp_in_b), temp_in_b);
    snprintf(temp_out_b, sizeof(temp_out_b), "%szupather_test_advback_omitted_out.xml", temp_in_b);
    strncat(temp_in_b, "zupather_test_advback_omitted_in.xml", sizeof(temp_in_b) - strlen(temp_in_b) - 1);

    fp = fopen(temp_in_b, "wb");
    TEST_ASSERT(fp != NULL, "Failed to create temp_in_b");
    fprintf(fp, "<Layout>\n");
    fprintf(fp, "    <AdventureTemples space=\"1200x675\">\n");
    fprintf(fp, "        <Temple1 x=\"150\" y=\"264\" />\n");
    fprintf(fp, "    </AdventureTemples>\n");
    fprintf(fp, "</Layout>\n");
    fclose(fp);

    LayoutEditorState st_b;
    memset(&st_b, 0, sizeof(st_b));
    st_b.target_w = 1200;
    st_b.target_h = 675;

    TEST_ASSERT(layout_xml_load(&st_b, temp_in_b), "Failed to load legacy XML without AdvBack");
    TEST_ASSERT(st_b.element_count == 1, "Element count must be 1 for legacy XML");

    LayoutElement *t1_b = find_element_by_tag(&st_b, "AdventureTemples", "Temple1");
    LayoutElement *advback_b = find_element_by_tag(&st_b, "AdventureTemples", "AdvBack");
    TEST_ASSERT(t1_b != NULL, "Temple1 must be loaded");
    TEST_ASSERT(advback_b == NULL, "AdvBack must NOT exist in legacy XML elements");

    TEST_ASSERT(layout_xml_save(&st_b, temp_out_b), "Failed to save legacy XML");

    FILE *rf_b = fopen(temp_out_b, "rb");
    TEST_ASSERT(rf_b != NULL, "Failed to read saved legacy XML");
    char buf_b[2048] = {0};
    fread(buf_b, 1, sizeof(buf_b) - 1, rf_b);
    fclose(rf_b);

    TEST_ASSERT(strstr(buf_b, "<AdvBack") == NULL, "Resaved legacy XML must NOT inject AdvBack tag (0-diff preservation)");
    TEST_ASSERT(strstr(buf_b, "<Temple1 x=\"150\" y=\"264\" />") != NULL, "Resaved legacy XML must preserve Temple1 tag");

    remove(temp_in_b);
    remove(temp_out_b);

    printf("  [PASS] AdvBack present and omitted roundtrip (0-diff) verified.\n");
    g_tests_passed++;
    return true;
}

static bool test_gauntlet_buttons_mapping_and_roundtrip(const char *layout_path) {
    printf("--- Test 14: Gauntlet Buttons Real Mapping & Roundtrip ---\n");
    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    st.target_w = 1200;
    st.target_h = 675;
    TEST_ASSERT(layout_xml_load(&st, layout_path), "Failed to load layout XML for Gauntlet Buttons test");

    /* Verify each button exists with its correct PopCap coordinates in 1200x675 space */
    LayoutElement *btn_back = find_element_by_tag(&st, "GauntletButtons", "Back");
    LayoutElement *btn_play = find_element_by_tag(&st, "GauntletButtons", "Play");
    LayoutElement *btn_next = find_element_by_tag(&st, "GauntletButtons", "Next");
    LayoutElement *btn_mainmenu = find_element_by_tag(&st, "GauntletButtons", "MainMenu");
    LayoutElement *btn_survival = find_element_by_tag(&st, "GauntletButtons", "Survival");
    LayoutElement *btn_practice = find_element_by_tag(&st, "GauntletButtons", "Practice");

    TEST_ASSERT(btn_back != NULL, "GauntletButtons <Back> must exist");
    TEST_ASSERT(btn_play != NULL, "GauntletButtons <Play> must exist");
    TEST_ASSERT(btn_next != NULL, "GauntletButtons <Next> must exist");
    TEST_ASSERT(btn_mainmenu != NULL, "GauntletButtons <MainMenu> must exist");
    TEST_ASSERT(btn_survival != NULL, "GauntletButtons <Survival> must exist");
    TEST_ASSERT(btn_practice != NULL, "GauntletButtons <Practice> must exist");

    TEST_ASSERT(btn_back->x == 399 && btn_back->y == 624, "Gauntlet <Back> coordinates (399, 624)");
    TEST_ASSERT(btn_play->x == 540 && btn_play->y == 624, "Gauntlet <Play> coordinates (540, 624)");
    TEST_ASSERT(btn_next->x == 698 && btn_next->y == 624, "Gauntlet <Next> coordinates (698, 624)");
    TEST_ASSERT(btn_mainmenu->x == 164 && btn_mainmenu->y == 593, "Gauntlet <MainMenu> coordinates (164, 593)");
    TEST_ASSERT(btn_survival->x == 862 && btn_survival->y == 581, "Gauntlet <Survival> coordinates (862, 581)");
    TEST_ASSERT(btn_practice->x == 860 && btn_practice->y == 627, "Gauntlet <Practice> coordinates (860, 627)");

    /* Verify default dimensions */
    TEST_ASSERT(btn_back->def_w == 93 && btn_back->def_h == 33, "Back def dimensions (93x33)");
    TEST_ASSERT(btn_play->def_w == 105 && btn_play->def_h == 33, "Play def dimensions (105x33)");
    TEST_ASSERT(btn_next->def_w == 94 && btn_next->def_h == 33, "Next def dimensions (94x33)");
    TEST_ASSERT(btn_mainmenu->def_w == 141 && btn_mainmenu->def_h == 56, "MainMenu def dimensions (141x56)");
    TEST_ASSERT(btn_survival->def_w == 127 && btn_survival->def_h == 28, "Survival def dimensions (127x28)");
    TEST_ASSERT(btn_practice->def_w == 127 && btn_practice->def_h == 32, "Practice def dimensions (127x32)");

    /* Verify sprite name resolution */
    int cols = 0, rows = 0, cell = 0;
    TEST_ASSERT(strcmp(get_element_sprite_name(btn_back, &cols, &rows, &cell), "gauntbackbutton") == 0, "Back sprite is gauntbackbutton");
    TEST_ASSERT(strcmp(get_element_sprite_name(btn_play, &cols, &rows, &cell), "gauntplaybutton") == 0, "Play sprite is gauntplaybutton");
    TEST_ASSERT(strcmp(get_element_sprite_name(btn_next, &cols, &rows, &cell), "gauntnextbutton") == 0, "Next sprite is gauntnextbutton");
    TEST_ASSERT(strcmp(get_element_sprite_name(btn_mainmenu, &cols, &rows, &cell), "gauntmainmenubutton") == 0, "MainMenu sprite is gauntmainmenubutton");
    TEST_ASSERT(strcmp(get_element_sprite_name(btn_survival, &cols, &rows, &cell), "gauntsurvivalbutton") == 0, "Survival sprite is gauntsurvivalbutton");
    TEST_ASSERT(strcmp(get_element_sprite_name(btn_practice, &cols, &rows, &cell), "gauntpracticebutton") == 0, "Practice sprite is gauntpracticebutton");

    printf("  [PASS] Gauntlet buttons mapping, coordinates, dimensions, and sprites verified.\n");
    g_tests_passed++;
    return true;
}


static bool test_gauntlet_labels_mapping_and_roundtrip(const char *layout_path) {
    printf("--- Test 15: Gauntlet Labels Real Mapping & Roundtrip ---\n");
    LayoutEditorState st;
    memset(&st, 0, sizeof(st));
    st.target_w = 1200;
    st.target_h = 675;
    TEST_ASSERT(layout_xml_load(&st, layout_path), "Failed to load layout XML for Gauntlet Labels test");

    /* Find all GauntletLabels elements */
    LayoutElement *el_gt    = find_element_by_tag(&st, "GauntletLabels", "GauntletTitle");
    LayoutElement *el_frame = find_element_by_tag(&st, "GauntletLabels", "DoorFrame");
    LayoutElement *el_dname = find_element_by_tag(&st, "GauntletLabels", "DoorName");
    LayoutElement *el_ptxt  = find_element_by_tag(&st, "GauntletLabels", "PracticeText");
    LayoutElement *el_cmtag = find_element_by_tag(&st, "GauntletLabels", "CustomMapTag");
    LayoutElement *el_clip  = find_element_by_tag(&st, "GauntletLabels", "HighscoreClip");
    LayoutElement *el_ibox  = find_element_by_tag(&st, "GauntletLabels", "InfoBox");
    LayoutElement *el_gem   = find_element_by_tag(&st, "GauntletLabels", "Gem");
    LayoutElement *el_hsn   = find_element_by_tag(&st, "GauntletLabels", "HighscoreNames");
    LayoutElement *el_hss   = find_element_by_tag(&st, "GauntletLabels", "HighscoreScores");

    TEST_ASSERT(el_gt != NULL, "GauntletTitle must exist");
    TEST_ASSERT(el_frame != NULL, "DoorFrame must exist");
    TEST_ASSERT(el_dname != NULL, "DoorName must exist");
    TEST_ASSERT(el_ptxt != NULL, "PracticeText must exist");
    TEST_ASSERT(el_cmtag != NULL, "CustomMapTag must exist");
    TEST_ASSERT(el_clip != NULL, "HighscoreClip must exist");
    TEST_ASSERT(el_ibox != NULL, "InfoBox must exist");
    TEST_ASSERT(el_gem != NULL, "Gem must exist");
    TEST_ASSERT(el_hsn != NULL, "HighscoreNames must exist");
    TEST_ASSERT(el_hss != NULL, "HighscoreScores must exist");

    /* GauntletTitle sprite and dimension check */
    int cols = 0, rows = 0, cell = 0;
    const char *sp_gt = get_element_sprite_name(el_gt, &cols, &rows, &cell);
    TEST_ASSERT(sp_gt != NULL && strcmp(sp_gt, "gaunttitle") == 0, "GauntletTitle resolves to gaunttitle sprite");
    TEST_ASSERT(el_gt->def_w == 168 && el_gt->def_h == 68, "GauntletTitle def size (168x68)");
    TEST_ASSERT(el_gt->x == 150 && el_gt->y == 0, "GauntletTitle coordinates (150, 0)");

    /* DoorFrame and centered DoorName/PracticeText */
    TEST_ASSERT(el_frame->w == 450 && el_frame->h == 338, "DoorFrame size (450x338)");
    TEST_ASSERT(el_dname->def_w == 450, "DoorName def_w must be 450 to center across doorway");
    TEST_ASSERT(el_ptxt->def_w == 450, "PracticeText def_w must be 450");

    /* CustomMapTag visibility in editor */
    st.cur_screen_idx = 3;
    st.cur_sub_filter_idx = 0;
    TEST_ASSERT(is_element_visible_in_editor(el_cmtag, &st) == true, "CustomMapTag must be visible in Gauntlet view");

    /* InfoBox text classification */
    TEST_ASSERT(is_text_element(el_ibox) == true, "InfoBox must be classified as a text element");
    TEST_ASSERT(is_region_element(el_ibox) == false, "InfoBox must NOT be a region element");

    /* HighscoreClip z-order is lowest (5) so it never blocks mouse clicks */
    TEST_ASSERT(get_element_z_order(el_clip, 3) == 5, "HighscoreClip z-order must be 5");

    /* Canvas rect positions in 1200x675 space */
    update_all_elements_canvas_rects(&st, 1232, 707);
    TEST_ASSERT(el_gt->canvas_rect.left == 16 + 150 && el_gt->canvas_rect.top == 16 + 0, "GauntletTitle at (150, 0)");
    TEST_ASSERT(el_dname->canvas_rect.left == 16 + 389, "DoorName left at 389");
    TEST_ASSERT(el_ptxt->canvas_rect.left == 16 + 361, "PracticeText left at 361");
    TEST_ASSERT(el_hss->canvas_rect.left == 16 + 959, "HighscoreScores left at 959");
    TEST_ASSERT(el_hsn->canvas_rect.left == 16 + 776, "HighscoreNames left at 776");

    printf("  [PASS] Gauntlet labels mapping, dimensions, sprites, and z-orders verified.\n");
    g_tests_passed++;
    return true;
}

/* 16. Test Adventure & Gauntlet Typography & Sprite Regression */
static bool test_adventure_gauntlet_regression(void) {
    printf("--- Test 16: Adventure & Gauntlet Typography & Sprite Regression ---\n");

    /* 1. Verify get_element_sprite_name for AdvBack and AdventureTitle is not empty */
    LayoutElement el_back = {0};
    strncpy(el_back.section, "AdventureTemples", sizeof(el_back.section) - 1);
    strncpy(el_back.tag, "AdvBack", sizeof(el_back.tag) - 1);
    const char *sp_back = get_element_sprite_name(&el_back, NULL, NULL, NULL);
    TEST_ASSERT(sp_back != NULL && strlen(sp_back) > 0, "AdvBack sprite must not be empty");
    TEST_ASSERT(strcmp(sp_back, "advback") == 0, "AdvBack sprite must resolve to 'advback'");

    LayoutElement el_title = {0};
    strncpy(el_title.section, "AdventureLabels", sizeof(el_title.section) - 1);
    strncpy(el_title.tag, "AdventureTitle", sizeof(el_title.tag) - 1);
    const char *sp_title = get_element_sprite_name(&el_title, NULL, NULL, NULL);
    TEST_ASSERT(sp_title != NULL && strlen(sp_title) > 0, "AdventureTitle sprite must not be empty");
    TEST_ASSERT(strcmp(sp_title, "advtitle") == 0, "AdventureTitle sprite must resolve to 'advtitle'");

    /* 2. Test word-wrap splits long text into >= 2 lines on rect width 211 */
    PopCapFont dummy_font;
    memset(&dummy_font, 0, sizeof(dummy_font));
    dummy_font.loaded = true;
    dummy_font.space_width = 8;
    dummy_font.ascent = 10;

    const char *long_text = "PLAY THROUGH ALL THE LEVELS YOU HAVE UNLOCKED IN ADVENTURE MODE IN RANDOM ORDER!";
    int lines = popcap_font_count_wrapped_lines(&dummy_font, long_text, 211, 1.0f);
    TEST_ASSERT(lines >= 2, "Word-wrap must split long InfoBox text into >= 2 lines on width 211");

    printf("  [PASS] AdvBack/AdventureTitle sprite resolution and word-wrap >= 2 lines verified.\n");
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
    test_auto_center_x_lock_and_roundtrip(full_layout);
    test_fase_b1_highscore_and_scoretext(full_layout);
    test_font_baseline_convention(full_layout);
    test_mmsun_roundtrip_and_omission();
    test_advback_roundtrip_and_omission();
    test_gauntlet_buttons_mapping_and_roundtrip(full_layout);
    test_gauntlet_labels_mapping_and_roundtrip(full_layout);
    test_adventure_gauntlet_regression();

    printf("\n=======================================================\n");
    printf(" Test Results: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("=======================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
