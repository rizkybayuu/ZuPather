#include "level_progression.h"
#include <string.h>
#include <strings.h>
#include <math.h>

LevelDifficulty g_level_difficulty = LEVEL_DIFF_MEDIUM;

const char *level_difficulty_to_string(LevelDifficulty diff) {
    switch (diff) {
        case LEVEL_DIFF_DEV_TEST: return "dev-test";
        case LEVEL_DIFF_EASY:     return "easy";
        case LEVEL_DIFF_MEDIUM:   return "medium";
        case LEVEL_DIFF_HARD:     return "hard";
        case LEVEL_DIFF_EXTREME:  return "extreme";
        default:                  return "medium";
    }
}

LevelDifficulty level_difficulty_from_string(const char *str) {
    if (!str || !str[0]) return LEVEL_DIFF_MEDIUM;
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (!*str) return LEVEL_DIFF_MEDIUM;

    char buf[32];
    size_t i = 0;
    while (str[i] && str[i] != ' ' && str[i] != '\t' && str[i] != '\r' && str[i] != '\n' && str[i] != '"' && str[i] != '>' && i < sizeof(buf) - 1) {
        buf[i] = str[i];
        i++;
    }
    buf[i] = '\0';

    if (_stricmp(buf, "dev-test") == 0 || _stricmp(buf, "dev_test") == 0 || _stricmp(buf, "dev") == 0) {
        return LEVEL_DIFF_DEV_TEST;
    }
    if (_stricmp(buf, "easy") == 0) {
        return LEVEL_DIFF_EASY;
    }
    if (_stricmp(buf, "hard") == 0) {
        return LEVEL_DIFF_HARD;
    }
    if (_stricmp(buf, "extreme") == 0) {
        return LEVEL_DIFF_EXTREME;
    }
    return LEVEL_DIFF_MEDIUM;
}

const char *level_difficulty_get_display_name(LevelDifficulty diff) {
    switch (diff) {
        case LEVEL_DIFF_DEV_TEST: return "Dev-Test (Testing Kit)";
        case LEVEL_DIFF_EASY:     return "Easy (Relaxed)";
        case LEVEL_DIFF_MEDIUM:   return "Medium (Standard Default)";
        case LEVEL_DIFF_HARD:     return "Hard (Challenging)";
        case LEVEL_DIFF_EXTREME:  return "Extreme (Intense)";
        default:                  return "Medium (Standard Default)";
    }
}

const char *level_difficulty_get_description(LevelDifficulty diff) {
    switch (diff) {
        case LEVEL_DIFF_DEV_TEST:
            return "Dev-Test (Testing Kit):\n"
                   "- Kecepatan bola kencang (speed x3, firespeed=10)\n"
                   "- repeat=100 (kereta bola respawn berulang hingga 100x)\n"
                   "- destroyall=true (semua bola otomatis hancur saat stage clear)\n"
                   "(Murni untuk keperluan testing gameplay map di Zuma Deluxe)";
        case LEVEL_DIFF_EASY:
            return "Relaxed: Ball train speed reduced by 25%, generous slow factor on ball matches, and lower starting ball count.";
        case LEVEL_DIFF_MEDIUM:
            return "Standard Default: Authentic Zuma Deluxe pacing (matches vanilla standard for 1 path, dual for 2 paths, and calibrated progressions for 3-10 paths).";
        case LEVEL_DIFF_HARD:
            return "Challenging: Faster ball train speeds (+25%), tighter slow factor recovery, and longer starting ball trains (authentic to Zuma Deluxe levels_hard.xml).";
        case LEVEL_DIFF_EXTREME:
            return "Extreme (Intense): Brutal challenge far beyond Hard. Blazing train speeds (+75%, up to 2.10 speed), massive ball trains (+60% balls), elevated score targets (+50%), scarce powerups, and minimal slowdown relief on matches.";
        default:
            return "Standard Default: Authentic Zuma Deluxe pacing.";
    }
}

void get_level_progression_id(int path_count, LevelDifficulty diff, char *out_buf, size_t buf_len) {
    if (!out_buf || buf_len == 0) return;
    int p = (path_count > 0 && path_count <= 10) ? path_count : 1;
    const char *diff_str = level_difficulty_to_string(diff);
    snprintf(out_buf, buf_len, "zupath%d-%s", p, diff_str);
}

typedef struct {
    float speed;
    int start;
    int score;
    float slowfactor;
    int powerfreq;
} VanillaProgStep;

