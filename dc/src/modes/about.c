/*
 * about.c — version + build info + detected video state.
 */
#include "about.h"
#include "../ui/bfont_util.h"
#include "../video/mode.h"

static void about_enter(void) {}
static void about_leave(void) {}
static void about_update(float dt) { (void)dt; }

static void about_draw(void)
{
    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK,
                     "Joypad Tester - Dreamcast");

    int y = 100;
    jt_text_centered(y, JT_COL_WHITE, JT_COL_BLACK,
                     "Version " JT_VERSION_STR);
    y += 36;
    jt_text_centered(y, JT_COL_CYAN, JT_COL_BLACK,
                     "Built on KallistiOS");
    y += 32;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Video:  %s", jt_cable_name(jt_video_cable()));
    y += 24;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Region: %s", jt_region_name(jt_video_region()));
    y += 24;
    jt_text_centered(y, JT_COL_GREY, JT_COL_BLACK,
                     "Mode:   %s",
                     jt_video_is_progressive() ? "Progressive 60Hz" : "Interlaced 480i");
    y += 48;

    jt_text_centered(y, JT_COL_WHITE, JT_COL_BLACK,
                     "Tests every maple-bus device class the DC supports.");
    y += 24;
    jt_text_centered(y, JT_COL_WHITE, JT_COL_BLACK,
                     "Includes a VMU icon editor (mode unlocked in v0.2).");

    jt_text_centered(408, JT_COL_GREY, JT_COL_BLACK,
                     "github.com/joypad-ai/joypad-tester");
    jt_text_centered(456, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_about = {
    .name   = "About",
    .enter  = about_enter,
    .leave  = about_leave,
    .update = about_update,
    .draw   = about_draw,
};
