//
//  main.c
//  Extension
//
//  Created by Dave Hayden on 7/30/14.
//  Copyright (c) 2014 Panic, Inc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>

#include "pd_api.h"

#define FIXED_POINT_SHIFT 8
#define fixed_t int32_t
#define FLOAT_TO_FIXED(x) ((fixed_t)((x) * (1 << FIXED_POINT_SHIFT)))
#define FIXED_TO_FLOAT(x) ((float)(x) / (1 << FIXED_POINT_SHIFT))
#define INT32_TO_FIXED(x) ((fixed_t)((x) << FIXED_POINT_SHIFT))
#define FIXED_TO_INT32(x) ((int32_t)(x) >> FIXED_POINT_SHIFT)
#define FIXED_MUL(x, y) (((x) * (y)) >> FIXED_POINT_SHIFT)
#define FIXED_DIV(x, y) (((x) << FIXED_POINT_SHIFT) / (y))

#define LCD_COLUMNS 400
#define LCD_ROWS 240
#define LCD_STRIDE 52

uint8_t screenBuffer[LCD_COLUMNS * LCD_ROWS];

static int update(void* userdata);
uint8_t* terrainData;
uint8_t* heightPerColumn;

#define LUT_SIZE 1024
#define LUT_MASK 1023
#define angle_t int32_t
fixed_t cos_lut[LUT_SIZE];
fixed_t sin_lut[LUT_SIZE];
#define DEG_45 (LUT_SIZE/8)
#define DEG_90 (LUT_SIZE/4)
#define DEG_180 (LUT_SIZE/2)


angle_t phi = DEG_45;
const angle_t fov = DEG_90;
const fixed_t vFov = fov*240/400;
fixed_t posX = 0;
fixed_t posY = 0;
fixed_t posZ = INT32_TO_FIXED(200);
const fixed_t horizon = INT32_TO_FIXED(120);
const fixed_t scaleHeight = FLOAT_TO_FIXED(1.5f);
const fixed_t distance = FLOAT_TO_FIXED(1024.f);
const fixed_t speed = FLOAT_TO_FIXED(5.f);

static fixed_t fixedCos(angle_t angle)
{
	return cos_lut[angle & LUT_MASK];
}

static fixed_t fixedSin(angle_t angle)
{
	return sin_lut[angle & LUT_MASK];
}

static inline uint32_t swap(uint32_t n)
{
#if TARGET_PLAYDATE
	//return __REV(n);
	uint32_t result;
	
	__asm volatile ("rev %0, %1" : "=l" (result) : "l" (n));
	return(result);
#else
	return ((n & 0xff000000) >> 24) | ((n & 0xff0000) >> 8) | ((n & 0xff00) << 8) | (n << 24);
#endif
}

static inline void
_drawMaskPattern(uint32_t* p, uint32_t mask, uint32_t color)
{
	if ( mask == 0xffffffff )
		*p = color;
	else
		*p = (*p & ~mask) | (color & mask);
}

static void
drawFragment(uint32_t* row, int x1, int x2, uint32_t color)
{
	if ( x2 < 0 || x1 >= LCD_COLUMNS )
		return;
	
	if ( x1 < 0 )
		x1 = 0;
	
	if ( x2 > LCD_COLUMNS )
		x2 = LCD_COLUMNS;
	
	if ( x1 > x2 )
		return;
	
	// Operate on 32 bits at a time
	
	int startbit = x1 % 32;
	uint32_t startmask = swap((1 << (32 - startbit)) - 1);
	int endbit = x2 % 32;
	uint32_t endmask = swap(((1 << endbit) - 1) << (32 - endbit));
	
	int col = x1 / 32;
	uint32_t* p = row + col;

	if ( col == x2 / 32 )
	{
		uint32_t mask = 0;
		
		if ( startbit > 0 && endbit > 0 )
			mask = startmask & endmask;
		else if ( startbit > 0 )
			mask = startmask;
		else if ( endbit > 0 )
			mask = endmask;
		
		_drawMaskPattern(p, mask, color);
	}
	else
	{
		int x = x1;
		
		if ( startbit > 0 )
		{
			_drawMaskPattern(p++, startmask, color);
			x += (32 - startbit);
		}
		
		while ( x + 32 <= x2 )
		{
			_drawMaskPattern(p++, 0xffffffff, color);
			x += 32;
		}
		
		if ( endbit > 0 )
			_drawMaskPattern(p, endmask, color);
	}
}

