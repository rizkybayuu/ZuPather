#ifndef LEVEL_PROGRESSION_H
#define LEVEL_PROGRESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Supported Progression Difficulties */
typedef enum {
    LEVEL_DIFF_MEDIUM = 0,     /* Default: Standard Balanced Zuma gameplay */
    LEVEL_DIFF_DEV_TEST,       /* Fast testing kit: speed x3, destroyall, repeat=100 */
    LEVEL_DIFF_EASY,           /* Relaxed pace: -25% speed, generous slow factor */
    LEVEL_DIFF_HARD,           /* Fast & challenging: +25% speed, longer trains (levels_hard.xml) */
    LEVEL_DIFF_EXTREME,        /* Brutal intensity: +75% speed (up to 2.1x), +60% balls, 1.5x score */
    LEVEL_DIFF_COUNT
} LevelDifficulty;

/* Progression Setting Entry */
typedef struct {
    char id[32];            /* e.g. "zupath3_med_1" */
    float speed;            /* e.g. 0.40 */
    int start;              /* starting ball count, e.g. 18 */
    int score;              /* score to reach Zuma, e.g. 2000 */
    int repeat;             /* ball generation repetitions, e.g. 40 */
    int colors;             /* ball color count: 4, 5, 6 */
    float slowfactor;       /* match slow multiplier, e.g. 3.5 */
    int powerfreq;          /* powerup spawn interval, e.g. 2200 */
    bool destroyall;        /* destroyall="true" for dev-test */
    int firespeed;          /* frog shooting speed, default 6, 10 for dev-test */
    int partime;            /* optional par time */
} ZumaProgressionSetting;

#define MAX_PROGRESSION_STEPS 30

typedef struct {
    char id[64];            /* e.g. "zupath3-medium" */
    int path_count;         /* 1 to 10 */
    LevelDifficulty difficulty;
    int step_count;         /* number of settings steps (e.g. 24 or 30) */
    ZumaProgressionSetting steps[MAX_PROGRESSION_STEPS];
} ZumaLevelProgressionDef;

/* Global state for canvas difficulty */
extern LevelDifficulty g_level_difficulty;

/* String conversions & utility functions */
const char *level_difficulty_to_string(LevelDifficulty diff);
LevelDifficulty level_difficulty_from_string(const char *str);
const char *level_difficulty_get_display_name(LevelDifficulty diff);
const char *level_difficulty_get_description(LevelDifficulty diff);

/* Build progression ID, e.g. "zupath1-medium", "zupath3-hard", "zupath2-dev-test" */
void get_level_progression_id(int path_count, LevelDifficulty diff, char *out_buf, size_t buf_len);

/* Generate the full balanced Zuma progression definition for any track count (1-10) and difficulty */
void generate_zuma_level_progression(int path_count, LevelDifficulty diff, ZumaLevelProgressionDef *out_prog);

/* Write settings XML tags for a progression */
void write_zuma_progression_settings_xml(FILE *f, const ZumaLevelProgressionDef *prog, const char *indent);

/* Write LevelProgression XML tag for a progression */
void write_zuma_progression_def_xml(FILE *f, const ZumaLevelProgressionDef *prog, const char *indent);

#endif /* LEVEL_PROGRESSION_H */
