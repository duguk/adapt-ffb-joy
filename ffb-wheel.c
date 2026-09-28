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

#include "ffb-wheel.h"

#include <LUFA/Drivers/Board/LEDs.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

uint8_t FfbwheelUsbToMidiEffectType(uint8_t usb_effect_type)
{
	const uint8_t usbToMidiEffectType[] = {
		0x06,	// Constant,
		0x05, 	// Ramp
		0x03, 	// Square
		0x02, 	// Sine
		0x04,	// Triangle
		0x05,	// SawtoothDown
		0x05,	// SawtoothUp
		0x08,	// Spring
		0x09,	// Damper
		0x0a,	// Inertia
		0x0b,	// Friction
		0x06 	// Custom: live constant-force fallback
	};

	if (usb_effect_type >= sizeof(usbToMidiEffectType))
		return 0;

	return usbToMidiEffectType[usb_effect_type];
}

static void FfbwheelSendModify(uint8_t effectId, uint8_t address,
	uint16_t value);
static uint16_t FfbwheelEncode14(uint16_t value);

/*
 * Condition coefficients are 14-bit values centred on 0x2000.  The factory
 * centering spring uses 0x3e80 for the negative side and 0x007d for the
 * positive side, so a restoring force raises the negative value and lowers
 * the positive one.
 */
#define FFW_COEFF_CENTRE		0x2000
#define FFW_COEFF_NEGATIVE_MAX	0x3e80
#define FFW_COEFF_POSITIVE_MAX	0x007d

/*
 * For effect parameters, only the high byte of each coefficient field is the
 * coefficient (0x40 is no force, 0x7d/0x00 the factory strength).  The low
 * byte behaves like a force limit: anything but the factory 0x00 (negative
 * side) or 0x7d (positive side) adds a constant push, so that a damper drives
 * a stationary wheel towards one end.
 */
#define FFW_COND_COEFF_ZERO		0x40
#define FFW_COND_NEGATIVE_MAX		0x7d
#define FFW_COND_POSITIVE_MAX		0x00
#define FFW_COND_NEGATIVE_LIMIT	0x00
#define FFW_COND_POSITIVE_LIMIT	0x7d

/* sin() over one quadrant of the wheel's 128-step direction, scaled to 127. */
static const uint8_t PROGMEM ffwQuarterSine[33] = {
	0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85,
	90, 94, 98, 102, 106, 109, 112, 115, 117, 120, 122, 123, 125, 126, 126, 127,
	127
};

/*
 * The wheel only has the X axis.  Project a force along the given direction
 * onto it, as Linux and DirectInput do: 0x20 (90 degrees) is the full force
 * with a positive sign, 0x60 (270 degrees) the full force with a negative
 * sign, and 0x00/0x40 have no X component.
 */
static int16_t FfbwheelProjectOnAxis(int16_t value, uint8_t direction)
{
	uint8_t d = direction & 0x7f;
	uint8_t i = d & 0x3f;
	if (i > 32)
		i = 64 - i;

	int16_t projected = ((int32_t)value * pgm_read_byte(&ffwQuarterSine[i])) / 127;
	return (d < 0x40) ? projected : -projected;
}

/*
 * The wheel's fade time is when the fade starts, measured from the start of
 * the effect (the factory ramp uses the duration itself for "no fade"),
 * whereas USB gives the length of the fade at the end of the effect.
 */
static uint16_t FfbwheelFadeStart(uint16_t duration, uint16_t fadeLength)
{
	if (duration == USB_DURATION_INFINITE)
		return MIDI_DURATION_INFINITE;
	if (fadeLength >= duration)
		return 1;

	uint16_t start = UsbUint16ToMidiUint14_Time(duration - fadeLength);
	return start ? start : 1;
}

/*
 * The wheel's built-in centering spring uses a slightly different checksum
 * convention from normal effect parameter writes.  In particular, bit 6 of
 * the address is a protocol flag and is excluded from the checksum.  These
 * are the packets used by the original wheel startup sequence, so keep this
 * helper separate from the general effect writer below.
 */
