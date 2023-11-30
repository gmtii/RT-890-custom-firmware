/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "misc.h"
#include "app/spectrum.h"
#include "app/radio.h"
#include "driver/bk4819.h"
#include "driver/delay.h"
#include "driver/key.h"
#include "driver/pins.h"
#include "driver/speaker.h"
#include "helper/helper.h"
#include "helper/inputbox.h"
#include "radio/channels.h"
#include "radio/settings.h"
#include "ui/gfx.h"
#include "ui/helper.h"
#include "ui/main.h"

#include "gradient.h"
#include "driver/st7735s.h"

#ifdef UART_DEBUG
#include "driver/uart.h"
#include "external/printf/printf.h"
#endif

uint32_t CurrentFreq;
uint8_t CurrentFreqIndex;
uint8_t CurrentFreqIndex_old;

uint32_t FreqCenter;
uint32_t FreqMin;
uint32_t FreqMax;

uint8_t CurrentModulation;
uint8_t CurrentFreqStepIndex;
uint32_t CurrentFreqStep;
uint32_t CurrentFreqChangeStep;
uint8_t CurrentStepCountIndex;
uint8_t CurrentStepCount;
uint16_t CurrentScanDelay;

uint16_t RssiValue_v[128] = {0};

uint16_t SquelchLevel;

uint8_t bExit;
uint8_t bRXMode;
uint8_t bResetSquelch;
uint8_t bRestartScan;

uint8_t bFilterEnabled;
uint8_t bNarrow;

uint16_t RssiLow;
uint16_t RssiHigh;

uint16_t KeyHoldTimer = 0;
uint8_t bHold;
KEY_t Key;
KEY_t LastKey = KEY_NONE;

uint8_t scroll;

////////////////////////////////////////////////////////////////

// WATERFALL AND SPECTRUM

#define SPECTRUM_WIDTH 160
#define WATERFALL_HEIGHT 1

uint8_t offset = 0;

uint16_t RssiValue;
uint16_t RssiValue_old;

uint16_t pixelnew[SPECTRUM_WIDTH] = {0};
uint16_t pixelold[SPECTRUM_WIDTH] = {0};

uint8_t waterfall[WATERFALL_HEIGHT][SPECTRUM_WIDTH];

uint8_t cnt, waterfall_line;

const char *StepStrings[] = {
	"0.25K",
	"1.25K",
	"2.5K ",
	"5K   ",
	"6.25K",
	"10K  ",
	"12.5k",
	"20K  ",
	"25K  ",
	"50K  ",
	"100K ",
	"500K ",
	"1M   ",
	"5M   "};

static const char Mode[3][2] = {
	"FM",
	"AM",
	"SB",
};

////////////////////////////////////////////////////////////////

void ShiftShortStringRight(uint8_t Start, uint8_t End)
{
	for (uint8_t i = End; i > Start; i--)
	{
		gShortString[i + 1] = gShortString[i];
	}
}

void DrawCurrentFreq(uint16_t Color)
{

	gColorForeground = Color;
	Int2Ascii(CurrentFreq, 8);
	ShiftShortStringRight(2, 6);
	gShortString[3] = '.';
	UI_DrawSmallString(2, 20, gShortString, 7);

	UI_DrawSmallString(2, 50, Mode[CurrentModulation], 2);

	gColorForeground = Color;
	gShortString[2] = ' ';
	Int2Ascii(SquelchLevel, (SquelchLevel < 100) ? 2 : 3);
	UI_DrawSmallString(2, 40, gShortString, 3);

	gColorForeground = COLOR_RED;
	gShortString[2] = ' ';
	Int2Ascii(RssiValue_v[CurrentFreqIndex], (RssiValue_v[CurrentFreqIndex] < 100) ? 2 : 3);
	UI_DrawSmallString(25, 40, gShortString, 3);
}

