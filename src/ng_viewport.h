// agent: composer-2.5 | 2026-07-25 | viewport resize debounce | 7c1e4a
#ifndef NG_VIEWPORT_H
#define NG_VIEWPORT_H

void ng_viewport_init(int width, int height);
void ng_viewport_poll(void);
int ng_viewport_width(void);
int ng_viewport_height(void);

#endif
