/*
 * joypad-tester for the OG Xbox.
 *
 * v0.1.0 boot smoke-test: clears the framebuffer and prints the
 * version string via nxdk's debugPrint. Lets us confirm the toolchain
 * is wired up before we hook up XID input / MU detection.
 */
#include <hal/video.h>
#include <hal/debug.h>
#include <hal/xbox.h>
#include <windows.h>

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    debugClearScreen();
    debugPrint("joypad-tester xbox v0.1.0\n");
    debugPrint("boot ok -- toolchain wired up\n");

    /* Idle until the user resets. The next iteration replaces this
     * with the main loop + tester mode. */
    while (1) {
        Sleep(2000);
    }

    return 0;
}