void DrawLabels(void)
{

	gColorForeground = COLOR_FOREGROUND;

	Int2Ascii(FreqMin / 10, 7);
	for (uint8_t i = 6; i > 2; i--)
	{
		gShortString[i + 1] = gShortString[i];
	}
	gShortString[3] = '.';
	UI_DrawSmallString(2, 2, gShortString, 8);

	Int2Ascii(FreqMax / 10, 7);
	for (uint8_t i = 6; i > 2; i--)
	{
		gShortString[i + 1] = gShortString[i];
	}
	gShortString[3] = '.';
	UI_DrawSmallString(2, 88, gShortString, 8);

	// gShortString[2] = ' ';
	// Int2Ascii(CurrentStepCount, (CurrentStepCount < 100) ? 2 : 3);
	// UI_DrawSmallString(2, 72, gShortString, 3);

	UI_DrawSmallString(25, 72, StepStrings[CurrentFreqStepIndex], 5);

	UI_DrawSmallString(2, 60, (bFilterEnabled) ? "F" : "U", 1);

	UI_DrawSmallString(15, 60, (bNarrow) ? "N" : "W", 1);

	UI_DrawSmallString(30, 60, (bHold) ? "H" : " ", 1);

	// Int2Ascii(offset, 5);
	// UI_DrawSmallString(2, 20, gShortString, 5);

	gColorForeground = COLOR_GREY;

	Int2Ascii(CurrentFreqChangeStep / 10, 5);
	for (uint8_t i = 4; i > 0; i--)
	{
		gShortString[i + 1] = gShortString[i];
	}
	gShortString[1] = '.';
	UI_DrawSmallString(2, 30, gShortString, 6);
}
////////////////////////////////////////////////////////////////

void SetFreqMinMax(void)
{
	CurrentFreqChangeStep = CurrentFreqStep * (128 >> 1);
	FreqMin = FreqCenter - CurrentFreqChangeStep;
	FreqMax = FreqCenter + CurrentFreqChangeStep;
	FREQUENCY_SelectBand(FreqCenter);
	BK4819_EnableFilter(bFilterEnabled);
	RssiValue_v[CurrentFreqIndex] = 0; // Force a rescan
}

////////////////////////////////////////////////////////////////

void SetStepCount(void)
{
	CurrentStepCount = 128 >> CurrentStepCountIndex;
}

////////////////////////////////////////////////////////////////

void IncrementStepIndex(void)
{
	CurrentStepCountIndex = (CurrentStepCountIndex + 1) % STEPS_COUNT;
	SetStepCount();
	SetFreqMinMax();
	DrawLabels();
}

////////////////////////////////////////////////////////////////

void IncrementFreqStepIndex(void)
{
	CurrentFreqStepIndex = (CurrentFreqStepIndex + 1) % 10;
	CurrentFreqStep = FREQUENCY_GetStep(CurrentFreqStepIndex);
	SetFreqMinMax();
	DrawLabels();
}

////////////////////////////////////////////////////////////////

void IncrementScanDelay(void)
{
	CurrentScanDelay = (CurrentScanDelay + 5) % 45;
	DrawLabels();
}

////////////////////////////////////////////////////////////////

void ChangeCenterFreq(uint8_t Up)
{
	if (Up)
	{
		FreqCenter += CurrentFreqChangeStep;
	}
	else
	{
		FreqCenter -= CurrentFreqChangeStep;
	}
	SetFreqMinMax();
	DrawLabels();
}

void ChangeHoldFreq(uint8_t Up)
{
	if (Up)
	{
		CurrentFreqIndex = (CurrentFreqIndex + 1) % CurrentStepCount;
	}
	else
	{
		CurrentFreqIndex = (CurrentFreqIndex + CurrentStepCount - 1) % CurrentStepCount;
	}
	CurrentFreq = FreqMin + (CurrentFreqIndex * CurrentFreqStep);
}

////////////////////////////////////////////////////////////////

void ChangeSquelchLevel(uint8_t Up)
{
	if (Up)
	{
		SquelchLevel += 2;
	}
	else
	{
		SquelchLevel -= 2;
	}
}

////////////////////////////////////////////////////////////////

void ToggleFilter(void)
{
	bFilterEnabled ^= 1;
	BK4819_EnableFilter(bFilterEnabled);
	bResetSquelch = TRUE;
	bRestartScan = TRUE;
	DrawLabels();
}

////////////////////////////////////////////////////////////////

void ToggleNarrowWide(void)
{
	bNarrow ^= 1;
	BK4819_WriteRegister(0x43, (bNarrow) ? 0x4048 : 0x3028);
	DrawLabels();
}

////////////////////////////////////////////////////////////////

