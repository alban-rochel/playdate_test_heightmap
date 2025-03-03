#pragma once

#ifdef EDITOR
#include <stdint.h>
void initDisplay(void);
void setTerrainData(uint8_t* data);
void upPushed(void);
void downPushed(void);
void leftPushed(void);
void rightPushed(void);
void aPushed(void);
void bPushed(void);
#else
#include "pd_api.h"
void initDisplay(PlaydateAPI* pd);
#endif

int draw(uint8_t* frameBuffer);

