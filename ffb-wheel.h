/*
	Copyright 2013  Saku Kekkonen

	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files (the
	"Software"), to deal in the Software without restriction, including
	without limitation the rights to use, copy, modify, merge, publish,
	distribute, sublicense, and/or sell copies of the Software, and to
	permit persons to whom the Software is furnished to do so, subject to
	the following conditions:

	The above copyright notice and this permission notice shall be included
	in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
	CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
	TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _FFB_WHEEL_
#define _FFB_WHEEL_

#define CMD_PLAY		0x02
#define CMD_STOP		0x03
#define CMD_DELETE		0x01

#define EFFECT_SINE		0x02
#define EFFECT_SQUARE		0x03
#define EFFECT_TRIANGLE	0x04
#define EFFECT_SAWTOOTH	0x05

#include <stdint.h>
#include "ffb.h"

typedef struct
{
	uint8_t		cmd;		// f2
	uint8_t		operation_and_checksum;
	uint8_t		effect_id;
} cmd_f2_t;

typedef struct
{
	uint8_t		cmd;		// f1
	uint8_t		checksum;
	uint8_t		def_and_address;
	uint8_t		effect_id;
	uint16_t	value;
} cmd_f1_t;

typedef struct
{
	uint8_t		command;		// always 0x20
	uint8_t		waveForm;
	uint8_t		unknown1;		// always 0x7f
	uint16_t	duration;
	uint8_t		direction;
} FFW_MIDI_Effect_Common_t;

/* Sine, square, triangle, sawtooth and ramp effects.  The Sidewinder wheel
 * exposes a single ramp waveform, so sawtooth effects are approximated with
 * that waveform. */
typedef struct
{
	FFW_MIDI_Effect_Common_t	common;
	uint8_t		preciseDirection;
	uint16_t	phase;
	uint8_t		attackLevel;
	uint16_t	attackTime;
	uint8_t		magnitude;
	uint16_t	fadeTime;
	uint8_t		fadeLevel;
	uint16_t	frequency;
	uint8_t		offset;
} FFW_MIDI_Effect_Periodic_Ramp_t;

typedef struct
{
	FFW_MIDI_Effect_Common_t	common;
	uint8_t		unknown2;		// always 0x7f
	uint8_t		attackLevel;
	uint16_t	attackTime;
	uint8_t		magnitude;
	uint16_t	fadeTime;
	uint8_t		fadeLevel;
	uint8_t		forceDirection;
} FFW_MIDI_Effect_ConstantForce_t;

typedef struct
{
	FFW_MIDI_Effect_Common_t	common;
	uint16_t	negativeCoefficient;
	uint8_t		conditionParameters[4];
	uint16_t	positiveCoefficient;
} FFW_MIDI_Effect_Spring_Inertia_Damper_t;

typedef struct
{
	FFW_MIDI_Effect_Common_t	common;
	uint8_t		coefficient;
} FFW_MIDI_Effect_Friction_t;

void FfbwheelEnableInterrupts(void);
uint8_t FfbwheelDeviceControl(uint8_t usb_control);
const uint8_t* FfbwheelGetSysExHeader(uint8_t* hdr_len);
void FfbwheelSetAutoCenter(uint8_t enable);

void FfbwheelStartEffect(uint8_t effectId);
void FfbwheelStopEffect(uint8_t effectId);
void FfbwheelFreeEffect(uint8_t effectId);

void FfbwheelModifyDuration(uint8_t effectId, uint16_t duration);
void FfbwheelModifyDeviceGain(uint8_t gain);
void FfbwheelSetCustomSample(uint8_t effectId, volatile TEffectState* effect, int8_t sample);

void FfbwheelSetEnvelope(USB_FFBReport_SetEnvelope_Output_Data_t* data, volatile TEffectState* e);
void FfbwheelSetCondition(USB_FFBReport_SetCondition_Output_Data_t* data, volatile TEffectState* e);
void FfbwheelSetPeriodic(USB_FFBReport_SetPeriodic_Output_Data_t* data, volatile TEffectState* e);
void FfbwheelSetConstantForce(USB_FFBReport_SetConstantForce_Output_Data_t* data, volatile TEffectState* e);
void FfbwheelSetRampForce(USB_FFBReport_SetRampForce_Output_Data_t* data, volatile TEffectState* e);
int  FfbwheelSetEffect(USB_FFBReport_SetEffect_Output_Data_t *data, volatile TEffectState* effect);
void FfbwheelCreateNewEffect(USB_FFBReport_CreateNewEffect_Feature_Data_t* inData, volatile TEffectState* effect);

uint8_t FfbwheelUsbToMidiEffectType(uint8_t usb_effect_type);

/* Sidewinder wheel F1 parameter addresses. */
#define FFW_MIDI_MODIFY_DURATION			0x00
#define FFW_MIDI_MODIFY_ATTACK_LEVEL		0x03
#define FFW_MIDI_MODIFY_ATTACK_TIME		0x04
#define FFW_MIDI_MODIFY_MAGNITUDE			0x06
#define FFW_MIDI_MODIFY_FADE_TIME			0x07
#define FFW_MIDI_MODIFY_FADE_LEVEL			0x09
#define FFW_MIDI_MODIFY_FORCE_DIRECTION	0x09
#define FFW_MIDI_MODIFY_FREQUENCY			0x0a
#define FFW_MIDI_MODIFY_OFFSET			0x0c
#define FFW_MIDI_MODIFY_POSITIVE_COEFF		0x06
#define FFW_MIDI_MODIFY_NEGATIVE_COEFF		0x03
#define FFW_MIDI_MODIFY_DEVICE_GAIN		0x00

/* 0x7d is the factory maximum and 0x3e/0x3f is neutral. */
#define FFW_AUTOCENTER_STRENGTH			32

#endif // _FFB_WHEEL_
