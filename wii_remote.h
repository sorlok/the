#ifndef WII_REMOTE_H
#define WII_REMOTE_H

//See main.cpp for licensing information.

#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <deque>
#include <memory>
#include <iostream>

#include <cmath>

#define POWER_WORD_INCLUDE
#include "platform.h"
#undef POWER_WORD_INCLUDE

//Mimic SDL hats
/*#define HAT_CENTERED    0x00
#define HAT_UP          0x01
#define HAT_RIGHT       0x02
#define HAT_DOWN        0x04
#define HAT_LEFT        0x08*/


namespace {
	const unsigned int MAX_EVENT_LEGNTH = 32;
	
	//TODO: Investigate.
    uint32_t currTimestamp = 0; 
}

struct HatState {
	HatState() : up(false), left(false), right(false), down(false) {}
	bool up;
	bool left;
	bool right;
	bool down;

	bool operator==(const HatState& other) const {
		return (left==other.left) && (right==other.right) && (up==other.up) && (down==other.down);
	}
};


//Helper class: represents button presses.
struct WiiButtons {
	WiiButtons() : dpadLeft(false), dpadRight(false), dpadUp(false), dpadDown(false),
	            btnA(false), btnB(false), plus(false), minus(false), home(false), one(false), two(false) 
	            {}

	bool dpadLeft;
	bool dpadRight;
	bool dpadUp;
	bool dpadDown;
	bool btnA;
	bool btnB;
	bool plus;
	bool minus;
	bool home;
	bool one;
	bool two;
};

//Helper class: represents Gamepad button presses (translated from Wii).
struct WiiTransGamepad {
	WiiTransGamepad() : dpadLeft(false), dpadRight(false), dpadUp(false), dpadDown(false),
						btnOk(false), btnCancel(false), btnAux(false), btnMenu(false)
						{}

	bool dpadLeft;
	bool dpadRight;
	bool dpadUp;
	bool dpadDown;
	bool btnOk;
	bool btnCancel;
	bool btnAux; //A on the d-pad side. Rarely used, but good to have mapped.
	bool btnMenu;
};


//Helper class: represent a request for data (from the EEPROM)
struct DataRequest {
	DataRequest() : buffer(nullptr), addr(0), length(0) {}
	unsigned char* buffer;  //Stores data when it is read. Must be freed by receiver.
	unsigned int addr;      //What address to start reading from in EEPROM.
	unsigned short length;  //How many bytes to read.
	//unsigned short wait;    //TODO: How many bytes are left to read.
};


//Helper class: represents a Wii Remote
struct WiiRemote {
	//Various handshake flags. If false, a handshake has not occurred for this element.
	bool handshake_calib;  //Calibration constants handshake.

	//List of pending requests. Front = oldest request.
	std::deque<DataRequest> requests;
	bool waitingOnData;  //If false, we can send out a new request.

	int id; //Index in array.

	unsigned char event_buf[MAX_EVENT_LEGNTH]; //Stores the currently-read data.

	int lastStatus; //How many ticks since we got a status report.

	WiiButtons currButtons;

#define POWER_WORD_CLASSDEF_WII_REMOTE
	#include "platform.h"
#undef POWER_WORD_CLASSDEF_WII_REMOTE
};


//This class wraps all the functionality required to read sensor data from a Wii Remote.
class WiiRemoteMgr {
public:
	WiiRemoteMgr();

	void shutdown() {
		std::cout <<"WiiRemote: Shutdown start.\n";
		running = false;
		main_thread.join();
		std::cout <<"WiiRemote: Shutdown complete.\n";
	}

	//TODO: Better design
	void waitForDone() {
		main_thread.join();
	}

	//Get the step count and clear it.
	int get_and_reset_steps() {
		return std::atomic_exchange(&currStepCount, 0);
	}

	//Get the battery status.
	float get_battery_status_rhs() {
		return batteryStatusRHS;
	}
	float get_battery_status_lhs() {
		return batteryStatusLHS;
	}


protected:
	//Find the Window!
	bool WiiRemoteMgr::findWindow();

