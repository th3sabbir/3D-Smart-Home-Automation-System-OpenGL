
# Smart Home Automation System 3D (CGM Project)

A 3D cutaway (dollhouse) smart home built with **C++ / OpenGL / GLU / FreeGLUT**.

## Features

* 3 rooms: **Living Room, Bedroom, Kitchen** (cutaway view)
* 3D furniture: sofa, coffee table, TV, bed, wardrobe, refrigerator, kitchen cabinets
* Room lights with real OpenGL point lights, glowing lamps and light cones
* Animated rotating ceiling fans with 3 speed levels
* Animated main door (swings) and bedroom window (slides)
* Security system with flashing alarm when the door opens while armed
* Simulated temperature + electricity consumption (kWh) monitoring
* Professional status dashboard (2D overlay)
* Orbit camera: mouse drag / arrow keys, mouse wheel zoom

## Keyboard Controls

| Key | Action |
|-----|--------|
| `1` `2` `3` | Living / Bedroom / Kitchen light |
| `F` `B` | Living / Bedroom fan ON-OFF |
| `+` `-` | Fan speed (1-3) |
| `D` | Main door open/close |
| `W` | Window open/close |
| `C` | Roof show/hide (cutaway view) |
| `S` | Security arm/disarm |
| `A` | Reset alarm |
| `T` `Y` | Temperature up/down |
| `H` | Help overlay |
| `R` | Reset system |
| Arrow keys | Rotate camera |
| Mouse drag | Rotate camera |
| Mouse wheel | Zoom |
| `ESC` | Exit |

# Setup

## 1.  Windows ##

   * ### Download MSYS2 Package Manager from https://www.msys2.org/ . ###  
   * ### Create **build** and **lib** folder in Code Repo. Put *glfw3.dll* from *lib-mingw-w64* folder of glfw library to **build** and **lib** folder.  ###
   * ### For C++ compiler and make, Download MSYS2 Package Manager from https://www.msys2.org/ . ###  
   * ### Run following commands in MSYS2 terminal: ```pacman -S base-devel```, ```pacman -S make``` and ```pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-freeglut```. ###
   * ### add your bin folder (which includes g++.exe and make.exe) PATH (C:\msys64\usr\bin) of msys64 installation directory in your environment variable.
   * ### Run ```make win``` in Terminal (or ```make build``` to compile without running). ###
   * ### ```.exe``` file will be in **build** folder. ###
   * ### if your code does not run, then you have to check openGL version by installing GLview from http://www.realtech-vr.com/home/glview . If your openGL version is below 3.3, then update windows. ### 
​
## 2. Linux ##

   * ### Run following command in Terminal:  ```sudo apt-get install freeglut3-dev``` ###  
   * ### Create **build** and **lib** folder in Code Repo. ###
   * ### Run ```make linux``` in terminal. ###
   * ### executable file will be in **build** folder. ###