/* Authentic Zuma Deluxe 'standard' progression (level1 - level24) */
static const VanillaProgStep s_vanilla_standard[24] = {
    { 0.50f, 35, 1000, 0.0f,    0 }, /* level1 */
    { 0.60f, 40, 1500, 0.0f,    0 }, /* level2 */
    { 0.70f, 45, 2000, 0.0f,    0 }, /* level3 */
    { 0.75f, 50, 2500, 0.0f,    0 }, /* level4 */
    { 0.80f, 55, 3000, 0.0f,    0 }, /* level5 */
    { 0.85f, 60, 3500, 0.0f,    0 }, /* level6 */
    { 0.90f, 65, 4000, 0.0f,    0 }, /* level7 */
    { 0.95f, 70, 4500, 0.0f,    0 }, /* level8 */
    { 0.95f, 75, 5000, 3.5f,    0 }, /* level9 */
    { 1.00f, 75, 5000, 3.0f,    0 }, /* level10 */
    { 1.00f, 70, 5000, 2.5f,    0 }, /* level11 */
    { 0.95f, 65, 5000, 2.0f,    0 }, /* level12 */
    { 0.80f, 60, 5000, 1.5f,    0 }, /* level13 */
    { 0.75f, 55, 5000, 1.0f,    0 }, /* level14 */
    { 0.80f, 50, 5000, 1.0f,    0 }, /* level15 */
    { 0.85f, 40, 5000, 1.0f, 2500 }, /* level16 */
    { 0.95f, 35, 5000, 1.0f, 2300 }, /* level17 */
    { 1.00f, 35, 5000, 1.0f, 2100 }, /* level18 */
    { 1.00f, 40, 5000, 1.0f, 1900 }, /* level19 */
    { 1.05f, 45, 5000, 1.0f, 1700 }, /* level20 */
    { 1.05f, 50, 5000, 1.0f, 1500 }, /* level21 */
    { 1.10f, 55, 5000, 1.0f, 1500 }, /* level22 */
    { 1.15f, 60, 5000, 1.0f, 1400 }, /* level23 */
    { 1.20f, 65, 5000, 1.0f, 1300 }  /* level24 */
};

/* Authentic Zuma Deluxe 'dual' progression (dual1 - dual24) */
static const VanillaProgStep s_vanilla_dual[24] = {
    { 0.45f, 20, 2000, 0.0f,    0 }, /* dual1 */
    { 0.50f, 25, 2500, 0.0f,    0 }, /* dual2 */
    { 0.55f, 30, 3000, 0.0f,    0 }, /* dual3 */
    { 0.60f, 35, 3500, 0.0f,    0 }, /* dual4 */
    { 0.65f, 40, 4000, 0.0f,    0 }, /* dual5 */
    { 0.70f, 45, 4500, 0.0f,    0 }, /* dual6 */
    { 0.75f, 50, 5000, 0.0f,    0 }, /* dual7 */
    { 0.80f, 50, 4500, 0.0f,    0 }, /* dual8 */
    { 0.80f, 50, 4500, 3.5f,    0 }, /* dual9 */
    { 0.85f, 45, 4500, 3.5f,    0 }, /* dual10 */
    { 0.80f, 45, 4500, 3.0f,    0 }, /* dual11 */
    { 0.85f, 40, 4500, 3.0f,    0 }, /* dual12 */
    { 0.80f, 45, 4500, 2.5f, 2900 }, /* dual13 */
    { 0.80f, 50, 5000, 2.5f, 2800 }, /* dual14 */
    { 0.80f, 45, 5000, 2.0f, 2700 }, /* dual15 */
    { 0.80f, 50, 5000, 2.0f, 2600 }, /* dual16 */
    { 0.70f, 40, 5000, 1.0f, 2500 }, /* dual17 */
    { 0.75f, 40, 5000, 1.0f, 2400 }, /* dual18 */
    { 0.75f, 45, 5000, 1.0f, 2300 }, /* dual19 */
    { 0.80f, 45, 5000, 1.0f, 2200 }, /* dual20 */
    { 0.85f, 50, 5000, 1.0f, 2100 }, /* dual21 */
    { 0.90f, 55, 5000, 1.0f, 2500 }, /* dual22 */
    { 0.95f, 55, 5000, 1.0f, 2400 }, /* dual23 */
    { 1.00f, 60, 5000, 1.0f, 2300 }  /* dual24 */
};

typedef struct {
    float start_speed;
    float end_speed;
    int start_balls;
    int end_balls;
    int powerfreq_start;
    int powerfreq_end;
} BaseTrackMetrics;

