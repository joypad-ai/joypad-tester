/*
 * joypad-tester for the OG Xbox.
 *
 * Boot path mirrors nxdk's sdl_gamecontroller sample: XVideoSetMode
 * to bring up 640x480x32, SDL_Init for gamecontroller, pbkit for
 * NV2A push-buffer rendering, then a vsync-locked main loop that
 * pumps SDL events, polls jt_ports, and lets modes/tester draw the
 * text overlay.
 */
#include <hal/video.h>
#include <hal/debug.h>
#include <hal/xbox.h>
#include <SDL.h>
#include <pbkit/pbkit.h>
#include <windows.h>

#include "ports/ports.h"
#include "modes/tester.h"

static void wait_then_reboot(void)
{
    Sleep(5000);
    XReboot();
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        debugPrint("SDL_Init failed: %s\n", SDL_GetError());
        wait_then_reboot();
        return 1;
    }

    if (pb_init() != 0) {
        debugPrint("pbkit init failed\n");
        SDL_Quit();
        wait_then_reboot();
        return 1;
    }
    pb_show_front_screen();

    jt_ports_init();

    while (1) {
        pb_wait_for_vbl();
        pb_target_back_buffer();
        pb_reset();
        pb_fill(0, 0, 640, 480, 0);
        pb_erase_text_screen();

        jt_ports_poll();
        jt_tester_draw();

        pb_draw_text_screen();
        while (pb_busy());
        while (pb_finished());
    }

    /* Unreached, but tidy the cleanup path for when modes evolve. */
    pb_kill();
    SDL_Quit();
    return 0;
}