static void FfbwheelSendAutoCenterModify(uint8_t address, uint16_t value)
	{
	cmd_f1_t op;

	op.cmd = 0xf1;
	op.def_and_address = address;
	op.effect_id = 1;
	op.value = value;

	uint8_t* d = (uint8_t*)&op;
	uint8_t sum = d[0] + (d[2] & ~0x40) + d[3] + d[4] + d[5];
	op.checksum = (0x80 - sum) & 0x7f;

	FfbSendData(d, sizeof(op));
	}

/**
 * Initialize wheel for FF. Releases spring effect.
 *
 * Force Editor with Windows XP and gameport sends
 * X1 pulse groups during initialization, but
 * those are not needed for enabling FF.
 */
void FfbwheelEnableInterrupts(void)
	{

	const uint8_t startupFfbWheelData_0[] = {
		0xf3, 0x1d
	};

	const uint8_t startupFfbWheelData_1[] = {
		0xf1 ,0x0e ,0x43 ,0x01 ,0x00 ,0x7d,
		0xf1 ,0x7e ,0x04 ,0x01 ,0x3e ,0x4e,
		0xf1 ,0x1c ,0x45 ,0x01 ,0x3e ,0x2f,
		0xf1 ,0x0b ,0x46 ,0x01 ,0x7d ,0x00,
	};

	const uint8_t startupFfbWheelData_2[] = {
		0xf1, 0x10, 0x40, 0x00, 0x7f, 0x00,	/* full device gain */
		0xf3, 0x6a	/* stop all */
	};

    WaitMs(100);

	FfbSendData(startupFfbWheelData_0, sizeof(startupFfbWheelData_0));
	FfbSendData(startupFfbWheelData_1, sizeof(startupFfbWheelData_1));
	FfbSendData(startupFfbWheelData_0, sizeof(startupFfbWheelData_0));
	FfbSendData(startupFfbWheelData_2, sizeof(startupFfbWheelData_2));

	WaitMs(100);
	}

uint8_t FfbwheelDeviceControl(uint8_t usb_control)
{
	static const uint8_t usbToMidiControl[] = {
		0x2e, /* enable actuators */
		0x3f, /* disable actuators */
		0x6a, /* stop all */
		0x1d, /* reset */
		0x48, /* pause */
		0x59  /* continue */
	};
	uint8_t command[2] = {0xf3, 0};

	if (usb_control < USB_DCTRL_ACTUATORS_ENABLE || usb_control > USB_DCTRL_CONTINUE)
		return 0;

	command[1] = usbToMidiControl[usb_control - 1];
	FfbSendData(command, sizeof(command));
	return 1;
}

/*
 * The centering spring is the wheel's built-in effect 1, which can be started
 * and stopped like any other effect.  Never reset the wheel or stop all
 * effects here: that would delete or stop the host's effects too, e.g. when
 * the Force switch is turned off and on during a game.
 */
void FfbwheelSetAutoCenter(uint8_t enable)
{
	if (!enable)
		{
		FfbwheelStopEffect(1);
		return;
		}

	/*
	 * Rewrite the wheel's built-in spring setup, with a reduced coefficient
	 * in both directions.  A reset (by the host or at startup) restores the
	 * factory values.  The two middle writes are the original saturation and
	 * dead-band values.
	 *
	 * Scale each factory coefficient's distance from the centre by the same
	 * amount to keep centering equally strong both ways.
	 */
	uint16_t coefficient1 = FFW_COEFF_CENTRE +
		((uint32_t)(FFW_COEFF_NEGATIVE_MAX - FFW_COEFF_CENTRE) *
		FFW_AUTOCENTER_STRENGTH_PERCENT) / 100;
	uint16_t coefficient2 = FFW_COEFF_CENTRE -
		((uint32_t)(FFW_COEFF_CENTRE - FFW_COEFF_POSITIVE_MAX) *
		FFW_AUTOCENTER_STRENGTH_PERCENT) / 100;
	FfbwheelSendAutoCenterModify(0x43, FfbwheelEncode14(coefficient1));
	FfbwheelSendAutoCenterModify(0x04, 0x4e3e);
	FfbwheelSendAutoCenterModify(0x45, 0x2f3e);
	FfbwheelSendAutoCenterModify(0x46, FfbwheelEncode14(coefficient2));
	FfbwheelStartEffect(1);
}

const uint8_t* FfbwheelGetSysExHeader(uint8_t* hdr_len)
{
	static const uint8_t header[] = {0xf0, 0x00, 0x01, 0x0a, 0x15};
	*hdr_len = sizeof(header);
	return header;
}

