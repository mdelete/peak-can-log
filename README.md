PeakLog - OSX user-space driver for PCAN-USB adapters
=====================================================

Description
-----------
Simple CAN-logger with user-space driver using IOKitLib and IOUSBLib for PEAK PCAN-USB Adapters.
The driver is based on the USBPrivateDataSample and the pcan linux driver.

Supported hardware
------------------
 * [PCAN-USB](http://www.peak-system.com/PCAN-USB.199.0.html)

Pasting CAN-Messages
--------------------
You can simply paste numbers into the log window, which will be translated into can-frames and sent over the bus. This has been tested with plain text, formatted text and *Numbers* spreadsheets.

For example:

    0x80000005 0xc0 0xff 0xee;

will send an *CAN2.0B* frame with identfier *5* and the trailing numbers as payload bytes. The frame-length will be automatically set to *3*.

You can paste multiple messages at once, just paste multiple rows (*Numbers*) or lines separated by newline and/or semicolon. For example:

    0x5 0xc0 0xff 0xee;
    0x13 0xba 0xbe;

will generate two *CAN2.0A* messages, the first with three byte payload, the second with two byte.

### Sending EXT-frames

You can send frames with *CAN2.0B* extended identifier by OR-ing a number respresenting the 29-bit identifier with 0x80000000. For example:

    0x80000005 0xc0 0xff 0xee;
    
will send a *CAN2.0B* frame with id *5* and three bytes payload.

### Sending RTR-frames

You can also paste RTR-frames by OR-ing the id you expect to answer with 0x40000000. RTR-frames should have a length field set stating the length of data you expect to receive. You can do this by appending dummy numbers (e.g. zeroes). For example:

    0x40000006 0 0
    
will send a RTR-frame to id 6 expecting two bytes in return.

Both flags can be combined by OR-ing with 0xC0000000

TODOs
-----
 * Get rid of too many global variables in the driver part
 * Export logs
 * Maybe some script interface

License
-------
Copyright (c) 2012 Marc Delling

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Changelog
---------
 * 2012-12-08 Initial commit

