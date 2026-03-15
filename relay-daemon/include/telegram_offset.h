#ifndef RELAY_TELEGRAM_OFFSET_H
#define RELAY_TELEGRAM_OFFSET_H

/*
 * telegram_offset — persists the Telegram getUpdates offset across restarts.
 *
 * The offset prevents relay from reprocessing already-handled messages
 * (including /restart) when the daemon restarts.
 *
 * File location: <workspace>/data/state/telegram-offset.txt
 * Format: plain decimal long long followed by newline
 */

/*
 * Save the offset to disk atomically.
 * Returns 0 on success, non-zero on error.
 * No-op (returns non-zero) if workspace is NULL.
 */
int telegram_offset_save(const char *workspace, long long offset);

/*
 * Load the offset from disk.
 * Returns the saved offset, or 0 if the file does not exist or cannot be read.
 * Returns 0 if workspace is NULL.
 */
long long telegram_offset_load(const char *workspace);

#endif /* RELAY_TELEGRAM_OFFSET_H */
