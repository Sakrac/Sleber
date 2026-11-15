Sleber - A simple Commodore 64 graphics modes pixel editor
(c) Carl-Henrik Skårstedt 2026

Controls

Left button: draw with the foreground color
Right button: draw with the background color
Middle button: pan the canvas
Mouse wheel: zoom in or out
0-9, A-F: choose foreground color
Shift + 0-9, A-F: choose background color
Arrow keys: pan the canvas
Escape: cancel the current operation
Delete: fill the selection with the background color
M: start a selection, G: fill mode
T: show pixel information while held
U: show previous pixels while held
Ctrl+A: select all
Ctrl+S: Save the current file
Ctrl+Shift+S: Save file as
Ctrl+L or Ctrl+O: open image file
Ctrl+C / Ctrl+X: copy or cut the selection
Ctrl+V: paste
Shift+Ctrl+V: paste while skipping background color
Ctrl+Z / Ctrl+Shift+Z: undo / redo a drawing step
Ctrl+U / Ctrl+Shift+U: undo / redo one frame's edit
Ctrl+I: import image


This program was originally intended to be a basic Linux port of
Pixcen by John Hammarberg.

That attempt failed explosively so instead I started over with
a completely rewritten tool. The purpose of it is basically that
I want to practice writing native UI code.

This repository contains no original Pixcen code, everything is
written in C for Win32 and GTK3 without referencing Pixcen.

The supported screen modes include:

Character Mode (text)
Multicolor Character Mode (multicolor text)
Bitmap Mode
Multicolor Bitmap Mode
ECBM Mode (text with 4 background colors)
Mixed Mode (Multicolor Character Mode with optional first eight colors Character Mode)
Sprite Mode
Multicolor Sprite Mode

(Modes not supported by GPX can not be saved as GPX).


The MIT License (MIT)

Copyright (c) 2015 Carl-Henrik Skårstedt

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
