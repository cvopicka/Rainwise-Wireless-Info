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

| Byte | Data | Example |Result|
| :---: | :--- | :---: | --- |
|01|Header|33||
|02|Header|80||
|03|Header|00||
|04|Temperature F * 10|01|299|
|05|Temperature F * 10|2B|-|
|06|Humidity %|4A|74|
|07|Barametric Pressure inHg * 1000|64|25755|
|08|Barametric Pressure inHg * 1000|9B|-|
|09|Wind Speed MPH * 10|00|30|
|10|Wind Speed MPH * 10|1E|*|
|11|Wind Direction Degrees|00|45|
|12|Wind Direction Degrees|2D|-|
|13||3D||
|14||C2||
|15||EB||
|16||04||
|17||B4||
|18||B4||
|19||00||
|20|Battery Voltage * 100|02|708|
|21|Battery Voltage * 100|C4|-|
|22||02||
|23||A6||
|24||00||
|25||00||
|26||00||
|27||00||
|28||00||
|29||00||
|30||00||
|31|Checksum **|AC||
|32|Checksum **|5E||

### Assumptions

** Assumed based on other data streams I have had to figure out.  See GPS NMEA data streams as example or other serial devices.

- Device ID included
- Several empty bytes for devices my station does not have
- Empty bytes for rain gauge.  Currently snowing so no way to easily check.

### RAW

`33 80 00 01 2B 4A 64 9B 00 1E 00 2D 3D C2 EB 04 B4 B4 00 02 C4 02 A6 00 00 00 00 00 00 00 AC 5E
`

### Closest XML Snapshot

```XML
<?xml version="1.0" encoding="ISO-8859-1"?>
<!-- Weather and hardware paramters for the IP-100, min's, max's and accumulations are for the current day -->
<status>
 <hardware>
   <serial_number></serial_number>
   <firmware_version>2083</firmware_version>
   <clock>2024/03/23 12:06:48</clock>
   <station_volts>7.0</station_volts>
   <network>
     <MAC_address></MAC_address>
     <IP_address></IP_address>
  <subnet></subnet>
  <gateway></gateway>
   </network>
   <interval>1</interval>
   <base_units>English</base_units>
   <speed_units>mph</speed_units>
 </hardware>

 <weather>
 <temperature_outside>
  <current>29.9</current>
  <max>30.0</max>
  <min>26.3</min>
 </temperature_outside>

 <humidity>
  <current>74</current>
  <max>74</max>
  <min>67</min>
 </humidity>

 <pressure>
  <current>25.76</current>
  <max>25.95</max>
  <min>25.76</min>
 </pressure>

 <precipitation>
  <current>0.00</current>
 </precipitation>

 <wind>
  <speed>5.4</speed>
  <direction>45</direction>
  <gust_speed>7.2</gust_speed>
  <gust_direction>45</gust_direction>
 </wind>

 <temperature_inside>
  <current>60.0</current>
  <max>62.0</max>
  <min>60.0</min>
 </temperature_inside>












 </weather>
</status>
```

## Receiver to Display Pinout

4 pins
9V power supply

Suspect RS485 or RS422 - Half Duplex