void initLut(void)
{
	for(int i = 0; i < LUT_SIZE; ++i)
	{
		cos_lut[i] = FLOAT_TO_FIXED(cos(i / (float)LUT_SIZE * 2.f * (float)M_PI));
		sin_lut[i] = FLOAT_TO_FIXED(sin(i / (float)LUT_SIZE * 2.f * (float)M_PI));
	}
}

void loadFile(PlaydateAPI* pd)
{
	pd->system->logToConsole("opening file");
	SDFile* file = pd->file->open("./images/test.terrain", kFileRead);
	if(file == NULL)
	{
		pd->system->logToConsole("Error open %s", pd->file->geterr());
		return;
	}
	terrainData = pd->system->realloc(terrainData, 1024*1024);
	if(terrainData == NULL)
	{
		pd->system->logToConsole("Error alloc");
		return;
	}

	if(pd->file->read(file, terrainData, 1024*1024) < 0)
	{
		pd->system->logToConsole("Error read");
		return;
	}
	pd->system->logToConsole("READ OK");
}

int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg)
{
	(void)arg; // arg is currently only used for event = kEventKeyPressed

	if ( event == kEventInit )
	{
		//const char* err;

		initLut();

		heightPerColumn = pd->system->realloc(heightPerColumn, LCD_COLUMNS);

		loadFile(pd);
		pd->display->setRefreshRate(50.f);
		// Note: If you set an update callback in the kEventInit handler, the system assumes the game is pure C and doesn't run any Lua code in the game
		pd->system->setUpdateCallback(update, pd);
	}

	return 0;
}

#if !defined(MIN)
#define MIN(a, b) (((a)<(b))?(a):(b))
#endif

#if !defined(MAX)
#define MAX(a, b) (((a)>(b))?(a):(b))
#endif

#define SET_BIT(byte, offset) {(byte) = (byte) | (1  << (offset));}
#define UNSET_BIT(byte, offset) {(byte) = (byte) & ~(1  << (offset));}

void checkButtons(PlaydateAPI* pd)
{
	PDButtons pushed;
	PDButtons current;
	pd->system->getButtonState(&current, &pushed, NULL);

	if(current & kButtonUp)
	{
		posX += FIXED_MUL(speed, fixedCos(phi));
		posY += FIXED_MUL(speed, fixedSin(phi));
	}
	if(current & kButtonDown)
	{
		posX -= FIXED_MUL(speed, fixedCos(phi));
		posY -= FIXED_MUL(speed, fixedSin(phi));
	}

	if(current & kButtonLeft)
	{
			phi+=FLOAT_TO_FIXED(.1f);
	}
	if(current & kButtonRight)
	{
			phi-=FLOAT_TO_FIXED(.1f);
	}

	if(current & kButtonB)
	{
		posX += FIXED_MUL(speed, fixedCos(phi + DEG_90));
		posY += FIXED_MUL(speed, fixedSin(phi + DEG_90));
	}

	if(current & kButtonA)
	{
		posX += FIXED_MUL(speed, fixedCos(phi - DEG_90));
		posY += FIXED_MUL(speed, fixedSin(phi - DEG_90));
	}

	posZ = FLOAT_TO_FIXED(pd->system->getCrankAngle()) + FLOAT_TO_FIXED(200.f);
}

struct Pattern
{
	uint32_t color;
	uint32_t size;
};
struct Line
{
	uint8_t count;
	struct Pattern patterns[50];
};

struct Line pending[240];

static void initLines(void)
{
	for(int i = 0; i < 240; ++i)
	{
		pending[i].count = 0;
	}
}

