//Linux-only includes.
#ifndef _WIN32
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#endif

#include <iostream>

#include "wii_remote.h"

//Compile like so:
// g++ -std=c++11  -o the main.cpp  -lbluetooth -pthread -lX11 -lXtst
//Windows, "Native" command line, then:
// cl  /EHsc /O2  Main.cpp   /link /OUT:The.exe /DYNAMICBASE "Hid.lib" "Setupapi.lib" "Ws2_32.lib" "User32.lib"
//See bottom of file for licensing information.

WiiRemoteMgr* wiiRemote = nullptr;

void on_kill(int s)
{
  std::cout <<"Caught signal: " <<s <<std::endl;
  if (wiiRemote != nullptr) {
    wiiRemote->shutdown(); //This crashes, but whatever; the BT connection is still killed.
    delete wiiRemote;
  }
  std::cout <<"Shutting down" <<std::endl;
  exit(1); 
}

int main(int argc, char *argv[]) {
  //Starts the thread.
  wiiRemote = new WiiRemoteMgr();
  
  //Set up Ctrl+C handler.
  //This isn't needed on Windows, since the Wii Remote will always be paired even when the app is closed (need to manually remove it in the BT panel).
#ifndef _WIN32
  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = on_kill;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, NULL);
#endif

  //Wait for kill signal.
  //pause();

  //TODO: Maybe wait for thread join instead?
  wiiRemote->waitForDone();

  //NOTE: Can get stuff like this.
  //int steps = wiiRemote.get_and_reset_steps();
  //float batLHS = wiiRemote.get_battery_status_lhs();
  //float batRHS = wiiRemote.get_battery_status_rhs();

  //Done
  std::cout <<"Normal exit" <<std::endl;
  delete wiiRemote;
  std::cout <<"Shutting down" <<std::endl;

  return 0;
}


//Copyright (c) 2016, Seth N. Hetu
//All rights reserved.
//
//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions are met:
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//    * Neither the name of the Seth N. Hetu nor the
//      names of its contributors may be used to endorse or promote products
//      derived from this software without specific prior written permission.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
//ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//DISCLAIMED. IN NO EVENT SHALL Seth N. Hetu BE LIABLE FOR ANY
//DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
//ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.