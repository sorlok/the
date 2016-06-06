# the
Interface to Wii Remotes +X11

Compile:
g++ -std=c++11  -o the main.cpp  -lbluetooth -pthread -lx11 -lxtst

Requires 2 Wii remotes. Press +/- to switch LHS/RHS. Focus window gets key events. Have fun!
