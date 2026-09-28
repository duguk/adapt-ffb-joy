# What is Adapt-FFB-Joy #

Adapt-FFB-Joy is an AVR microcontroller based device that looks like a joystick with advanced force feedback features in a Windows machine without need for installing any device drivers to PC.

This project contains the software for the AVR microcontroller. The original instructions for [building the hardware](https://github.com/tloimu/adapt-ffb-joy/blob/wiki/HowToBuild.md) are found on the [Adapt-ffb-joy Wiki](https://github.com/tloimu/adapt-ffb-joy/blob/wiki/README.md), and some alternative instructions are below.

For more information, see [Adapt-ffb-joy Wiki](https://github.com/tloimu/adapt-ffb-joy/blob/wiki/README.md)

# Status of this fork #

### **Steering Wheel (including Force Feedback) is successfully working in Linux!**

This fork is based on ej113's `master` branch. ej113's later work, merged into tloimu's `master` as 0.5.0beta1 (mainly further Force Feedback Pro effect fixes), isn't included yet.

**Sidewinder Force Feedback Wheel: working on Linux.** Tested on real hardware with kernel 6.18 and the in-kernel `hid-pidff` driver:

* steering, pedals and all buttons
* force feedback effects, checked with `fftest` and raw MIDI over the debug port
* DiRT Rally 2.0 under Proton (after the [`device_defines.xml` change](#game-notes))

**Not yet tested:**

* the wheel on Windows. The Linux fixes changed how the PID Block Load result is returned, so Windows needs retesting.
* the Force Feedback Pro joystick with this fork's changes. Most of the changes are in the wheel code, but the joystick shares the USB and PID handling.

Bug reports and test results, especially from Windows or Force Feedback Pro users, are welcome.

# How an LLM was used in this project #

This fork heavily uses an LLM. Why? I've owned this wheel since around 1998, and since support was dropped and hardware unavailable, I've wanted to see this working again. Many people have worked hard to contribute to this project, but support for Linux has been lacking. I've found it hard to find the time to do so myself, but desperately wanted to see it working. So, I've used an AI coding assistant, to help read the documentation, as well as write and debug the code.

Most of the wheel and Linux work in this fork was written with the help of an AI coding assistant, Anthropic's Claude (through Claude Code). The assistant read the existing code and the kernel's `hid-pidff` behaviour, proposed and wrote the changes, and drove tests through the debug serial port and the Linux input devices.

The Sidewinder wheel's MIDI protocol isn't documented, so much of the work was experimenting on the hardware. The assistant sent raw MIDI to the wheel, then watched the steering axis and the debug log to work out what each parameter does. For example, it worked out that the condition coefficients are centred on `0x40`, and which low-byte values add an unwanted constant push. The comments in `ffb-wheel.c` and `ffb-wheel.h` record these findings.

A human did all of the hardware setup, physical testing and game testing, and decided what to keep. Even so, treat the code the way you would any contribution: read it, test it on your own hardware, and report anything that looks wrong.

# Quick start #

## What you need ##

* a Microsoft Sidewinder Force Feedback Wheel or Force Feedback Pro, with its own power supply (the force feedback motors aren't powered over the game port)
* a Teensy 2.0 or a clone (ATmega32U4 at 16 MHz, 5 V). Other ATmega32U4 boards such as the Pro Micro or Leonardo don't break out all the pins this wiring uses.
* a female DB15 connector, to plug the game port cable into
* 2 × 2.2 kΩ resistors and 1 × 220 Ω resistor
* optionally, 2 × 1 nF capacitors, and a relay for back-power protection (see [My Hardware Modifications](#my-hardware-modifications))

## Steps ##

1. **Build the adapter.** Wire the Teensy to the DB15 connector as shown in the [circuit diagram](#circuit-diagram).
2. **Build and flash the firmware.** Connect the Teensy by USB, run `make` (or use the existing [Joystick.hex](Joystick.hex) file provided), then press the Teensy's button and run `teensy_loader_cli --mcu=TEENSY2 -w Joystick.hex`. See [Building and flashing](#building-and-flashing).
3. **Connect it up:** Connect its game port cable to the adapter AFTER powering on the wheel or joystick.
   Connecting the game port to an unpowered Teensy can back-power it. The relay modification is one way to solve this problem.
   The Teensy's LED flashes until it finds the wheel or joystick. Only then does the adapter appear on USB.
4. **Turn the Force switch on** (wheel only). With it off, the wheel ignores all force feedback.
5. **Check that it works.**
   * **Linux:** the device appears as *Sidewinder Wheel FFB* or *Sidewinder Joystick FFB*. Check the axes and buttons with `evtest`, and force feedback with `fftest /dev/input/eventN` from the `linuxconsole` tools. Reading `/dev/input/event*` usually needs root or membership of the `input` group.
   * **Windows:** open *Set up USB game controllers* (`joy.cpl`) and check the axes and buttons in *Properties*.
6. **Set up your games.** Some games need extra setup to use the wheel's force feedback. See [Game notes](#game-notes).

# How to Build #

## My Hardware ##

I used a Teensy 2.0 clone with a 16 MHz crystal from [Amazon](https://www.amazon.co.uk/dp/B0C6T33T7W).

[ej113 reported a back-powering potential](https://github.com/tloimu/adapt-ffb-joy/issues/49) which means that the Teensy should be connected first, before the gameport is connected.
beel1 suggested adding a 220R resistor for each of the buttons to reduce this problem, which may help to provide some protection.

I also added a relay to avoid this problem, effectively connecting the ground only when the Teensy is powered. This avoids switching all the other pins, and means the gameport can stay connected to the wheel.

To do this, the control side of the relay is connected to the power and ground rails of the Teensy, so it turns on when the Teensy is powered. The switching side connects the ground to the gameport to the ground of the Teensy. This effectively means the ground is disconnected from the wheel, until the Teensy is powered, which mitigates the problem.

## Circuit diagram ##

This is the original adapter circuit ([schematic image](downloads/adaptffbjoy-circuit.png), [TinyCAD file](adaptffbjoy-circuit-tinycad.dsn)), redrawn as text. Teensy 2.0 pin numbers are in brackets. Both the Force Feedback Pro and the Force Feedback Wheel use the same wiring.

The diagram is quite hard to read, be aware that the text labels aren't necessarily closest to the lines.  

Also note that many of the pins are missing on the Gameport connector, because they are not used. You can use this to determine the orientation - as all pins should be connected.

I kept the 1nF capacitors for PB4/PB5 but I'm not sure if these are necessary.

| AVR | DB15 | Desc                                              |
|:----|:-----|:--------------------------------------------------|
| PB0 | 2 | Button1                                           |
| PB1 | 7 | Button2                                           |
| PB2 | 10 | Button3                                           |
| PB3 | 14 | Button4                                           |
| PB4 | 11 | X1 (with 2.2k Ohm resistor in series)             |
| PB5 | 3 | X2 (with 2.2k Ohm resistor in series)             |
| PD0 | 2 | Button1 (INT)                                     |
| PD3 | 12 | MIDI out (with 220 Ohm resistor in series)        |
| VCC | 1 | Vcc for joystick                                  |
| GND | 4 | GND for joystick |

## Relay Modification ##

If using a relay (e.g. SRD-05VDC-5L), the GND wire between AVR and Joystick should be split, and connected to the NO pin and Common pin on the switched side of the relay.

The powered side of the relay should simply be connected to the AVR power and ground. This means that the relay is off when not connected to USB, so that the AVR is not back-powered.

# History of this fork #

* [tloimu/adapt-ffb-joy](https://github.com/tloimu/adapt-ffb-joy) is the original project, with Sidewinder FF Wheel support started by Saku Kekkonen.
* [ej113/adapt-ffb-joy](https://github.com/ej113/adapt-ffb-joy) worked on bringing the FFP force feedback as close as possible to a full implementation of the USB PID spec (see this [discussion thread](https://github.com/tloimu/adapt-ffb-joy/discussions/45)).
* [duguk/adapt-ffb-joy](https://github.com/duguk/adapt-ffb-joy) (this fork) builds on ej113's work and finishes support for the **Microsoft Sidewinder Force Feedback Wheel**, and makes the adapter work with the Linux kernel's generic force feedback driver (`hid-pidff`) as well as Windows.

# Sidewinder Force Feedback Wheel support #

The adapter detects at power-up whether a joystick or a wheel is connected to the game port. The same hardware and firmware serve both. The wheel appears as a USB device called **Sidewinder Wheel FFB**. It has a steering axis, separate accelerator and brake axes, nine buttons (A, B, C, X, Y, Z, L, R and the Force switch) and PID force feedback.

## Force feedback ##

All the PID effect types are mapped onto the wheel's MIDI effects:

* constant force, ramp
* sine, square, triangle and sawtooth (the wheel has no sawtooth, so its ramp waveform stands in)
* spring, damper, inertia and friction
* envelopes, durations, device gain and effect direction (projected onto the steering axis)

The **Force** switch on the wheel is a hardware force feedback enable. When it is off, the wheel ignores all effects. The firmware also uses it to toggle the wheel's built-in centering spring. The switch is reported to the host as button 8.

Effect strength, minimum forces, the shortest periodic period, the constant force direction and the centering spring strength can be tuned at compile time. See the *Tuning* section at the end of `ffb-wheel.h`.

## Linux ##

The wheel works with the in-kernel `hid-pidff` driver, with no extra drivers needed. This fork adds what `hid-pidff` needs during initialisation:

* the PID Block Load result can be read with GET_REPORT after Create New Effect (the original code only handled the Windows sequence)
* PID State can be read with GET_REPORT
* writes to effect block 1 are ignored, because that block is the wheel's built-in centering spring
* the PID State report doesn't claim a Safety Switch, which Linux would show as an extra button

You can check force feedback with `fftest` or `ffcfhtest` from the `linuxconsole` tools. Turn the wheel's Force switch on first. Check you are using the correct device, by checking `dmesg` for the device name.

### Game notes ###

* **DiRT Rally 2.0** (Proton): the game only sends real forces once the wheel is listed in `DiRT Rally 2.0/input/devices/device_defines.xml`. Add this line inside the `<devices>` list:

  ```xml
  <device id="{205603EB-0000-0000-0000-504944564944}" name="sidewinder_ff_wheel" priority="100" type="wheel" ffb="enabled" official="false" />
  ```

  Steam's *Verify integrity of game files* reverts this file.

## Joystick changes ##

The Force Feedback Pro joystick report now uses separate Accelerator and Brake axes for the two optional pedal inputs, rather than combining them into a single rudder axis. The joystick's own throttle stays on its separate Throttle axis. The pull-ups on the four analogue input pins are now disabled so that the trim pots and pedals read correctly.

# Building and flashing #

With `avr-gcc` and `avr-libc` installed (any recent version):

```
make
```

This builds `Joystick.hex` for an ATmega32U4 at 16 MHz, such as a Teensy 2.0.

Flash it with the usual tool for your board, e.g. [Teensy Loader](https://github.com/paulstoffregen/teensy_loader_cli), `dfu-programmer` (`make dfu`) or `avrdude` for Caterina bootloaders.

```
teensy_loader_cli --mcu=TEENSY2 -w Joystick.hex
```

# Debug serial port #

By default the adapter also exposes a USB serial (CDC) port next to the joystick. On Windows, the `.inf` files in this directory install a driver for it. On Linux, it shows up as `/dev/ttyACM0`. The port logs the USB and MIDI traffic, and it accepts the ASCII command protocol described in `main.c`, for example sending raw MIDI to the wheel.

On Linux, set the port to raw mode before opening it, and again after each re-plug or re-flash:

```
stty -F /dev/ttyACM0 raw -echo
```

Otherwise the tty echoes debug output back to the adapter, which parses it as commands. The adapter only sends debug output while a program has the port open.

To build without the serial port, remove `#define ENABLE_JOYSTICK_SERIAL` in `Descriptors.h`.

# Thanks #

This fork stands on a lot of other people's work:

* **Tero Loimuneva ([tloimu](https://github.com/tloimu))** created Adapt-FFB-Joy, including the USB PID force feedback framework and the hardware design.
* **Saku Kekkonen** added the original Sidewinder Force Feedback Wheel support that this fork builds on.
* **Ed Wilkinson ([ej113](https://github.com/ej113))** reworked the Force Feedback Pro effects to follow the USB PID spec more closely, and reported the back-powering issue.
* **Detlef "Grendel" Mueller** wrote 3DPVert. The Sidewinder game port reading code comes from it.
* **Dean Camera** wrote the [LUFA](http://www.fourwalledcubicle.com/LUFA.php) USB library.
* **beel1** suggested the button resistors for back-power protection.
* Everyone in the upstream [issues](https://github.com/tloimu/adapt-ffb-joy/issues) and [discussions](https://github.com/tloimu/adapt-ffb-joy/discussions) who worked out how these devices behave.