/* Scaling anchors for multi-track configurations (3..10 tracks) */
static const BaseTrackMetrics s_track_metrics[10] = {
    /* 1 track  */ { 0.50f, 1.40f, 35, 80, 2500, 1000 },
    /* 2 tracks */ { 0.45f, 1.00f, 20, 60, 2900, 2300 },
    /* 3 tracks */ { 0.40f, 0.85f, 18, 48, 2600, 1900 },
    /* 4 tracks */ { 0.35f, 0.75f, 15, 42, 2400, 1700 },
    /* 5 tracks */ { 0.30f, 0.68f, 13, 36, 2200, 1500 },
    /* 6 tracks */ { 0.27f, 0.60f, 11, 32, 2000, 1400 },
    /* 7 tracks */ { 0.24f, 0.54f, 10, 28, 1900, 1300 },
    /* 8 tracks */ { 0.22f, 0.48f,  8, 25, 1800, 1200 },
    /* 9 tracks */ { 0.20f, 0.44f,  7, 22, 1700, 1100 },
    /* 10 tracks*/ { 0.18f, 0.40f,  6, 20, 1600, 1000 }
};

void generate_zuma_level_progression(int path_count, LevelDifficulty diff, ZumaLevelProgressionDef *out_prog) {
    if (!out_prog) return;
    memset(out_prog, 0, sizeof(ZumaLevelProgressionDef));

    int p = (path_count > 0 && path_count <= 10) ? path_count : 1;
    out_prog->path_count = p;
    out_prog->difficulty = diff;
    get_level_progression_id(p, diff, out_prog->id, sizeof(out_prog->id));

    out_prog->step_count = 24;

    /* Abbreviated difficulty tag for setting IDs, e.g. "med", "dev", "easy", "hrd", "ext" */
    const char *id_diff_tag = "med";
    if (diff == LEVEL_DIFF_DEV_TEST) id_diff_tag = "dev";
    else if (diff == LEVEL_DIFF_EASY) id_diff_tag = "easy";
    else if (diff == LEVEL_DIFF_HARD) id_diff_tag = "hrd";
    else if (diff == LEVEL_DIFF_EXTREME) id_diff_tag = "ext";

    for (int i = 0; i < 24; ++i) {
        ZumaProgressionSetting *s = &out_prog->steps[i];
        snprintf(s->id, sizeof(s->id), "zupath%d_%s_%d", p, id_diff_tag, i + 1);

        float base_speed = 0.50f;
        int base_start = 35;
        int base_score = 2000;
        float base_slowfactor = 0.0f;
        int base_pf = 0;

        if (p == 1) {
            /* 100% Authentic Zuma Deluxe 'standard' progression */
            base_speed = s_vanilla_standard[i].speed;
            base_start = s_vanilla_standard[i].start;
            base_score = s_vanilla_standard[i].score;
            base_slowfactor = s_vanilla_standard[i].slowfactor;
            base_pf = s_vanilla_standard[i].powerfreq;
        } else if (p == 2) {
            /* 100% Authentic Zuma Deluxe 'dual' progression */
            base_speed = s_vanilla_dual[i].speed;
            base_start = s_vanilla_dual[i].start;
            base_score = s_vanilla_dual[i].score;
            base_slowfactor = s_vanilla_dual[i].slowfactor;
            base_pf = s_vanilla_dual[i].powerfreq;
        } else {
            /* 3..10 tracks: Proportionally scaled from authentic 'dual' curve */
            float speed_scale = s_track_metrics[p - 1].start_speed / s_track_metrics[1].start_speed;
            float start_scale = (float)s_track_metrics[p - 1].start_balls / (float)s_track_metrics[1].start_balls;
            base_speed = s_vanilla_dual[i].speed * speed_scale;
            base_start = (int)roundf((float)s_vanilla_dual[i].start * start_scale);
            base_score = s_vanilla_dual[i].score;
            base_slowfactor = s_vanilla_dual[i].slowfactor;
            base_pf = (s_vanilla_dual[i].powerfreq > 0) ? (int)roundf((float)s_vanilla_dual[i].powerfreq * (s_track_metrics[p - 1].powerfreq_start / (float)s_track_metrics[1].powerfreq_start)) : 0;
        }

        /* Enforce strictly: colors is NEVER emitted, repeat only in dev-test */
        s->colors = 0;
        s->repeat = 0;
        s->firespeed = 6;
        s->destroyall = false;
        s->partime = 0;

        /* Apply Difficulty Modifiers */
        if (diff == LEVEL_DIFF_DEV_TEST) {
            /* Dev-Test: Fast testing speed, repeat=100, destroyall=true on clear, standard score */
            s->speed = 2.50f;
            s->start = 30;
            s->score = base_score;
            s->repeat = 100;
            s->slowfactor = 4.0f;
            s->powerfreq = 800;
            s->destroyall = true;
            s->firespeed = 10;
        } else if (diff == LEVEL_DIFF_EASY) {
            s->speed = base_speed * 0.75f;
            s->start = (int)roundf((float)base_start * 0.75f);
            s->score = (int)roundf((float)base_score * 0.80f);
            s->slowfactor = (base_slowfactor > 0.0f) ? (base_slowfactor + 1.0f) : 0.0f;
            s->powerfreq = (base_pf > 400) ? (base_pf - 400) : 0;
        } else if (diff == LEVEL_DIFF_HARD) {
            /* Modeled faithfully on Zuma Deluxe levels_hard.xml (+25% speed, +25% balls) */
            s->speed = base_speed * 1.25f;
            s->start = (int)roundf((float)base_start * 1.25f);
            s->score = (int)roundf((float)base_score * 1.15f);
            s->slowfactor = (base_slowfactor > 1.5f) ? (base_slowfactor - 0.5f) : base_slowfactor;
            s->powerfreq = (base_pf > 0) ? (base_pf + 400) : 0;
        } else if (diff == LEVEL_DIFF_EXTREME) {
            /* Extreme: Far tougher than levels_hard.xml (+75% speed up to 2.1x, +60% balls, 1.5x score, near-zero slowdown) */
            s->speed = base_speed * 1.75f;
            s->start = (int)roundf((float)base_start * 1.60f);
            s->score = (int)roundf((float)base_score * 1.50f);
            s->slowfactor = (base_slowfactor > 0.05f) ? 0.5f : 0.0f;
            s->powerfreq = (base_pf > 0) ? (base_pf + 1500) : 3500;
        } else {
            /* Medium / Default */
            s->speed = base_speed;
            s->start = base_start;
            s->score = base_score;
            s->slowfactor = base_slowfactor;
            s->powerfreq = base_pf;
        }

        /* Sanity clamps */
        if (s->speed < 0.15f) s->speed = 0.15f;
        if (s->start < 5) s->start = 5;
        if (s->score < 1000) s->score = 1000;
    }
}

