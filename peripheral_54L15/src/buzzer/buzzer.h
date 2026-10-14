#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

void buzzer_init(void);
void buzzer_on(uint32_t duration_ms);

#endif /* BUZZER_H */