void IncrementModulation(void)
{
	CurrentModulation = (CurrentModulation + 1) % 3;
	DrawCurrentFreq((bRXMode) ? COLOR_GREEN : COLOR_BLUE);
}

////////////////////////////////////////////////////////////////

void JumpToVFO(void)
{
	uint32_t Divider = 10000000U;
	uint8_t i;

	Int2Ascii(CurrentFreq, 8);

	for (i = 0; i < 6; i++)
	{
		gInputBox[i] = (CurrentFreq / Divider) % 10U;
		Divider /= 10;
	}

	gSettings.WorkMode = FALSE;

#ifdef UART_DEBUG
	Int2Ascii(gSettings.WorkMode, 1);
	UART_printf("gSettings.WorkMode: ");
	UART_printf(gShortString);
	UART_printf("     -----     ");
#endif

	SETTINGS_SaveGlobals();
	gVfoState[gSettings.CurrentVfo].bIsNarrow = bNarrow;
	CHANNELS_UpdateVFO();

	bExit = TRUE;
}

////////////////////////////////////////////////////////////////

void StopSpectrum(void)
{

	ST7735S_Init();
	ST7735S_normalMode();
	SCREEN_TurnOn();

	if (gSettings.WorkMode)
	{
		CHANNELS_LoadChannel(gSettings.VfoChNo[!gSettings.CurrentVfo], !gSettings.CurrentVfo);
		CHANNELS_LoadChannel(gSettings.VfoChNo[gSettings.CurrentVfo], gSettings.CurrentVfo);
	}
	else
	{
		CHANNELS_LoadChannel(gSettings.CurrentVfo ? 999 : 1000, !gSettings.CurrentVfo);
		CHANNELS_LoadChannel(gSettings.CurrentVfo ? 1000 : 999, gSettings.CurrentVfo);
	}

	RADIO_Tune(gSettings.CurrentVfo);
	UI_DrawMain(false);
}

////////////////////////////////////////////////////////////////

void CheckKeys(void)
{
	Key = KEY_GetButton();
	if (Key == LastKey && Key != KEY_NONE)
	{
		if (bRXMode)
		{
			KeyHoldTimer += 10;
		}
		else
		{
			KeyHoldTimer++;
		}
	}
	if (Key != LastKey || KeyHoldTimer >= 50)
	{
		KeyHoldTimer = 0;
		switch (Key)
		{
		case KEY_NONE:
			break;
		case KEY_EXIT:
			bExit = TRUE;
			return;
			break;
		case KEY_MENU:
			JumpToVFO();
			break;
		case KEY_UP:
			if (!bHold)
			{
				ChangeCenterFreq(TRUE);
			}
			else
			{
				ChangeHoldFreq(TRUE);
			}
			break;
		case KEY_DOWN:
			if (!bHold)
			{
				ChangeCenterFreq(FALSE);
			}
			else
			{
				ChangeHoldFreq(FALSE);
			};
			break;
		case KEY_1:
			IncrementStepIndex();
			break;
		case KEY_2:
			offset++;
			offset %= 32;
			DrawLabels();
			break;
		case KEY_3:
			IncrementModulation();
			break;
		case KEY_4:
			IncrementFreqStepIndex();
			break;
		case KEY_5:
			break;
		case KEY_6:
			ChangeSquelchLevel(TRUE);
			break;
		case KEY_7:
			bHold ^= 1;
			DrawLabels();
			break;
		case KEY_8:
			RADIO_EndAudio();
			bRXMode = FALSE;
			break;
		case KEY_9:
			ChangeSquelchLevel(FALSE);
			break;
		case KEY_0:
			ToggleFilter();
			break;
		case KEY_HASH:
			ToggleNarrowWide();
			break;
		case KEY_STAR:
			IncrementScanDelay();
			break;
		default:
			break;
		}
		LastKey = Key;
	}
}