// effect operations ---------------------------------------------------------

static void FfbwheelSendEffectOper(uint8_t effectId, uint8_t operation)
{
	cmd_f2_t op;

	op.cmd = 0xf2;
	op.effect_id = effectId;
	op.operation_and_checksum = operation << 4;

	uint8_t sum = 0xf ^ 0x2 ^ (op.operation_and_checksum >> 4)
			^ (op.effect_id >> 4) ^ (op.effect_id & 0x0f);

	op.operation_and_checksum &= 0xf0;
	op.operation_and_checksum |= sum;

	FfbSendData((const uint8_t*)&op, sizeof(op));
}

void FfbwheelStartEffect(uint8_t effectId)
{
	FfbwheelSendEffectOper(effectId, 2);
}

void FfbwheelStopEffect(uint8_t effectId)
{
	FfbwheelSendEffectOper(effectId, 3);
}

void FfbwheelFreeEffect(uint8_t effectId)
{
	FfbwheelSendEffectOper(effectId, 1);
}

// modify operations ---------------------------------------------------------

static void FfbwheelSendModify(uint8_t effectId, uint8_t address, uint16_t value)
{
	cmd_f1_t op;

	op.cmd = 0xf1;
	op.def_and_address = address;
	op.effect_id = effectId;
	op.value = value;

	uint8_t* d = (uint8_t*)&op;
	uint8_t sum = d[2] + d[3] + d[4] + d[5];
	if (sum > 0x0f && sum <= 0x8f)
		op.def_and_address |= 0x40;

	sum += d[0];
	op.checksum = (0x80 - sum) & 0x7f;

	FfbSendData(d, sizeof(op));
}

void FfbwheelModifyDuration(uint8_t effectId, uint16_t duration)
{
	FfbwheelSendModify(effectId, FFW_MIDI_MODIFY_DURATION, duration);
}

void FfbwheelModifyDeviceGain(uint8_t gain)
{
	FfbwheelSendModify(0x00, FFW_MIDI_MODIFY_DEVICE_GAIN, (gain >> 1) & 0x7f);
}

static uint8_t FfbwheelClamp7(int16_t value)
{
	if (value < 0)
		return 0;
	if (value > 0x7f)
		return 0x7f;
	return (uint8_t)value;
}

/* Apply an effect type's strength and minimum force (see the tuning section
 * of ffb-wheel.h) to a 0..127 force. */
static uint8_t FfbwheelShapeForce(uint8_t force, uint16_t percent,
	uint8_t minimum)
{
	uint32_t scaled = ((uint32_t)force * percent) / 100;

	if (scaled == 0)
		return 0;
	if (scaled > 127)
		scaled = 127;
	return minimum + (scaled * (127 - minimum)) / 127;
}

static int16_t FfbwheelShapeSignedForce(int16_t force, uint16_t percent,
	uint8_t minimum)
{
	if (force < 0)
		return -(int16_t)FfbwheelShapeForce(FfbwheelClamp7(-force), percent,
			minimum);
	return FfbwheelShapeForce(FfbwheelClamp7(force), percent, minimum);
}

static uint16_t FfbwheelEncode14(uint16_t value)
{
	/* Keep both bytes MIDI data bytes (the wheel stores 14-bit values as
	 * two 7-bit bytes in a little-endian uint16_t). */
	return (value & 0x007f) | ((value & 0x3f80) << 1);
}

static void FfbwheelSetModify7(volatile TEffectState* effect,
	volatile uint8_t* cached, uint8_t effectId, uint8_t address, uint8_t value)
{
	if (*cached == value)
		return;

	*cached = value;
	if (effect->state & MEffectState_SentToJoystick)
		FfbwheelSendModify(effectId, address, value);
}

static void FfbwheelSetModify14(volatile TEffectState* effect,
	volatile uint16_t* cached, uint8_t effectId, uint8_t address, uint16_t value)
{
	if (*cached == value)
		return;

	*cached = value;
	if (effect->state & MEffectState_SentToJoystick)
		FfbwheelSendModify(effectId, address, value);
}