static inline void addToLine(uint8_t line, int col, uint32_t color)
{
	struct Line* currentLine = &pending[line];
	if(col == 0 || currentLine->patterns[currentLine->count].color != color)
	{
		currentLine->patterns[currentLine->count].color = color;
		currentLine->patterns[currentLine->count].size = 1;
		currentLine->count++;
	}
	else
	{
		currentLine->patterns[currentLine->count-1].size++;
	}
}

static int update(void* userdata)
{
	initLines();

	static uint16_t patterns[] = {0xFFFF, 0x5F5F, 0x5E5B, 0x5A5A, 0xA1A4, 0xA0A0, 0x0000, 0x0000};

	PlaydateAPI* pd = userdata;
	checkButtons(pd);

	pd->graphics->clear(kColorBlack);
	uint8_t* frameBuffer = pd->graphics->getFrame();

	fixed_t cosLeft = fixedCos(phi + fov / 2);
	fixed_t sinLeft = fixedSin(phi + fov / 2);
	fixed_t cosRight = fixedCos(phi - fov / 2);
	fixed_t sinRight = fixedSin(phi - fov / 2);
	fixed_t tanVFov = FIXED_DIV(fixedSin(vFov), fixedCos(vFov));

	memset(heightPerColumn, 0, LCD_COLUMNS);

	fixed_t dz = FLOAT_TO_FIXED(1.1f);
	fixed_t z = FLOAT_TO_FIXED(1.f);
	while(z < distance)
	{
		fixed_t pLeftX = posX + FIXED_MUL(z, cosLeft);
		fixed_t pLeftY = posY + FIXED_MUL(z, sinLeft);
		fixed_t pRightX = posX + FIXED_MUL(z, cosRight);
		fixed_t pRightY = posY + FIXED_MUL(z, sinRight);

		fixed_t dx = (pRightX - pLeftX)/LCD_COLUMNS;
		fixed_t dy = (pRightY - pLeftY)/LCD_COLUMNS;

		fixed_t hMin = posZ - FIXED_MUL(z, tanVFov);
		fixed_t hMax = posZ + FIXED_MUL(z, tanVFov);

			for(uint32_t col = 0; col < LCD_COLUMNS; ++col)
			{
				uint32_t leftX = FIXED_TO_INT32(pLeftX)&1023;
				uint32_t leftY = FIXED_TO_INT32(pLeftY)&1023;
				const fixed_t height = FIXED_MUL(cos_lut[leftX] + sin_lut[leftY], INT32_TO_FIXED(128));
				const fixed_t height2 = FIXED_MUL(cos_lut[(leftX+1)%1023] + sin_lut[leftY], INT32_TO_FIXED(128));
				uint8_t color = (height2 < height? 3 : 0);

				fixed_t heightOnScreen = FIXED_MUL(FIXED_DIV(FIXED_MUL(height, scaleHeight) - hMin, (hMax - hMin)), INT32_TO_FIXED(239));
				uint8_t heightOnScreen8 = MAX(MIN(FIXED_TO_INT32(heightOnScreen), 239), 0);
				
				const uint16_t pattern = (patterns[color]) >> ((col&0b11)*4);

				uint8_t offset = 7-(col%8);

				if(heightPerColumn[col] <= heightOnScreen8)
				{
					for(uint8_t h = 240-heightOnScreen8; h < 240 - heightPerColumn[col]; ++h)
					{
						uint8_t offset2 = h&0b11;
						uint8_t bit = pattern & (1 << offset2);
						uint8_t* word = frameBuffer + h * LCD_STRIDE + (col/8);
						if(bit)
						{
							SET_BIT(*word, offset);
						}
						else
						{
							UNSET_BIT(*word, offset);
						}
						//addToLine(h, col, color);
					}
					heightPerColumn[col] = heightOnScreen8;
				}
				else
				{
					uint8_t h = 240-heightPerColumn[col];
					uint8_t* word = frameBuffer + h * LCD_STRIDE + (col/8);
					UNSET_BIT(*word, offset);
					//addToLine(h, col, 0);
				}

				pLeftX += dx;
				pLeftY += dy;
			}


		z = FIXED_MUL(z, dz);
	}

	// Draw the current FPS on the screen
	pd->system->drawFPS(0, 0);

	return 1;
}