	void run_main() {
		std::cout <<"WiiRemote: Connect start.\n";
		if (!findWindow()) {
			return;
		}
		if (!connect()) {
			return;
		}

		std::cout <<"WiiRemote: At start of main loop.\n";
		while (running.load()) {
			for (std::shared_ptr<WiiRemote>& remote : remotes) {
				sendPending(*remote);
			}
			WiiTransGamepad oldGamepad = currGamepad;
			poll();
			pushKeys(oldGamepad);

			//Relinquish control (next Wii input comes in after 10ms)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		disconnect();
	}


private:
	bool connect() {
		//First step: Scan for Wii Remotes.
		if (!scan_remotes()) {
			return false;
		}


		//Second step: connect to each one.
		for (std::shared_ptr<WiiRemote>& remote : remotes) {
			if (!btconnect(*remote)) {
				return false;
			}
			if (!handshake_calib(*remote)) {
				return false;
			}

			//TODO: We eventually want to turn on the Motion Plus.
		}

		//Third step: handshake each one (todo: separate out later)
		//TODO: Do we also want to set the initial reporting mode later?
		//TODO: And get status
		//TODO: And set initial LEDs
		//TODO: And turn on the Motion+ Extension (just in case we need it later).


		//Done.
		return true;
	}

	//Scan for 2 Wii Remotes. Return true if we're good to go from here.
	bool scan_remotes();


	//Connect to a bluetooth remote, setting up all resources needed for sending/receiving data.
	bool btconnect(WiiRemote& remote);

	bool handshake_calib(WiiRemote& remote) {
		std::cout <<"Starting handshake (calibration) for Wii Remote\n";

		//Set a good starting mode for data reporting. (NOTE: Putting this after LEDs can cause some error messages)
		send_data_reporting(remote, true, 0x30);

		//We start with an LED change request.
		send_leds(remote, true, true, true, true);

		//Send a request for data.
		send_status_request(remote);

		//Maybe do this first?
		send_data_reporting(remote, true, 0x35);

		//Send request for calibration data.
		unsigned char* buf = (unsigned char*)malloc(sizeof(unsigned char) * 8); //TODO: leaks?

		//TODO: No callback needed; we can respond to this in the polling loop.
		read_data(remote, buf, 0x16, 7);
		return true;
	}


	void read_data(WiiRemote& remote, unsigned char* buffer, unsigned int addr, unsigned short length) {
		//Add it to the queue of requests.
		DataRequest req;
		req.buffer = buffer;
		req.addr = addr;
		req.length = length;
		remote.requests.push_back(req);
	}


	void sendPending(WiiRemote& remote) {
		//Send out pending data?
		if (!remote.waitingOnData && !remote.requests.empty()) {
			//Get the next request.
			DataRequest req = remote.requests.front();
			remote.requests.pop_front();
			remote.waitingOnData = true;

			//Prepare a message.
			unsigned char buf[6] = {0};

			//Big-Endian offset:
			*(unsigned int*)(buf) = htonl(req.addr);

			//Length is Big-Endian:
			*(unsigned short*)(buf + 4) = htons(req.length);

			std::cout <<"Requesting a read at address: " <<req.addr <<" of size: " <<req.length <<"\n";
			send_to_wii_remote(remote, 0x17, buf, 6);
		}
	}

	void send_status_request(WiiRemote& remote) {
		unsigned char buff = 0;
		send_to_wii_remote(remote, 0x15, &buff, 1);
	}

	void send_leds(WiiRemote& remote, bool L1, bool L2, bool L3, bool L4) {
		unsigned char buff = 0;
		if (L1) {
			buff |= 0x10;
		}
		if (L2) {
			buff |= 0x20;
		}
		if (L3) {
			buff |= 0x40;
		}
		if (L4) {
			buff |= 0x80;
		}

		send_to_wii_remote(remote, 0x11, &buff, 1);
	}

	void send_data_reporting(WiiRemote& remote, bool continuous, unsigned char mode) {
		unsigned char buff[] = {0,0};
		if (continuous) {
			buff[0] |= 0x4;
		}
		buff[1] = mode;

		send_to_wii_remote(remote, 0x12, buff, 2);
	}

	//Dispatch any pending activity to handle_event()
	void poll();



    void pushKeys(const WiiTransGamepad& oldGamepad) {
    	//Check each key for keydown/up
    	check_keyupdown(oldGamepad.btnOk, currGamepad.btnOk, 0);
    	check_keyupdown(oldGamepad.btnCancel, currGamepad.btnCancel, 1);
    	check_keyupdown(oldGamepad.btnAux, currGamepad.btnAux, 2);
    	check_keyupdown(oldGamepad.btnMenu, currGamepad.btnMenu, 3);

    	//Hats are slightly different in SDL.
    	check_hatupdown(currGamepad.dpadLeft, currGamepad.dpadRight, currGamepad.dpadUp, currGamepad.dpadDown, 0);

    	

    }

    void check_hatupdown(bool pressLeft, bool pressRight, bool pressUp, bool pressDown, uint8_t hatId) {
    	//Hats are weird. (Note: This approach may lead to bogus values (0xF), but mkxp can probably handle it.)
    	HatState newHat;
    	if (pressLeft) {
    		newHat.left = true;
    	}
    	if (pressRight) {
    		newHat.right = true;
    	}
    	if (pressUp) {
    		newHat.up = true;
    	}
    	if (pressDown) {
    		newHat.down = true;
    	}

    	//Don't spam hat events.
    	if (newHat == lastHat) {
    		return;
    	}

    	//Updated timestamp.
    	currTimestamp += 1;

    	//Now send only the diff
    	if (lastHat.left != newHat.left) {
    		send_key(MyKey_Left, newHat.left);
    	}
    	if (lastHat.right != newHat.right) {
    		send_key(MyKey_Right, newHat.right);
    	}
    	if (lastHat.up != newHat.up) {
    		send_key(MyKey_Up, newHat.up);
    	}
    	if (lastHat.down != newHat.down) {
    		send_key(MyKey_Down, newHat.down);
    	}

    	//And save the new state
    	lastHat = newHat;

    	//Create the base event.
    	//TODO
    	//std::cout <<"HAT event: " <<currTimestamp << " hatId: " <<hatId <<", value: " <<value <<std::endl;
    	//SDL_Event event;

		//Make a new Joystick event.
		//event.jhat.timestamp = currTimestamp;
		//event.jhat.type = SDL_JOYHATMOTION;
		//event.jhat.which = 0; //Joystick ID; probably needs to be 0 for mkxp?
		//event.jhat.hat = hatId;
		//event.jhat.value = value;

    	//Try to send it.
   		/*if (SDL_PushEvent(&event) != 1) {
   			std::cout <<"Error pushing event: " <<SDL_GetError() <<"\n";
   			running = false;
   			return;
   		}*/
    }

    //Actually send the key
    void send_key(unsigned int keycode, bool isPressed);

    void check_keyupdown(bool prevValue, bool currValue, uint8_t buttonId) {
    	//Only update on change.
    	if (prevValue == currValue) {
    		return;
    	}

    	//Updated timestamp.
    	currTimestamp += 1;

    	//Create the base event.
    	//TODO
    	//std::cout <<"BTN event: " <<currTimestamp << " button: " <<buttonId <<", value: " <<(currValue?"Press":"Release") <<std::endl;
    	//SDL_Event event;

    	//Send it to the foreground Window.
    	//buttonId == 0, 1, 3 == (ok, cancel, menu) == (T,R,E)
   		unsigned int keycode = 0;
   		if (buttonId == 0) {
   			keycode = MyKey_T;
   		} else if (buttonId == 1) {
   			keycode = MyKey_R;
   		} else if (buttonId == 2) {
   			keycode = MyKey_F;
   		} else if (buttonId == 3) {
   			keycode = MyKey_E;
   		}
   		if (keycode != 0) {
   			send_key(keycode, currValue);
   		}



		//Make a new Joystick event.
		//event.jbutton.timestamp = currTimestamp;
		//event.jbutton.type = currValue ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
		//event.jbutton.which = 0; //Joystick ID; probably needs to be 0 for mkxp?
		//event.jbutton.button = buttonId;
		//event.jbutton.state = currValue ? SDL_PRESSED : SDL_RELEASED;

    	//Try to send it.
   		/*if (SDL_PushEvent(&event) != 1) {
   			std::cout <<"Error pushing event: " <<SDL_GetError() <<"\n";
   			running = false;
   			return;
   		}*/
    }


	//There are lots of different event types.
	////////////////////////
	//TODO: We should check that we actually read the correct amount of data.
	////////////////////////
	void handle_event(WiiRemote& remote, unsigned char eventType, unsigned char* message) {
//std::cout <<"From remote: " <<remote.bdaddr_str <<" got event: " <<std::hex <<int(eventType) <<"\n";

		switch (eventType) {
			//Status response
			case 0x20: {
				//Always process buttons.
				proccess_buttons(remote, message);

				//Now read the status information.
				process_status_response(remote, message+2);
				break;
			}


			//Memory/Register data reporting.
			case 0x21: {
				//We must process button data here, as regular input reporting is suspended during an EEPROM read.
				proccess_buttons(remote, message);

				//Now handle the rest of the data.
				process_read_data(remote, message+2);

				//Finally, re-send data reporting mode (some 3rd-party remotes can flake out here)
				send_data_reporting(remote, true, 0x35);
				break;
			}

			//Error
			case 0x22: {
				//The host is telling us something is wrong.
				std::cout <<"Manual 0x22 error (" <<int(message[3]) << ") on report: " <<int(message[2]) <<"\n";
				if (int(message[3]) != 0) {
				  //Seems like "ok" is sometimes sent?????
				  running = false;
				  return;
				}
				break;
			}


			//Basic buttons only.
			case 0x30: {
				proccess_buttons(remote, message);
				break;
			}

			//Basic buttons plus accelerometer
			//NOTE: The cheap Wii Remotes can drop into data reporting mode 0x31 for some reason; we should react by nudging them back to 0x35.
			case 0x31: {
				proccess_buttons(remote, message);
				proccess_accellerometer(remote, message+2);
				break;
			}

			//Basic buttons plus 19 extension bytes.
			/*case 0x34: {
				proccess_buttons(remote, message);
				//proccess_accellerometer(remote, message+2);
				break;
			}*/

			//Basic buttons plus accelerometer plus 16 extension bytes.
			case 0x35: {
				proccess_buttons(remote, message);
				proccess_accellerometer(remote, message+2);
				break;
			}

			//Unknown: might as well crash.
			default: {
				std::cout <<"ERROR: Unknown Wii Remote event: " <<((int)eventType) <<"\n";
				running = false;
				return;
			}
		}

		//Finally, send a status request if we've waited a second.
		remote.lastStatus += 1;
		if (remote.lastStatus >= 100) {
			send_status_request(remote);
		}
	}


	void proccess_buttons(WiiRemote& remote, unsigned char* btns) {
		//First byte.
		remote.currButtons.dpadLeft  = (btns[0]&0x01);
		remote.currButtons.dpadRight = (btns[0]&0x02);
		remote.currButtons.dpadDown  = (btns[0]&0x04);
		remote.currButtons.dpadUp    = (btns[0]&0x08);
		remote.currButtons.plus      = (btns[0]&0x10);

		//Second byte
		remote.currButtons.two   = (btns[1]&0x01);
		remote.currButtons.one   = (btns[1]&0x02);
		remote.currButtons.btnB  = (btns[1]&0x04);
		remote.currButtons.btnA  = (btns[1]&0x08);
		remote.currButtons.minus = (btns[1]&0x10);
		remote.currButtons.home  = (btns[1]&0x80);

		//Plus resets the controller affinity.
		if (remote.currButtons.plus && currAffinity >= 0) {
			//Reset LEDs.
			for (std::shared_ptr<WiiRemote>& rm : remotes) {
				send_leds(*rm, true, true, true, true);
			}
			currAffinity = -1;
			currGamepad = WiiTransGamepad();
			return;
		}

		//Check affinity.
		if (currAffinity == -1) {
			//Always reset on afffinity -1
			currGamepad = WiiTransGamepad();

			//Additionally, see if we can break out of affinity -1
			if (remote.currButtons.btnA || remote.currButtons.btnB) {
				currAffinity = remote.id;
			} else if (remote.currButtons.dpadLeft || remote.currButtons.dpadRight || remote.currButtons.dpadUp || remote.currButtons.dpadDown) {
				currAffinity = 1 - remote.id;
			} else {
				//No affinity = no input
				return;
			}

			//Set LEDs based on affinity.
			for (std::shared_ptr<WiiRemote>& rm : remotes) {
				if (rm->id == currAffinity) {
				  send_leds(*rm, true, false, false, false);
				} else {
				  send_leds(*rm, false, true, false, false);
				}
			}
		}

		//Now add input based on this remote's affinity.
		if (currAffinity == remote.id) {
			currGamepad.btnOk = remote.currButtons.btnA;
			currGamepad.btnCancel = remote.currButtons.btnB;
		} else {
			currGamepad.dpadLeft = remote.currButtons.dpadLeft;
			currGamepad.dpadRight = remote.currButtons.dpadRight;
			currGamepad.dpadUp = remote.currButtons.dpadUp;
			currGamepad.dpadDown = remote.currButtons.dpadDown;
			currGamepad.btnAux = remote.currButtons.btnA;
			currGamepad.btnMenu = remote.currButtons.btnB;
		}

		//TODO: We need to fire off Gamepad button presses here.
		//TODO: ...and we need to make sure we don't lose buttons.
	}

	//Process accelerometer bytes.
	void proccess_accellerometer(WiiRemote& remote, unsigned char* data) {
		//Retrieve.
		unsigned char accX = data[0];
		unsigned char accY = data[1];
		unsigned char accZ = data[2];

		//Do pedometer calculation.
//TODO: algorithm cleanup
	//State variable: Are we on an upward slope (1), downward slope (-1), or in the neutral zone (0)
	static int slope = 0;
	static int safety = 0; //Allows us to build up ticks.

	//State variable: store and average the last 10 values.
	static double bucket_x = 0.0;
	static double bucket_y = 0.0;
	static double bucket_z = 0.0;
	static int curr_bucket = 0;

	//What we consider the middle value, and its range (+/-)
	const double middle = 26.5;
	const double range = 5.0;



	//Add to the current bucket.
	bucket_x += accX - 0x80;
	bucket_y += accY - 0x80;
	bucket_z += accZ - 0x80;
	curr_bucket += 1;

	//Have we filled a bucket?
	if (curr_bucket == 10) {
		//Average
		bucket_x /= 10.0;
		bucket_y /= 10.0;
		bucket_z /= 10.0;

		//Compute the magnitude
		double magnitude = sqrt(bucket_x*bucket_x + bucket_y*bucket_y + bucket_z*bucket_z);

		//TEMP
		//int did_step = 0;

		//Different slope require different actions.
		if (slope == 0) {
			//We start off in the middle, and revert back there during times of boredom. Thus, we need some real intertia to get out of there.
			safety = 0;
			if (magnitude >= middle+range) {
				slope = 1;
			} else if (magnitude <= middle-range) {
				slope = -1;
			}
		} else if (slope == 1) {
			//We are in the high range. Only react if we drop below the high water mark.
			if (magnitude < middle+range) {
				//Are we below the low water mark?
				if (magnitude <= middle-range) {
					//Step!
					currStepCount.fetch_add(1);
					//did_step = 1; //TEMP
					slope = -1;
					safety = 0;
				}

				//We get five "ticks" of safety space within which to switch over to the low water mark.
				safety += 1;
				if (safety >= 5) {
					slope = 0;
				}
			}
		} else if (slope == -1) {
			//We are in the low range. Only react if we drop below the low water mark.
			if (magnitude > middle-range) {
				//Are we above the high water mark?
				if (magnitude >= middle+range) {
					//End-of-step. Doesn't really matter.
					//did_step = 1; //TEMP
					slope = 1;
					safety = 0;
				}

				//We get five "ticks" of safety space within which to switch over to the low water mark.
				safety += 1;
				if (safety >= 5) {
					slope = 0;
				}
			}
		}

		//Nada
		//if (!did_step) { //printf(".\n"); }

		//Reset
		bucket_x = 0;
		bucket_y = 0;
		bucket_z = 0;
		curr_bucket = 0;
	}
//END_TODO
	}


	void process_status_response(WiiRemote& remote, unsigned char* data) {
		//LED state (0xF0)>>4
		//Don't care.

		//Battery is flat.
		bool batteryFlat = (data[0] & 0x01);

		//Extension connected.
		bool extensionConnected = data[0]&0x02;

		//std::cout <<"Remote: " <<remote.bdaddr_str <<" status; extension: " <<extensionConnected <<"\n";

		//Speaker status (0x04) 
		//Don't care.

		//IR camera enabled (0x08) 
		//Don't care.

		//Battery
		int batteryLvl = data[3];

		//Convert/report battery level.
		float batteryStatus = -1;
		if (!batteryFlat) {
			batteryStatus = batteryLvl / 200.0f;
		}


		//Assign it to the correct Wii Remote.
		if (remote.id == currAffinity) {
			batteryStatusRHS = batteryStatus;
		} else if (currAffinity >= 0) {
			batteryStatusLHS = batteryStatus;
		}

		//Note it.
		remote.lastStatus = 0;
	}


	void process_read_data(WiiRemote& remote, unsigned char* btns) {
		//Get the error state
		int err = btns[0]&0xF;
		if (err != 0) {
			std::cout <<"Error while reading from EEPROM: " <<err <<"\n";
			running = false;
			return;
		}

		//Get the size of the data read.
		unsigned short size = ((btns[0]&0xF0)>>4) + 1;

		//We are not currently robust for segmented messages.
		if (size >= 16) {
			std::cout <<"Error: this library doesn't support segmented data reading\n";
			running = false;
			return;
		}

		//Get the memory offset being read.
		unsigned short memOffset = ntohs(*(uint16_t*)(btns+1));

		//Now dispatch based on the handshake we are currently waiting for.
		if (!remote.handshake_calib) {
			finish_handshake_calib(remote, memOffset, size, btns+3);
			remote.handshake_calib = true;
		} else {
			std::cout <<"Error: received data when not waiting for a handshake.\n";
			running = false;
			return;
		}

		//Either way, we're done.
		remote.waitingOnData = false;
	}


	void finish_handshake_calib(WiiRemote& remote, unsigned short memOffset, unsigned short size, unsigned char* memory) {
		//Sanity check.
		if (memOffset != 0x16) {
			std::cout <<"Error: handshake calibration on unexpected memory address: " <<memOffset <<"\n";
			running = false;
			return;
		}

		//Sanity check 2.
		if (size != 7) {
			std::cout <<"Error: handshake calibration on unexpected size: " <<size <<"\n";
			running = false;
			return;
		}

		//Seems like we're good; read the calibration data.
		//TODO: We don't actually care about the calibration data.
		std::cout <<"Initial (calibration) handshake done.\n";


		//At some point, we need to tell SDL about this joystick.
		if (!sdlKnowsJoystick) {
		  sdlKnowsJoystick = true;

		  //Update timestamp
		  currTimestamp += 1;

    	  /*SDL_Event event;
   		  event.jdevice.timestamp = currTimestamp;
   		  event.jdevice.type = SDL_JOYDEVICEADDED;
   		  event.jdevice.which = 0; //Joystick ID; probably needs to be 0 for mkxp?

    	  //Try to send it.
   		  if (SDL_PushEvent(&event) != 1) {
   			  std::cout <<"Error pushing event: " <<SDL_GetError() <<"\n";
   			  running = false;
   			  return;
   		  }*/

   		  std::cout <<"Wii Joystick connected to system.\n";
   		}
	}



	//Construct a message and send it to the remote socket.
	size_t send_to_wii_remote(WiiRemote& remote, unsigned char reportType, unsigned char* message, int length);


    //Disconnect all Wii Remotes
	void disconnect();

private:
	//The current buttons, translated to "Gamepad" functionality.
	WiiTransGamepad currGamepad;

	//Current controller affinity
	//  -1 = unknown (4 LEDs)
	//   0 = first remote is RHS  (Accept/Cancel)
	//   1 = second remote is RHS (Accept/Cancel)
	int currAffinity;

	//Current step count (since last check).
	std::atomic<int> currStepCount;

	//Battery status. (-1 = flat, otherwise 0.0 to 1.0)
	std::atomic<float> batteryStatusRHS;
	std::atomic<float> batteryStatusLHS;

	//Does SDL know about this joystick?
	bool sdlKnowsJoystick;

	//Last hat value sent (kind of a hack)
	HatState lastHat;

	//Holds the main loop
	std::thread main_thread;

	//Holds our set of Wii Remotes.
	std::vector<std::shared_ptr<WiiRemote>> remotes;

	//Used to flag the main loop to stop.
	std::atomic<bool> running;

#define POWER_WORD_CLASSDEF_WII_MGR
	#include "platform.h"
#undef POWER_WORD_CLASSDEF_WII_MGR
};


#define POWER_WORD_IMPLEMENTL
	#include "platform.h"
#undef POWER_WORD_IMPLEMENTL



#endif // WII_REMOTE_H