void FfbwheelSetEnvelope(
	USB_FFBReport_SetEnvelope_Output_Data_t* data,
	volatile TEffectState* effect)
{
	uint8_t eid = data->effectBlockIndex;
	uint8_t attackLevel = CalcGain(data->attackLevel, effect->usb_gain);
	uint8_t fadeLevel = CalcGain(data->fadeLevel, effect->usb_gain);
	uint16_t attackTime = UsbUint16ToMidiUint14_Time(data->attackTime);
	uint16_t fadeTime = FfbwheelFadeStart(effect->usb_duration, data->fadeTime);
	FFW_MIDI_Effect_Common_t* common = (FFW_MIDI_Effect_Common_t*)effect->data;

	effect->usb_attackLevel = data->attackLevel;
	effect->usb_fadeLevel = data->fadeLevel;
	effect->usb_fadeTime = data->fadeTime;

	if (common->waveForm == 0x06) {
		FFW_MIDI_Effect_ConstantForce_t* midi_data =
			(FFW_MIDI_Effect_ConstantForce_t*)effect->data;
		attackLevel = FfbwheelShapeForce(attackLevel,
			FFW_CONSTANT_STRENGTH_PERCENT, FFW_CONSTANT_MIN_FORCE);
		fadeLevel = FfbwheelShapeForce(fadeLevel,
			FFW_CONSTANT_STRENGTH_PERCENT, FFW_CONSTANT_MIN_FORCE);
		FfbwheelSetModify7(effect, &midi_data->attackLevel, eid,
			FFW_MIDI_CONSTANT_ATTACK_LEVEL, attackLevel);
		FfbwheelSetModify14(effect, &midi_data->attackTime, eid,
			FFW_MIDI_CONSTANT_ATTACK_TIME, attackTime);
		FfbwheelSetModify14(effect, &midi_data->fadeTime, eid,
			FFW_MIDI_CONSTANT_FADE_TIME, fadeTime);
		FfbwheelSetModify7(effect, &midi_data->fadeLevel, eid,
			FFW_MIDI_CONSTANT_FADE_LEVEL, fadeLevel);
	} else {
		FFW_MIDI_Effect_Periodic_Ramp_t* midi_data =
			(FFW_MIDI_Effect_Periodic_Ramp_t*)effect->data;
		attackLevel = FfbwheelShapeForce(attackLevel,
			FFW_PERIODIC_STRENGTH_PERCENT, FFW_PERIODIC_MIN_FORCE);
		fadeLevel = FfbwheelShapeForce(fadeLevel,
			FFW_PERIODIC_STRENGTH_PERCENT, FFW_PERIODIC_MIN_FORCE);
		FfbwheelSetModify7(effect, &midi_data->attackLevel, eid,
			FFW_MIDI_PERIODIC_ATTACK_LEVEL, attackLevel);
		FfbwheelSetModify14(effect, &midi_data->attackTime, eid,
			FFW_MIDI_PERIODIC_ATTACK_TIME, attackTime);
		FfbwheelSetModify14(effect, &midi_data->fadeTime, eid,
			FFW_MIDI_PERIODIC_FADE_TIME, fadeTime);
		FfbwheelSetModify7(effect, &midi_data->fadeLevel, eid,
			FFW_MIDI_PERIODIC_FADE_LEVEL, fadeLevel);
	}
}

