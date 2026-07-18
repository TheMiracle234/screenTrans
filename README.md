# screenTrans - Online Meeting App

---

## Installation ##

**Easy way:**

1. Unzip this zip.
2. Unzip `msvc_run_time.zip`.
3. Copy all files from the extracted `msvc_run_time` folder and paste them into the folder that contains your `.exe` file.
4. You can now run the executable.

**To save space:**

The `msvc_run_time.zip` contains files you may not actually need. Consider these options:

- **If** you have Visual Studio (my version is 2026) or a MSVC compiler and can run the program without extracting these (since C++ is not ABI stable), simply delete this zip.
- **Else if** you cannot run, copy the files from `msvc_run_time.zip` to a specific folder and add that folder to the `PATH` environment variable.
- **Else** (i.e., you are not sure), I suggest you unzip it regardless of whether you can run it now, because the `.dll`s of other software may be removed in the future.

---

## Usage ##

**Remind:**

- Maybe you can run normally only if you **turn down the Firewall**

**Server:**

- run and set port

**Client:**

- in cmd: do as prompt
- in graphic: press `esc` to control setting panel

---

## Compile by yourself

notice that I didn't put source code of miniaudio and FFmpeg here, you need to download them seperatly. For the reason, include path in `.sln` is absolute path. So you need to reset include path of miniaudio and FFmpeg to your local path.

I'm not familiar with compiling `.dll`, so I first compile screenTrans to a `.lib`, then compile ScreenTrans_Client and ScreenTrans_Server using this '.lib'.

## Source Code

### screenTrans:

https://github.com/TheMiracle234/screenTrans/commit/35c6464c91c1d6ef06f7e73a46805538e7acf9cd

### FFmpeg:

https://github.com/FFmpeg/FFmpeg/commit/239f2c733d

---

## Third-Party Licenses ##

### freetype

- Portions of this software are copyright © 2025 The FreeType Project (www.freetype.org). All rights reserved.

### glew

- This software uses the OpenGL Extension Wrangler Library (GLEW).
Copyright (C) 2002-2007, Milan Ikits, Marcelo E. Magallon, Lev Povalahev.
All rights reserved. Licensed under the BSD 3-Clause License.

### GLFW

- Copyright (c) 2002-2006 Marcus Geelnard  
- Copyright (c) 2006-2019 Camilla Löwy  
- Licensed under the zlib/libpng License.

### GLM (OpenGL Mathematics)

- Copyright (c) 2005 - G-Truc Creation. Licensed under The Happy Bunny License or MIT License.

### imgui

- Copyright (c) 2014-2026 Omar Cornut

### miniaudio

- Copyright (c) 2025 David Reid. Licensed under Unlicense or MIT No Attribution (MIT0).

### stb_image

- Copyright (c) 2017 Sean Barrett. Licensed under MIT License or Public Domain (Unlicense).

### FFmpeg

- This software uses code from the FFmpeg project, which is licensed under the GNU General Public License v3.0.
In accordance with GPLv3, the complete corresponding source code is available here:
https://github.com/FFmpeg/FFmpeg/commit/239f2c733d
- FFmpeg official source code: https://ffmpeg.org/download.html
- FFmpeg version used in this project: 8.1.1
