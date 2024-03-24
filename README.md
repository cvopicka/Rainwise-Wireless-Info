# Rainwise-Wireless-Info

MK3-LR XBee data information

## Introduction

Recently I had a power failure at home.  When the power returned my oracle display stopped displaying data.  Having had to replace the wireless module long ago on another Rainwise i assumed it was the receiver.  I have reached out to 4x different support contacts for Rainwise and gotten no response.  So what does a reasonable person do??? I have no idea but this is what I did.

## Inspection

I started visually inspecting pieces.  The display lit up just showed no data.  The receiver box showed a blinking light.  Something was recieving data but not getting to the display (initial evaluation)

I disassembled the receiver box.  Found what i am assuming is mostly a voltage level shifter and a Digi XBee3 module.

Ordered XBee breakout board from amazon

Connected module to break out board and plugged in to my PC

Data was being recieved

Found the serial data stream to be configured at 9600 8N1.  This also matched some info I had located about the Digi XBee3 module.

## Data

The data being received was a 32 byte data stream

All data started with HEX `33 80 00`

The trailing null may or may not be part of the data.  I have only seen NULL

Here is a guess on the data as best as I have decoded by comparing the data to my IP-100 module that is receiving data.

| Byte | Data | Example |
| :---: | :--- | :---: |
|01|Header||
|02|Header||
|03|Header||
|04|Temperature F * 10||
|05|Temperature F * 10||
|06|Humidity %||
|07|Barametric Pressure inHg * 10||
|08|Barametric Pressure inHg * 10||
|09|Wind Speed MPH * 10||
|10|Wind Speed MPH * 10||
|11|Wind Direction Degrees||
|12|Wind Direction Degrees||
|13|||
|14|||
|15|||
|16|||
|17|||
|18|||
|19|||
|20|Battery Voltage * 10||
|21|Battery Voltage * 10||
|22|||
|23|||
|24|||
|25|||
|26|||
|27|||
|28|||
|29|||
|30|||
|31|Checksum **||
|32|Checksum **||

** Assumed based on other data streams I have had to figure out