void Spectrum_StartAudio(void)
{
	gReceivingAudio = true;

	gpio_bits_set(GPIOA, BOARD_GPIOA_LED_GREEN);
	gRadioMode = RADIO_MODE_RX;
	OpenAudio(bNarrow, CurrentModulation);
	if (CurrentModulation == 0)
	{
		BK4819_WriteRegister(0x4D, 0xA080);
		BK4819_WriteRegister(0x4E, 0x6F7C);
	}

	if (CurrentModulation > 0)
	{
		// AM, SSB
		BK4819_EnableScramble(false);
		BK4819_EnableCompander(false);
		// Set bit 4 of register 73 (Auto Frequency Control Disable)
		uint16_t reg_73 = BK4819_ReadRegister(0x73);
		BK4819_WriteRegister(0x73, reg_73 | 0x10U);
		if (CurrentModulation > 1)
		{													// if SSB
			BK4819_WriteRegister(0x43, 0b0010000001011000); // Filter 6.25KHz
			BK4819_WriteRegister(0x37, 0b0001011000001111);
			BK4819_WriteRegister(0x3D, 0b0010101101000101);
			BK4819_WriteRegister(0x48, 0b0000001110101000);
		}
	}
	else
	{
		// FM
		BK4819_EnableScramble(false);
		BK4819_EnableCompander(true);
		uint16_t reg_73 = BK4819_ReadRegister(0x73);
		BK4819_WriteRegister(0x73, reg_73 & ~0x10U);
		BK4819_SetAFResponseCoefficients(false, true, gCalibration.RX_3000Hz_Coefficient);
	}
	SPEAKER_TurnOn(SPEAKER_OWNER_RX);
}

void RunRX(void)
{
	bRXMode = TRUE;
	Spectrum_StartAudio();

	while (RssiValue_v[CurrentFreqIndex] > SquelchLevel)
	{
		RssiValue_v[CurrentFreqIndex] = BK4819_GetRSSI();
		CheckKeys();
		if (bExit)
		{
			RADIO_EndAudio();
			return;
		}

		// show_horizontal_waterfall();
		DELAY_WaitMS(5);
	}

	RADIO_EndAudio();
	bRXMode = FALSE;
}
////////////////////////////////////////////////////////////////

