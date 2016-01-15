# LEDBrick Project

An Open Hardware design for Aquarium LED lighting.

For more details, see: [the ReefCentral thread][1] and [the blog][2].

## Emitter Board


![Brick][3]

## Bluetooth Low Energy PWM Board

An 8 channel 24VIN + 12VOUT Fan LED dimmer board powered by a Nordic nRF51 basic Bluetooth Low Energy / Bluetooth Smart microcontroller.

[Firmware is available for both Keil and GCC compilers][5]

[Design files and gerber files][6]

![PWM][4]

[1]: http://www.reefcentral.com/forums/showthread.php?t=2477205
[2]: http://yannramin.com/elec/ledbrick-pt1/
[3]: http://yannramin.com/images/ledbrick/ledbrick-board-top-sm.jpg
[4]: https://raw.githubusercontent.com/theatrus/ledbrick/master/pwm/board.png
[5]: https://github.com/theatrus/ledbrick/tree/master/firmware/nordic_nrf51/app/ble_peripheral/ledbrick_pwm/pca10028/s110
[6]: https://github.com/theatrus/ledbrick/tree/master/pwm/0.2/
