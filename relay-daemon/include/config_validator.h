#ifndef RELAY_CONFIG_VALIDATOR_H
#define RELAY_CONFIG_VALIDATOR_H

#include "relay.h"
#include "config.h"

/* Validates option values beyond presence checks.
 * Returns number of errors written to the `errors` array. */
int config_validate_options(const config_t *cfg,
                            char errors[][RELAY_MAX_VALUE],
                            int max_errors);

#endif /* RELAY_CONFIG_VALIDATOR_H */