uint16_t map(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void show_spectrum()
{
#define SPECTRUM_RIGHT_MARGIN 2
#define SPECTRUM_LEFT_MARGIN 2

	uint32_t FreqToCheck;

	uint16_t y_old, y_new, y1_new, y1_old;
	uint16_t y1_old_minus = 0;
	uint16_t y1_new_minus = 0;

	uint8_t spectrum_x = 0;		  // x offset
	uint8_t spectrum_y = 10;	  // y offset
	uint8_t spectrum_height = 40; // spectrum max. height

	uint8_t i;

	FreqToCheck = FreqMin;

	for (i = SPECTRUM_LEFT_MARGIN; i < SPECTRUM_WIDTH - SPECTRUM_RIGHT_MARGIN; i++)
	{

		FreqToCheck += CurrentFreqStep;

		BK4819_set_rf_frequency(FreqToCheck, true); // set the VCO/PLL

		// moving window - weighted average of 5 points of the spectrum to smooth spectrum in the frequency domain
		// weights:  x: 50% , x-1/x+1: 36%, x+2/x-2: 14%

		y_new = pixelnew[i] * 0.5 + pixelnew[i - 1] * 0.18 + pixelnew[i + 1] * 0.18 + pixelnew[i - 2] * 0.07 + pixelnew[i + 2] * 0.07;
		y_old = pixelold[i] * 0.5 + pixelold[i - 1] * 0.18 + pixelold[i + 1] * 0.18 + pixelold[i - 2] * 0.07 + pixelold[i + 2] * 0.07;

		if (y_old > (spectrum_height - 1))
		{
			y_old = (spectrum_height - 1);
		}

		if (y_new > (spectrum_height - 1))
		{
			y_new = (spectrum_height - 1);
		}

		if (y_old < 0)
			y_old = 0;
		if (y_new < 0)
			y_new = 0;

		y1_old = y_old + spectrum_y;
		y1_new = y_new + spectrum_y;

		if (i == SPECTRUM_LEFT_MARGIN)
		{
			y1_old_minus = y1_old;
			y1_new_minus = y1_new;
		}
		if (i == SPECTRUM_WIDTH - SPECTRUM_RIGHT_MARGIN)
		{
			y1_old_minus = y1_old;
			y1_new_minus = y1_new;
		}

		// DELETE OLD LINE/POINT
		if (y1_old - y1_old_minus > 1)
		{ // plot line upwards
			ST7735S_DrawFastLine(i + spectrum_x, y1_old_minus + 1, y1_old - y1_old_minus, COLOR_BACKGROUND, 1);
		}
		else if (y1_old - y1_old_minus < -1)
		{ // plot line downwards
			ST7735S_DrawFastLine(i + spectrum_x, y1_old, y1_old_minus - y1_old, COLOR_BACKGROUND, 1);
		}
		else
		{
			ST7735S_SetPixel(i + spectrum_x, y1_old, COLOR_BACKGROUND); // delete old pixel
		}

		// DRAW NEW LINE/POINT
		if (y1_new - y1_new_minus > 1)
		{ // plot line upwards
			ST7735S_DrawFastLine(i + spectrum_x, y1_new_minus + 1, y1_new - y1_new_minus, COLOR_GREEN, 1);
		}
		else if (y1_new - y1_new_minus < -1)
		{ // plot line downwards
			ST7735S_DrawFastLine(i + spectrum_x, y1_new, y1_new_minus - y1_new, COLOR_GREEN, 1);
		}
		else
		{
			ST7735S_SetPixel(i + spectrum_x, y1_new, COLOR_GREEN); // write new pixel
		}

		y1_new_minus = y1_new;
		y1_old_minus = y1_old;

		RssiValue = 0.6 * BK4819_GetRSSI() + 0.4 * RssiValue_old;
		RssiValue_old = RssiValue;

		pixelold[i] = pixelnew[i];
		pixelnew[i] = (((RssiValue - 72) * 100) / 258) * 0.8 - offset;
	}
}

void show_waterfall()
{
	uint8_t lptr = WATERFALL_HEIGHT - waterfall_line; // get current line of "bottom" of waterfall in circular buffer

	lptr %= WATERFALL_HEIGHT; // do modulus limit of spectrum high

	uint8_t lcnt = 0; // initialize count of number of lines of display

	// Waterfall software scrolling (st7735 does not support horizontal scrolling)
	for (lcnt = 0; lcnt < WATERFALL_HEIGHT; lcnt++)
	{
		ST7735S_SetAddrWindow(0, WATERFALL_HEIGHT - lcnt, SPECTRUM_WIDTH, WATERFALL_HEIGHT - lcnt);

		for (uint8_t i = 0; i < (SPECTRUM_WIDTH); i++)
		{
			uint16_t wf = (waterfall_rainbow[63 - waterfall[lptr][i]]);
			ST7735S_SendU16(wf); // write to memory using waterfall color from palette
		}

		lptr++;					  // point to next line in circular display buffer
		lptr %= WATERFALL_HEIGHT; // clip to display height
	}

	waterfall_line++;
	waterfall_line %= WATERFALL_HEIGHT;
}

void Spectrum_Loop_s()
{

	// uint32_t FreqToCheck;

	// while (1)
	// {

	// 	FreqToCheck = FreqMin;
	// 	CurrentFreqIndex = 0;
	// 	CurrentFreq = FreqMin;

	// 	show_spectrum();

	// 	CheckKeys();
	// 	if (bExit)
	// 	{
	// 		return;
	// 	}

	// 	DrawCurrentFreq(COLOR_BLUE);
	//}
}

void show_horizontal_waterfall()
{

#define WATERFALL_RIGHT_MARGIN 0
#define WATERFALL_LEFT_MARGIN 0
#define H_WATERFALL_WIDTH 127

#define SCROLL_LEFT_MARGIN 55
#define SCROLL_RIGHT_MARGIN 160

	scroll++;
	scroll %= (SCROLL_RIGHT_MARGIN - SCROLL_LEFT_MARGIN);

	ST7735S_scroll(scroll);

	ST7735S_SetAddrWindow((SCROLL_RIGHT_MARGIN)-scroll, 0, (SCROLL_RIGHT_MARGIN)-scroll, 127);

	for (uint8_t i = 0; i < 127; i++)
	{
		uint16_t wf = (waterfall_rainbow[RssiValue_v[i] - RssiLow - offset]);
		ST7735S_SendU16(wf); // write to screen using waterfall color from palette
	}

	ST7735S_SetPixel(54, CurrentFreqIndex_old, COLOR_BACKGROUND);
	ST7735S_SetPixel(53, CurrentFreqIndex_old, COLOR_BACKGROUND);
	ST7735S_SetPixel(52, CurrentFreqIndex_old, COLOR_BACKGROUND);

	CurrentFreqIndex_old = CurrentFreqIndex;

	ST7735S_SetPixel(54, CurrentFreqIndex, COLOR_GREY);
	ST7735S_SetPixel(53, CurrentFreqIndex, COLOR_GREY);
	ST7735S_SetPixel(52, CurrentFreqIndex, COLOR_GREY);
}

void Spectrum_Loop(void)
{

	uint32_t FreqToCheck;
	CurrentFreqIndex = 0;
	CurrentFreqIndex_old = 0;
	CurrentFreq = FreqMin;
	bResetSquelch = TRUE;
	bRestartScan = FALSE;

	scroll = 0;

	DrawLabels();

	ST7735S_defineScrollArea(SCROLL_LEFT_MARGIN, SCROLL_RIGHT_MARGIN);

	while (1)
	{
		FreqToCheck = FreqMin;
		bRestartScan = TRUE;

		for (uint8_t i = WATERFALL_LEFT_MARGIN; i < H_WATERFALL_WIDTH - WATERFALL_RIGHT_MARGIN; i++)
		{
			if (bRestartScan)
			{
				bRestartScan = FALSE;
				RssiLow = 330;
				RssiHigh = 72;
				i = 0;
			}

			BK4819_set_rf_frequency(FreqToCheck, true); // set the VCO/PLL

			DELAY_WaitUS(500);

			RssiValue_v[i] = BK4819_GetRSSI();

			if (RssiValue_v[i] < RssiLow)
			{
				RssiLow = RssiValue_v[i];
			}
			else if (RssiValue_v[i] > RssiHigh)
			{
				RssiHigh = RssiValue_v[i];
			}

			if (RssiValue_v[i] > RssiValue_v[CurrentFreqIndex] && !bHold)
			{
				CurrentFreqIndex = i;
				CurrentFreq = FreqToCheck;
			}

			FreqToCheck += CurrentFreqStep;
		}

		if (bResetSquelch)
		{
			bResetSquelch = FALSE;
			SquelchLevel = RssiHigh + 5;
		}

		if (RssiValue_v[CurrentFreqIndex] > SquelchLevel)
		{
			BK4819_set_rf_frequency(CurrentFreq, TRUE);
			DELAY_WaitMS(CurrentScanDelay);
			RunRX();
		}

		DrawCurrentFreq(COLOR_BLUE);
		show_horizontal_waterfall();

		CheckKeys();
		if (bExit)
		{
			return;
		}
	}
}

void APP_Spectrum(void)
{

	bExit = FALSE;
	bRXMode = FALSE;

	FreqCenter = gVfoState[gSettings.CurrentVfo].RX.Frequency;
	bNarrow = gVfoState[gSettings.CurrentVfo].bIsNarrow;
	CurrentModulation = gVfoState[gSettings.CurrentVfo].gModulationType;
	CurrentFreqStepIndex = gSettings.FrequencyStep;
	CurrentFreqStep = FREQUENCY_GetStep(CurrentFreqStepIndex);
	CurrentStepCountIndex = STEPS_128;
	bHold = 0;
	bFilterEnabled = TRUE;
	SquelchLevel = 0;
	CurrentScanDelay = 5;

	SetStepCount();
	SetFreqMinMax();

	for (int i = 0; i < 8; i++)
	{
		gShortString[i] = ' ';
	}

	DISPLAY_Fill(0, 159, 1, 96, COLOR_BACKGROUND);

	DrawLabels();

	Spectrum_Loop();

	StopSpectrum();
}

//---------------------------------------------------------------------------------------------
// From fagci for reference - remove later
// bool IsCenterMode() { return settings.scanStepIndex < STEP_1_0kHz; }

// uint8_t GetStepsCount() { return 128 >> settings.stepsCount; }

// uint16_t GetScanStep() { return StepFrequencyTable[settings.scanStepIndex]; }

// uint32_t GetFreqStart() { return IsCenterMode() ? currentFreq - (GetBW() >> 1) : currentFreq; }

// uint32_t GetFreqEnd() { return currentFreq + GetBW(); }
//---------------------------------------------------------------------------------------------

// *********************************************************************************************************
