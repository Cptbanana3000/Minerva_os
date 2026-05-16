#ifndef MINERVA_DESKTOP_H
#define MINERVA_DESKTOP_H

void desktop_init(void);
int  desktop_process(void);   /* returns 1 if scene needs redraw */
void desktop_redraw(void);

#endif