void FfbwheelSetCondition(
	USB_FFBReport_SetCondition_Output_Data_t* data,
	volatile TEffectState* effect)
{
	uint8_t eid = data->effectBlockIndex;
	FFW_MIDI_Effect_Common_t* common =
		(FFW_MIDI_Effect_Common_t*)effect->data;

	/* The USB report contains the coefficient for one parameter block.  The
	 * wheel represents the two signs as separate 14-bit values. */
	/* The wheel's saturation and dead-band fields are part of its initial
	 * condition packet, but their live F1 addresses are not documented. */
	if (common->waveForm == 0x0b) {
		FFW_MIDI_Effect_Friction_t* midi_data =
			(FFW_MIDI_Effect_Friction_t*)effect->data;
		int16_t coefficient = data->positiveCoefficient;
		if (coefficient < 0)
			coefficient = -coefficient;
		coefficient = (coefficient * effect->usb_gain) / 255;
		coefficient = FfbwheelShapeSignedForce(coefficient,
			FFW_CONDITION_STRENGTH_PERCENT, FFW_CONDITION_MIN_FORCE);
		/* Like the other condition coefficients, 0x40 is no friction and
		 * lower values push the wheel along its motion instead of resisting
		 * it (the factory friction effect uses 0x7e). */
		FfbwheelSetModify7(effect, &midi_data->coefficient, eid,
			FFW_MIDI_FRICTION_COEFF, FfbwheelClamp7(FFW_COND_COEFF_ZERO +
			(coefficient * (FFW_COND_NEGATIVE_MAX - FFW_COND_COEFF_ZERO)) / 127));
	} else if (data->parameterBlockOffset == 0) {
		FFW_MIDI_Effect_Spring_Inertia_Damper_t* midi_data =
			(FFW_MIDI_Effect_Spring_Inertia_Damper_t*)effect->data;
		int16_t positiveCoefficient = FfbwheelShapeSignedForce(
			(data->positiveCoefficient * effect->usb_gain) / 255,
			FFW_CONDITION_STRENGTH_PERCENT, FFW_CONDITION_MIN_FORCE);
		int16_t negativeCoefficient = FfbwheelShapeSignedForce(
			(data->negativeCoefficient * effect->usb_gain) / 255,
			FFW_CONDITION_STRENGTH_PERCENT, FFW_CONDITION_MIN_FORCE);
		uint8_t positive = FfbwheelClamp7(FFW_COND_COEFF_ZERO -
			(positiveCoefficient *
			(FFW_COND_COEFF_ZERO - FFW_COND_POSITIVE_MAX)) / 127);
		uint8_t negative = FfbwheelClamp7(FFW_COND_COEFF_ZERO +
			(negativeCoefficient *
			(FFW_COND_NEGATIVE_MAX - FFW_COND_COEFF_ZERO)) / 127);
		uint16_t positiveValue =
			((uint16_t)positive << 8) | FFW_COND_POSITIVE_LIMIT;
		uint16_t negativeValue =
			((uint16_t)negative << 8) | FFW_COND_NEGATIVE_LIMIT;

		FfbwheelSetModify14(effect, &midi_data->positiveCoefficient, eid,
			FFW_MIDI_CONDITION_POSITIVE_COEFF, positiveValue);
		FfbwheelSetModify14(effect, &midi_data->negativeCoefficient, eid,
			FFW_MIDI_CONDITION_NEGATIVE_COEFF, negativeValue);
	}
}

void FfbwheelSetPeriodic(
	USB_FFBReport_SetPeriodic_Output_Data_t* data,
	volatile TEffectState* effect)
{
	uint8_t eid = data->effectBlockIndex;
	FFW_MIDI_Effect_Periodic_Ramp_t* midi_data =
		(FFW_MIDI_Effect_Periodic_Ramp_t*)effect->data;
	uint8_t magnitude = FfbwheelShapeForce(
		CalcGain(data->magnitude, effect->usb_gain),
		FFW_PERIODIC_STRENGTH_PERCENT, FFW_PERIODIC_MIN_FORCE);
	int16_t offset = FFW_PERIODIC_OFFSET_ZERO +
		((int32_t)(data->offset / 2) * FFW_PERIODIC_STRENGTH_PERCENT) / 100;
	uint16_t phase14 = ((uint16_t)data->phase * 0x3fff) / 0xff;
	uint16_t phase = FfbwheelEncode14(phase14);
	uint16_t period = data->period;
	if (period < FFW_PERIODIC_MIN_PERIOD_MS)
		period = FFW_PERIODIC_MIN_PERIOD_MS;
	uint16_t frequency = UsbUint16ToMidiUint14_Time(period);

	effect->usb_magnitude = data->magnitude;
	effect->usb_offset = (uint8_t)data->offset;

	FfbwheelSetModify14(effect, &midi_data->phase, eid,
		FFW_MIDI_PERIODIC_PHASE, phase);
	FfbwheelSetModify7(effect, &midi_data->magnitude, eid,
		FFW_MIDI_PERIODIC_MAGNITUDE, magnitude);
	FfbwheelSetModify14(effect, &midi_data->frequency, eid,
		FFW_MIDI_PERIODIC_FREQUENCY, frequency);
	FfbwheelSetModify7(effect, &midi_data->offset, eid,
		FFW_MIDI_PERIODIC_OFFSET, FfbwheelClamp7(offset));
}

