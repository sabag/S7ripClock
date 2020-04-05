# S7ripClock

This is an Arduino sketch code for supporting the S7ripClock model from https://www.thingiverse.com/thing:4190016 to allow installating on Wemos D1 Mini. The wemos has wifi and it can fetch the time from NTP server without the need for RTC module.

The local timezone is also fetched from the internet using timezonedb. You need an API KEY from them to allow the sketch to fetch the your local timezone using longitude and latitude.

There are several configuration that needs to be adjusted in the sketch code to allow it to work for you, such as wifi ssid , password, timezone db api key.
