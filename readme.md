# glib-ringbuf

A lightweight ring buffer implementation for C using GLib and virtual memory tricks.
Uses clever virtual memory tricks inspired by 
[http://en.wikipedia.org/wiki/Circular_buffer](http://en.wikipedia.org/wiki/Circular_buffer) 
and [https://lo.calho.st/posts/black-magic-buffer/](https://lo.calho.st/posts/black-magic-buffer/).
It is thread safe and can be configured to block when full.

Supports DMA access.

## Building
1. Compile with your preferred C compiler and link against GLib.
2. Include ringbuf.h in your application.
To test the example, simply run `meson setup build`.

## Usage
Call ringbuf_new() to create a buffer, use ringbuf_push()/ringbuf_pop() for data,
and ringbuf_free() to clean up.

## License
TODO
