#ifndef JT_XBOX_TESTER_H
#define JT_XBOX_TESTER_H

/* Render the tester view for this frame. Caller must have already
 * pumped SDL events, called jt_ports_poll(), and prepared the back
 * buffer (pb_target_back_buffer + pb_fill + pb_erase_text_screen).
 * tester_draw only writes the text layer; the caller flushes and
 * flips. */
void jt_tester_draw(void);

#endif /* JT_XBOX_TESTER_H */
