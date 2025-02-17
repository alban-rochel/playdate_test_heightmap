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

#define LCD_COLUMNS 400
#define LCD_ROWS 240
#define LCD_STRIDE 52

static int update(void* userdata);
uint8_t* terrainData;
uint8_t* heightPerColumn;

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

		heightPerColumn = pd->system->realloc(heightPerColumn, LCD_COLUMNS);

		loadFile(pd);

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

uint32_t frame = 0;

float phi = 45.f/180.f * M_PI;
float fov = 90.f / 180.f * M_PI;
float vFov = 90.f / 180.f * M_PI * 240.f/400.f;
float posX = 0.f;
float posY = 0.f;
float posZ = 200.f;
float horizon = 120.f;
float scaleHeight = 1.5f;
float distance = 1024.f;
float speed = 5.f;

void checkButtons(PlaydateAPI* pd)
{
	PDButtons pushed;
	PDButtons current;
	pd->system->getButtonState(&current, &pushed, NULL);

	if(current & kButtonUp)
	{
		posX += speed * cos(phi);
		posY += speed * sin(phi);
	}
	if(current & kButtonDown)
	{
		posX -= speed * cos(phi);
		posY -= speed * sin(phi);
	}

	if(current & kButtonLeft)
	{
		phi+=0.1f;
	}
		if(current & kButtonRight)
	{
		phi-=0.1f;
	}

	if(current & kButtonB)
	{
		posX += speed * cos(phi + M_PI/2);
		posY += speed * sin(phi + M_PI/2);
	}

		if(current & kButtonA)
	{
		posX += speed * cos(phi - M_PI/2);
		posY += speed * sin(phi - M_PI/2);
	}

	posZ = pd->system->getCrankAngle() + 200.f;

}

static int update(void* userdata)
{
	static uint16_t patterns[] = {0xFFFF, 0x5F5F, 0x5E5B, 0x5A5A, 0xA1A4, 0xA0A0, 0x0000, 0x0000};

	PlaydateAPI* pd = userdata;
	checkButtons(pd);

	pd->graphics->clear(kColorBlack);
	uint8_t* frameBuffer = pd->graphics->getFrame();

	float cosLeft = cos(phi + fov/2.f);
	float sinLeft = sin(phi + fov/2.f);
	float cosRight = cos(phi - fov/2.f);
	float sinRight = sin(phi - fov/2.f);
	float tanVFov = tan(vFov);

	for(uint32_t col = 0; col < LCD_COLUMNS; ++col)
	{
		heightPerColumn[col] = 0;
	}

	float dz = 1.f;
	float z = 1.f;
	while(z < distance)
	{
		float pLeftX = posX + z * cosLeft;
		float pLeftY = posY + z * sinLeft;
		float pRightX = posX + z * cosRight;
		float pRightY = posY + z * sinRight;

		float dx = (pRightX - pLeftX)/LCD_COLUMNS;
		float dy = (pRightY - pLeftY)/LCD_COLUMNS;

		float hMin = posZ - z * tanVFov;
		float hMax = posZ + z * tanVFov;

		for(uint32_t col = 0; col < LCD_COLUMNS; ++col)
		{
			uint32_t leftX = (uint32_t)(pLeftX+0.5f)%1024;

			uint32_t leftY = (uint32_t)(pLeftY+0.5f)%1024;
			const uint8_t colorAndHeight = terrainData[leftX * 1024 + leftY];
			const uint8_t color = (colorAndHeight & 0x7);
			const float height = colorAndHeight & 0xF8;
			//uint8_t heightOnScreen = MIN((height-posZ), 240);
			float_t heightOnScreen = (height*scaleHeight - hMin)/(hMax - hMin) * 239.f;
			heightOnScreen = MAX(MIN(heightOnScreen, 239.f), 0.f);
			uint8_t heightOnScreen8 = heightOnScreen;

			const uint16_t pattern = (patterns[color]) >> ((col&0b11)*4);

			uint8_t offset = 7-(col%8);

			if(heightPerColumn[col] < heightOnScreen8)
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
				}
				heightPerColumn[col] = heightOnScreen8;
			}


			pLeftX += dx;
			pLeftY += dy;
		}
		z += dz;
    dz += 0.2f;
	}

	// Draw the current FPS on the screen
	pd->system->drawFPS(0, 0);

	return 1;
}


