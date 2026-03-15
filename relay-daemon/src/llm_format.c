#include "llm_format.h"

#include <stdio.h>
#include <string.h>

static const char *STRUCTURED_FORMAT_GUIDE =
    "Use this layout for every reply:\n"
    "## Section Title (tailored to the topic)\n"
    "**1. Subheading**\n"
    "- Bullet sentence focused on one idea\n"
    "- Bullet sentence focused on one idea\n"
    "## Next Section Title\n"
    "**2. Subheading**\n"
    "- Continue with concise bullets (max ~6 per section)\n"
    "\nRules:\n"
    "- Start immediately with a '##' heading (no intro paragraph).\n"
    "- Each subsection label is bold + numbered like '**3. Customer Insight**'.\n"
    "- Bullets must be single sentences, imperative or action-oriented when possible.\n"
    "- Avoid long paragraphs, tables, or trailing commentary outside bullet lists.\n"
    "- Tailor section titles and numbered labels to the actual question/context.\n";

void llm_append_structured_format_instructions(char *buf, size_t max)
{
    if (!buf || max == 0) {
        return;
    }

    size_t used = strnlen(buf, max);
    if (used >= max - 1) {
        return;
    }

    size_t remaining = max - used;
    const char *prefix = "\n\nFormatting instructions:\n";

    int written = snprintf(buf + used, remaining, "%s%s",
                           prefix, STRUCTURED_FORMAT_GUIDE);

    (void)written;
}
