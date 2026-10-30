#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { KEY_CONSUMER_COUNT=13 };
/* Report 0=no-op, 1=keyboard, 2=consumer, 3=system-control bit. */
bool key_code_lookup(const char *token,uint8_t *report,uint16_t *usage);
uint16_t key_code_consumer_usage(unsigned index);
