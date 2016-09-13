# the
Interface to Wii Remotes +X11

Compile:
g++ -std=c++11  -o the main.cpp  -lbluetooth -pthread -lx11 -lxtst

On Windows:
  (From the start manu, type "Native", and choose "VS2015 x64 Native Tools Command Prompt")
Then, cd to the folder, then:
  cl  /EHsc /O2  Main.cpp   /link /OUT:The.exe /DYNAMICBASE "Hid.lib" "Setupapi.lib" "Ws2_32.lib" "User32.lib"


Requires 2 Wii remotes. Press +/- to switch LHS/RHS. Focus window gets key events. Have fun!