/*
 * The wheel ignores the effect direction for constant forces.  It stores
 * magnitude as a 7-bit value with the sign in a separate byte (0x00 turns the
 * wheel left, 0x7f right), so project the level onto the wheel's axis here.
 * Linux puts the sign in the direction and always sends a positive level.
 */
static void FfbwheelUpdateConstantForce(uint8_t effectId,
	volatile TEffectState* effect)
{
	FFW_MIDI_Effect_ConstantForce_t* midi_data =
		(FFW_MIDI_Effect_ConstantForce_t*)effect->data;
	int16_t force = FfbwheelProjectOnAxis(effect->usb_constantMagnitude,
		midi_data->common.direction);
	uint8_t direction = FFW_CONSTANT_INVERT ? 0x7f : 0x00;

	if (force < 0) {
		force = -force;
		direction ^= 0x7f;
	}
	if (force > 255)
		force = 255;

	FfbwheelSetModify7(effect, &midi_data->magnitude, effectId,
		FFW_MIDI_CONSTANT_MAGNITUDE, FfbwheelShapeForce(
		CalcGain((uint8_t)force, effect->usb_gain),
		FFW_CONSTANT_STRENGTH_PERCENT, FFW_CONSTANT_MIN_FORCE));
	FfbwheelSetModify7(effect, &midi_data->forceDirection, effectId,
		FFW_MIDI_CONSTANT_FORCE_DIRECTION, direction);
}

void FfbwheelSetConstantForce(
	USB_FFBReport_SetConstantForce_Output_Data_t* data,
	volatile TEffectState* effect)
{
	effect->usb_constantMagnitude = data->magnitude;
	FfbwheelUpdateConstantForce(data->effectBlockIndex, effect);
}

void FfbwheelSetCustomSample(uint8_t effectId,
	volatile TEffectState* effect, int8_t sample)
{
	FFW_MIDI_Effect_ConstantForce_t* midi_data =
		(FFW_MIDI_Effect_ConstantForce_t*)effect->data;
	uint8_t magnitude;
	uint8_t direction;
	int16_t absoluteSample;

	if (sample < 0) {
		absoluteSample = -(int16_t)sample;
		direction = 0x7f;
	} else {
		absoluteSample = sample;
		direction = 0x00;
	}
	if (FFW_CONSTANT_INVERT)
		direction ^= 0x7f;
	if (absoluteSample > 127)
		absoluteSample = 127;
	magnitude = FfbwheelShapeForce(
		CalcGain((uint8_t)(absoluteSample * 2), effect->usb_gain),
		FFW_CONSTANT_STRENGTH_PERCENT, FFW_CONSTANT_MIN_FORCE);

	FfbwheelSetModify7(effect, &midi_data->magnitude, effectId,
		FFW_MIDI_CONSTANT_MAGNITUDE, magnitude);
	FfbwheelSetModify7(effect, &midi_data->forceDirection, effectId,
		FFW_MIDI_CONSTANT_FORCE_DIRECTION, direction);
}

void FfbwheelSetRampForce(
	USB_FFBReport_SetRampForce_Output_Data_t* data,
	volatile TEffectState* effect)
{
	uint8_t eid = data->effectBlockIndex;
	FFW_MIDI_Effect_Periodic_Ramp_t* midi_data =
		(FFW_MIDI_Effect_Periodic_Ramp_t*)effect->data;
	int16_t start = data->start;
	int16_t end = data->end;
	int16_t midpoint = (start + end) / 2;
	uint8_t magnitude = FfbwheelShapeForce(
		CalcGain((uint8_t)(end > start ? end - start : start - end),
		effect->usb_gain),
		FFW_PERIODIC_STRENGTH_PERCENT, FFW_PERIODIC_MIN_FORCE);
	uint8_t offset = FfbwheelClamp7(FFW_PERIODIC_OFFSET_ZERO +
		((int32_t)(midpoint / 2) * FFW_PERIODIC_STRENGTH_PERCENT) / 100);

	FfbwheelSetModify7(effect, &midi_data->magnitude, eid,
		FFW_MIDI_PERIODIC_MAGNITUDE, magnitude);
	FfbwheelSetModify7(effect, &midi_data->offset, eid,
		FFW_MIDI_PERIODIC_OFFSET, offset);

	/* A descending ramp is the same wheel waveform with the opposite phase. */
	FfbwheelSetModify14(effect, &midi_data->phase, eid,
		FFW_MIDI_PERIODIC_PHASE,
		(end >= start) ? 0x0000 : 0x2000);
}

