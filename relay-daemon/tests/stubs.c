/* stubs.c — test-build stubs for symbols defined in main.c
 *
 * These no-op implementations allow event_loop.c to be included in the
 * test build for compile-time coverage without pulling in main.c itself. */

void proc_set_current_chat_id(const char *chat_id) { (void)chat_id; }
