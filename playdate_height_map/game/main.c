#include <stdio.h>
#include <stdlib.h>

#include "display.h"

int update(void* userdata)
{
	PlaydateAPI* pd = userdata;
	checkButtons(pd);

	pd->graphics->clear(kColorWhite);

	uint8_t* frameBuffer = pd->graphics->getFrame();

	draw(frameBuffer);

	// Draw the current FPS on the screen

	pd->system->drawFPS(0, 0);

	return 1;
}

void init(PlaydateAPI* pd)
{
	initDisplay(pd);

  pd->display->setRefreshRate(50.f);
  // Note: If you set an update callback in the kEventInit handler, the system assumes the game is pure C and doesn't run any Lua code in the game
  pd->system->setUpdateCallback(update, pd);
}

int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg)
{
	(void)arg; // arg is currently only used for event = kEventKeyPressed

	if ( event == kEventInit )
	{
		init(pd);
		initDisplay(pd);
	}

	return 0;
}
