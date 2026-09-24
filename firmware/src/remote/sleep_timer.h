#pragma once
void sleep_timer_init(void (*on_expire)(void));
void reset_sleep_timer(void);