int FfbwheelSetEffect(
	USB_FFBReport_SetEffect_Output_Data_t *data,
	volatile TEffectState* e)
{
	/*
	USB effect data:
		uint8_t	reportId;	// =1
		uint8_t	effectBlockIndex;	// 1..40
		uint8_t	effectType;	// 1..12 (effect usages: 26,27,30,31,32,33,34,40,41,42,43,28)
		uint16_t	duration; // 0..32767 ms
		uint16_t	triggerRepeatInterval; // 0..32767 ms
		uint16_t	samplePeriod;	// 0..32767 ms
		uint8_t	gain;	// 0..255	 (physical 0..10000)
		uint8_t	triggerButton;	// button ID (0..8)
		uint8_t	enableAxis; // bits: 0=X, 1=Y, 2=DirectionEnable
		uint8_t	directionX;	// angle (0=0 .. 180=0..360deg)
		uint8_t	directionY;	// angle (0=0 .. 180=0..360deg)
	*/

	uint8_t midi_data_len = 0;
	FFW_MIDI_Effect_Common_t* common =
		(FFW_MIDI_Effect_Common_t*)e->data;

	/* The wheel has one direction byte.  HID direction is a full circle in
	 * 180 steps, while the wheel uses 128 steps.  Without a direction, a
	 * constant force acts along the wheel's axis. */
	e->usb_gain = data->gain;
	if (data->enableAxis & 0x04)
		common->direction =
			(((uint16_t)data->directionX * 128 + 90) / 180) & 0x7f;
	else if (data->effectType == USB_EFFECT_CONSTANT)
		common->direction = 0x20;
	else
		common->direction = 0x40;

	/* The wheel's fade start depends on the duration, which may have
	 * changed.  Without an envelope, the fade starts at the end. */
	uint16_t fadeLength = (e->usb_fadeTime == USB_DURATION_INFINITE) ?
		0 : e->usb_fadeTime;
	uint16_t fadeTime = FfbwheelFadeStart(e->usb_duration, fadeLength);

	switch (data->effectType)
	{
	case USB_EFFECT_SQUARE:
	case USB_EFFECT_SINE:
	case USB_EFFECT_TRIANGLE:
	case USB_EFFECT_SAWTOOTHDOWN:
	case USB_EFFECT_SAWTOOTHUP:
	case USB_EFFECT_RAMP:
	{
		FFW_MIDI_Effect_Periodic_Ramp_t* midi_data =
			(FFW_MIDI_Effect_Periodic_Ramp_t*)e->data;
		FfbwheelSetModify14(e, &midi_data->fadeTime, data->effectBlockIndex,
			FFW_MIDI_PERIODIC_FADE_TIME, fadeTime);
		midi_data_len = sizeof(FFW_MIDI_Effect_Periodic_Ramp_t);
	}
	break;

	case USB_EFFECT_CONSTANT:
	case USB_EFFECT_CUSTOM:
	{
		FFW_MIDI_Effect_ConstantForce_t* midi_data =
			(FFW_MIDI_Effect_ConstantForce_t*)e->data;
		FfbwheelSetModify14(e, &midi_data->fadeTime, data->effectBlockIndex,
			FFW_MIDI_CONSTANT_FADE_TIME, fadeTime);
		if (data->effectType == USB_EFFECT_CONSTANT)
			FfbwheelUpdateConstantForce(data->effectBlockIndex, e);
		midi_data_len = sizeof(FFW_MIDI_Effect_ConstantForce_t);
	}
	break;

	case USB_EFFECT_SPRING:
	case USB_EFFECT_DAMPER:
	case USB_EFFECT_INERTIA:
	{
		midi_data_len = sizeof(FFW_MIDI_Effect_Spring_Inertia_Damper_t);
	}
	break;

	case USB_EFFECT_FRICTION:
	{
		midi_data_len = sizeof(FFW_MIDI_Effect_Friction_t);
	}
	break;

	default:
	{
	}
	break;
	}

	return midi_data_len;
}