void write_zuma_progression_settings_xml(FILE *f, const ZumaLevelProgressionDef *prog, const char *indent) {
    if (!f || !prog) return;
    const char *ind = indent ? indent : "";

    for (int i = 0; i < prog->step_count; ++i) {
        const ZumaProgressionSetting *s = &prog->steps[i];
        fprintf(f, "%s<Settings id=\"%s\" speed=\"%.2f\" start=\"%d\" score=\"%d\"",
                ind, s->id, s->speed, s->start, s->score);

        if (s->slowfactor > 0.05f) fprintf(f, " slowfactor=\"%.1f\"", s->slowfactor);
        if (s->powerfreq > 0) fprintf(f, " powerfreq=\"%d\"", s->powerfreq);
        if (prog->difficulty == LEVEL_DIFF_DEV_TEST && s->repeat > 0) {
            fprintf(f, " repeat=\"%d\"", s->repeat);
        }
        if (s->destroyall) fprintf(f, " destroyall=\"true\"");
        if (s->firespeed != 6 && s->firespeed > 0) fprintf(f, " firespeed=\"%d\"", s->firespeed);
        if (s->partime > 0) fprintf(f, " partime=\"%d\"", s->partime);

        fprintf(f, " />\n");
    }
}

void write_zuma_progression_def_xml(FILE *f, const ZumaLevelProgressionDef *prog, const char *indent) {
    if (!f || !prog) return;
    const char *ind = indent ? indent : "";

    fprintf(f, "%s<LevelProgression\n", ind);
    fprintf(f, "%s\tid=\"%s\"\n", ind, prog->id);

    /* settings list */
    fprintf(f, "%s\tsettings=\"", ind);
    for (int i = 0; i < prog->step_count; ++i) {
        fprintf(f, "%s%s", prog->steps[i].id, (i + 1 < prog->step_count) ? "," : "");
    }
    fprintf(f, "\"\n");

    /* difficulty numbers 1..24 */
    fprintf(f, "%s\tdifficulty=\"", ind);
    for (int i = 0; i < prog->step_count; ++i) {
        fprintf(f, "%d%s", i + 1, (i + 1 < prog->step_count) ? "," : "");
    }
    fprintf(f, "\"\n");
    fprintf(f, "%s\t/>\n", ind);
}
