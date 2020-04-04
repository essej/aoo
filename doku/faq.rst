FAQ collection, maybe goes into doku
====================================


Functionality
-------------

What is the lowest Latency possible ?

You definitely can stream between apps on the same computer. 
Regarding latency you have to measure, but there are basically three 
parameters regarding the latency in the case of Pd:

a) network latency (negligible for localhost)

b) hardware buffer size / Pd latency

c) [aoo_receive~] buffer size

For exampke on my Windows 7 laptop with an ASIO driver and 64 sample hardware buffer size and 5ms Pd latency, I can set the [aoo_receive~] buffer size down to 4-5 ms. On some other systems, you might get even lower. One of the use cases of AOO is certainly low latency audio streaming over local networks, especially over long periods of time (literally weeks or months, given that the devices have access to a NTP time server).

AOO does have a bit more latency than [pd~] because of the extra buffer in [aoo_receive~]. I think it rather depends what you need: [pd~] is slaved to the parent process, but provides sample accuracy. On the other hand, when you run a seperate Pd instance and pass audio with AOO, they 
are fully independent.

Give it a try and report back! Here's the latest snapshot (Just unpack 
the .dek archive): https://git.iem.at/cm/aoo/-/jobs/10983/artifacts/download