void FfbwheelCreateNewEffect(
	USB_FFBReport_CreateNewEffect_Feature_Data_t* data,
	volatile TEffectState* effect)
{
	FFW_MIDI_Effect_Common_t* c = (FFW_MIDI_Effect_Common_t*)&effect->data;
	c->command = 0x20; // always 0x20
	c->unknown1 = 0x7f; // always 0x7f
	c->direction = 0x00; // 0 for effect not using direction

		/* ramp   f0 00 01 0a 15 20 05 7f 6e 1e 40 7f 00 00 7f 00 00 7f 6e 1e 7f 6e 1e 3e  3e f7 */
	/* damper f0 00 01 0a 15 20 09 7f 6e 1e 00 00 7d 3e 3f 3e 3f 7d 00  58 f7 */
	/* spring f0 00 01 0a 15 20 08 7f 6e 1e 00 00 7d 3e 4e 3e 2f 7d 00  5a f7 */

	/*

	effect command for waveforms

   xx 			effect type (sine=2, square=3, triangle=4, sawtooth=5)
   7f			always 7f
   xx xx		duration
   xx 		        direction (angle*128/360)
   ??			precise direction indicator ? 7f=precise, 7d=not
   00 40		periodic x-offset, default 00 40
   7f			envelope y1, default 7f
   00 00		envelope x1, default 00 00
   7f			periodic amplitude
   65 12		envelope x2
   7f			envelope y2
   74 03		periodic x-hz
   3e			periodic y-offset
   */



	switch (data->effectType)
	{
	case USB_EFFECT_SQUARE:
	case USB_EFFECT_SINE:
	case USB_EFFECT_TRIANGLE:
	case USB_EFFECT_SAWTOOTHDOWN:
	case USB_EFFECT_SAWTOOTHUP:
	case USB_EFFECT_RAMP:
	{
		FFW_MIDI_Effect_Periodic_Ramp_t* midi_data =
			(FFW_MIDI_Effect_Periodic_Ramp_t*)effect->data;
		midi_data->common.direction = 0x40;

		midi_data->preciseDirection = 0x7f;
		midi_data->attackLevel = 0x7f;
		midi_data->attackTime = 0x0000;
		midi_data->magnitude = 0x7f;
		midi_data->fadeLevel = 0x7f;
		midi_data->offset = FFW_PERIODIC_OFFSET_ZERO;

		if (data->effectType == USB_EFFECT_RAMP) {
			midi_data->phase = 0x0000;
			midi_data->fadeTime = 0x1e6e;
			midi_data->frequency = 0x1e6e;
		} else {
			midi_data->phase = 0x4000; // 0x00 0x40
			midi_data->fadeTime = 0x1265;
			midi_data->frequency = 0x0374;
		}
	}
	break;

	case USB_EFFECT_CONSTANT:
	case USB_EFFECT_CUSTOM:
	{
		FFW_MIDI_Effect_ConstantForce_t* midi_data =
			(FFW_MIDI_Effect_ConstantForce_t*)effect->data;
		midi_data->unknown2 = 0x7f;
		midi_data->attackLevel = 0x7f;
		midi_data->attackTime = 0x0000;
		midi_data->magnitude = 0x7f;
		midi_data->fadeTime = MIDI_DURATION_INFINITE;
		midi_data->fadeLevel = 0x7f;
		midi_data->forceDirection = 0x00;
	}
	break;

	case USB_EFFECT_SPRING:
	case USB_EFFECT_DAMPER:
	case USB_EFFECT_INERTIA:
	{
		FFW_MIDI_Effect_Spring_Inertia_Damper_t* midi_data =
			(FFW_MIDI_Effect_Spring_Inertia_Damper_t*)effect->data;
		midi_data->negativeCoefficient = 0x7d00;
		midi_data->conditionParameters[0] = 0x3e;
		midi_data->conditionParameters[1] = 0x3f;
		midi_data->conditionParameters[2] = 0x3e;
		midi_data->conditionParameters[3] = 0x3f;
		midi_data->positiveCoefficient = 0x007d;
	}
	break;

	case USB_EFFECT_FRICTION:
	{
		FFW_MIDI_Effect_Friction_t* midi_data =
			(FFW_MIDI_Effect_Friction_t*)effect->data;
		midi_data->coefficient = 0x7e;
	}
	break;

	default:
	{
	}
	break;
	}
}
