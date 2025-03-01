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

//uint8_t screenBuffer[LCD_COLUMNS * LCD_ROWS];

static int update(void* userdata);
static int update2(void* userdata);
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

fixed_t frameCount = 0;

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

struct Line
{
	int32_t x;
	uint32_t pattern;
};

struct Line lines[240];

static void initLines(void)
{
	for(int i = 0; i < 240; ++i)
	{
		lines[i].x = -1;
		lines[i].pattern = 0;
	}
}

static inline void flushLines(uint32_t* frameBuffer)
{
	/*for(int i = 0; i < 240; ++i)
	{
		struct Line* currentLine = &lines[i];
		if(currentLine->x >= 0)
		{
			uint32_t* row = frameBuffer + (i * LCD_STRIDE)/4;
			drawFragment(row, currentLine->x, LCD_COLUMNS, currentLine->pattern);
		}
	}*/
}

static inline void addToLine(PlaydateAPI* pd, uint32_t* frameBuffer, uint8_t line, int col, uint32_t pattern)
{
	struct Line* currentLine = &lines[line];


	if(currentLine->x < 0)
	{
		currentLine->x = col;
		currentLine->pattern = pattern;
	}
	else
	{
		#if 1
		if(currentLine->pattern != pattern)
		{
			uint32_t* row = frameBuffer + (line * LCD_STRIDE)/4;
			if(line == 200)
			{
				pd->system->logToConsole("Change %d %d %d", col, currentLine->x, pattern);
			}
			drawFragment(row, currentLine->x, col, currentLine->pattern);
			currentLine->x = col;
			currentLine->pattern = pattern;
		}
		#else
		{
			uint32_t* row = frameBuffer + (line * LCD_STRIDE)/4;
			drawFragment(row, col-1, col, pattern);
		}
		#endif
	}

}

static inline void baseWave(fixed_t pLeftX, fixed_t pLeftY, fixed_t* height)
{
	uint32_t leftX = FIXED_TO_INT32(pLeftX+10*frameCount)&1023;
	uint32_t leftY = FIXED_TO_INT32(pLeftY+10*frameCount)&1023;

	fixed_t _cos = cos_lut[leftX]+INT32_TO_FIXED(1);
	fixed_t _sin = sin_lut[leftY]+INT32_TO_FIXED(1);
	*height = FIXED_MUL(_cos + _sin, INT32_TO_FIXED(3));
}

static inline void rippleWave(fixed_t pLeftX, fixed_t pLeftY, fixed_t* height)
{
	uint32_t leftX = FIXED_TO_INT32(pLeftX+3*frameCount)&1023;
	uint32_t leftY = FIXED_TO_INT32(pLeftY+5*frameCount)&1023;

	fixed_t _cos = cos_lut[leftX]+INT32_TO_FIXED(1);
	fixed_t _sin = sin_lut[leftY]+INT32_TO_FIXED(1);
	*height += FIXED_MUL(_cos + _sin, INT32_TO_FIXED(3));
}

static inline void computeHeight(fixed_t pLeftX, fixed_t pLeftY, fixed_t* height, uint8_t* patternIndex)
{
	*height = 0;

	uint32_t leftX = FIXED_TO_INT32(pLeftX)&1023;
	uint32_t leftY = FIXED_TO_INT32(pLeftY)&1023;
	uint8_t heightAndColor = terrainData[leftX*1024+leftY];
	if((heightAndColor >> 3) > 3)
	{
		*height = FIXED_MUL(heightAndColor & 0xF8, INT32_TO_FIXED(128));
		*patternIndex = ((heightAndColor & 0x7) >= 3  ? 0 : 3);
	}
	else
	{
		baseWave(pLeftX, pLeftY, height);
		rippleWave(pLeftX, pLeftY, height);
		*patternIndex = 6;
	}
	//*pattern = ((heightAndColor & 0x7) >= 3  ? 0x5A5A5A5A : 0xFFFFFFFF);
	/*baseWave(pLeftX, pLeftY, height);
	rippleWave(pLeftX, pLeftY, height);*/
}

static int update(void* userdata)
{
	initLines();

	static uint16_t patterns[] = {0x0000, 0xA0A0, 0xA4A0, 0xA5A5, 0xFADA, 0xFAFA, 0xFFFF};

	PlaydateAPI* pd = userdata;
	checkButtons(pd);

	pd->graphics->clear(kColorWhite);
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
				const fixed_t height;
				const fixed_t normal;
				uint8_t patternIndex;
				computeHeight(pLeftX, pLeftY, &height, &patternIndex);

				uint16_t pattern = patterns[patternIndex];
				pattern = pattern >> (4*(col%0b11));

				fixed_t heightOnScreen = FIXED_MUL(FIXED_DIV(FIXED_MUL(height, scaleHeight) - hMin, (hMax - hMin)), INT32_TO_FIXED(239));
				uint8_t heightOnScreen8 = MAX(MIN(FIXED_TO_INT32(heightOnScreen), 239), 0);
				uint8_t colOffsetInByte = 7-col%8;

				if(heightPerColumn[col] <= heightOnScreen8)
				{
					for(uint8_t h = 240-heightOnScreen8; h < 240 - heightPerColumn[col]; ++h)
					{
						uint8_t patternOffset = h&0b11;
						uint8_t bit = (pattern >> patternOffset)&0x1;
						uint8_t* word = frameBuffer + h * LCD_STRIDE + (col/8);
						if(bit)
						{
							SET_BIT(*word, colOffsetInByte);
						}
						else
						{
							UNSET_BIT(*word, colOffsetInByte);
						}
						//addToLine(pd, frameBuffer, h, col, pattern);
					}
					heightPerColumn[col] = heightOnScreen8;
				}
				else
				{
					uint8_t h = 240-heightPerColumn[col];
					uint8_t* word = frameBuffer + h * LCD_STRIDE + (col/8);
					UNSET_BIT(*word, colOffsetInByte);
					UNSET_BIT(*(word+52), colOffsetInByte);
					//addToLine(pd, frameBuffer, h, col, 0);
				}

				pLeftX += dx;
				pLeftY += dy;
			}


		z = FIXED_MUL(z, dz);
	}

	flushLines(frameBuffer);

	// Draw the current FPS on the screen
	pd->system->drawFPS(0, 0);

	frameCount += INT32_TO_FIXED(1);

	return 1;
}

static int update2(void* userdata)
{
	initLines();

	PlaydateAPI* pd = userdata;

	pd->graphics->clear(kColorBlack);
	uint8_t* frameBuffer = pd->graphics->getFrame();

	for(int plop = 0; plop < 240; ++plop)
	{
		addToLine(pd, frameBuffer, plop, 0, 0xFFFFFFFF);
		addToLine(pd, frameBuffer, plop, plop, 0x0);
		addToLine(pd, frameBuffer, plop, plop+1, 0x0);
		addToLine(pd, frameBuffer, plop, plop+2, 0x0);
		addToLine(pd, frameBuffer, plop, plop+4, 0x0);
		addToLine(pd, frameBuffer, plop, plop+5, 0xFFFFFFFF);
		addToLine(pd, frameBuffer, plop, plop+6, 0x0);
		addToLine(pd, frameBuffer, plop, plop+7, 0xFFFFFFFF);
		addToLine(pd, frameBuffer, plop, plop+8, 0x0);
		addToLine(pd, frameBuffer, plop, plop+9, 0xFF00FF00);
	}

	flushLines(frameBuffer);

	return 1;
}


