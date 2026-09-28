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

/*
 * Sidewinder wheel F1 parameter addresses.  An address is the position of the
 * parameter in the effect's SysEx layout (the FFW_MIDI_Effect_* structs):
 * the duration is 0, the direction 2, and each following field takes one
 * address whether it is 7 or 14 bits wide.  The addresses therefore differ
 * between effect types.
 */
#define FFW_MIDI_MODIFY_DURATION			0x00
#define FFW_MIDI_MODIFY_DEVICE_GAIN		0x00	// effect 0

/* Constant force */
#define FFW_MIDI_CONSTANT_ATTACK_LEVEL		0x04
#define FFW_MIDI_CONSTANT_ATTACK_TIME		0x05
#define FFW_MIDI_CONSTANT_MAGNITUDE		0x06
#define FFW_MIDI_CONSTANT_FADE_TIME		0x07
#define FFW_MIDI_CONSTANT_FADE_LEVEL		0x08
#define FFW_MIDI_CONSTANT_FORCE_DIRECTION	0x09

/* Periodic and ramp */
#define FFW_MIDI_PERIODIC_PHASE			0x04
#define FFW_MIDI_PERIODIC_ATTACK_LEVEL		0x05
#define FFW_MIDI_PERIODIC_ATTACK_TIME		0x06
#define FFW_MIDI_PERIODIC_MAGNITUDE		0x07
#define FFW_MIDI_PERIODIC_FADE_TIME		0x08
#define FFW_MIDI_PERIODIC_FADE_LEVEL		0x09
#define FFW_MIDI_PERIODIC_FREQUENCY		0x0a
#define FFW_MIDI_PERIODIC_OFFSET			0x0b

/* Spring, damper and inertia */
#define FFW_MIDI_CONDITION_NEGATIVE_COEFF	0x03
#define FFW_MIDI_CONDITION_POSITIVE_COEFF	0x06

/* Friction */
#define FFW_MIDI_FRICTION_COEFF			0x03

/* ---- Tuning ----------------------------------------------------------------
 *
 * Forces are on a 0..127 scale, where 127 is the wheel's full force.
 *
 * *_STRENGTH_PERCENT scales every force of that effect type (after the
 * effect's own gain).  Values above 100 boost weak effects, clipping at full
 * force.
 *
 * *_MIN_FORCE lifts weak forces above the wheel's friction: any non-zero
 * force is rescaled from 1..127 into MIN_FORCE..127, so full force stays the
 * same and zero stays zero.  For springs, dampers, inertia and friction it
 * applies to the coefficient instead (the force the effect would reach at
 * full deflection or speed).  The device gain set by the game is applied by
 * the wheel afterwards and also scales the minimum.
 */
#define FFW_CONSTANT_STRENGTH_PERCENT		100
#define FFW_CONSTANT_MIN_FORCE			0

/* Sine, square, triangle, sawtooth, ramp and (on Linux) rumble */
#define FFW_PERIODIC_STRENGTH_PERCENT		100
#define FFW_PERIODIC_MIN_FORCE			0

/* Spring, damper, inertia and friction */
#define FFW_CONDITION_STRENGTH_PERCENT		100
#define FFW_CONDITION_MIN_FORCE		0

/* Shortest periodic effect period in ms; shorter ones are slowed down to it.
 * The motor cannot follow very fast waves and just makes noise (fftest's sine
 * asks for 10 ms, i.e. 100 Hz).  0 leaves every period unchanged.  Linux
 * rumble uses 50 ms, so values up to 50 leave rumble alone. */
#define FFW_PERIODIC_MIN_PERIOD_MS		50

/* Set to 1 to swap left and right for constant forces.  Linux defines
 * direction 0x4000 as left, which turns the wheel left with the default 0. */
#define FFW_CONSTANT_INVERT			0

/* Periodic offset byte that gives no push.  The true neutral lies between
 * 0x3e and 0x3f: very fast full-strength waves (e.g. fftest's 100 Hz sine)
 * drift slowly left with 0x3e and right with 0x3f. */
#define FFW_PERIODIC_OFFSET_ZERO		0x3e

/* Self-centering strength when enabled by the Force button, as a percentage
 * (0..100) of the wheel's factory centering spring. */
#define FFW_AUTOCENTER_STRENGTH_PERCENT	75

#endif // _FFB_WHEEL_
