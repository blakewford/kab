# kab

C/C++ version of the WiOn (KAB enterprises protocol)
Tested against the WiOn 50049 Outdoor WiFi Plug.

First pass runs a device discovery, saves to file.
Second pass devices are operable (OFF/ON).
To rediscover, delete the cache.json file.

wip Wemo adapter for convenience and better IoT integration including the wemo app and Google Home.
Intended target is an RPi bridge device that launches kab on startup.
