#pragma once
#include <stdbool.h>

void ir_init(void);
void ir_set(bool on);
bool ir_get(void);   // last value passed to ir_set() (false after ir_init)
