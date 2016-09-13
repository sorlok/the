//This file is weird.

//Includes for this platform
#ifdef POWER_WORD_INCLUDE
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

//Also define relevant keycodes here
#define MyKey_Left XK_Left
#define MyKey_Right XK_Right
#define MyKey_Up XK_Up
#define MyKey_Down XK_Down
#define MyKey_T XK_T
#define MyKey_R XK_R
#define MyKey_E XK_E
#define MyKey_F XK_F
#endif //POWER_WORD_INCLUDE

//Extra members in this class (and some definitions)
#ifdef POWER_WORD_CLASSDEF_WII_REMOTE
	WiiRemote(bdaddr_t bdaddr) : bdaddr(bdaddr), sock(-1), ctrl_sock(-1), handshake_calib(false), waitingOnData(false), id(-1), lastStatus(0) {}
	bdaddr_t bdaddr;  //Bluetooth address; i.e.,: 2C:10:C1:C2:D6:5E
	std::string bdaddr_str; //String version of the above
	int sock;         //Data channel ("in_socket")
	int ctrl_sock;    //Control channel ("out_socket") (needed?)
#endif //POWER_WORD_CLASSDEF_WII_REMOTE

//Extra members in the class (and some functions)
#ifdef POWER_WORD_CLASSDEF_WII_MGR
	//Main display.
	Display* display;
#endif //POWER_WORD_CLASSDEF_WII_MGR

//Actual implementation of functions
#ifdef POWER_WORD_IMPLEMENTL
	WiiRemoteMgr::WiiRemoteMgr() : currAffinity(-1), currStepCount(0), batteryStatusRHS(0), batteryStatusLHS(0), sdlKnowsJoystick(false), running(true), display(nullptr) {
		//Start up the main thread.
		main_thread = std::thread(&WiiRemoteMgr::run_main, this);
	}

	bool WiiRemoteMgr::findWindow() {
		display = XOpenDisplay(NULL);

		std::cout <<"Opening main display as: " <<display <<"\n";
		return true;
	}

	bool WiiRemoteMgr::scan_remotes() {
		//Get the address of the first Bluetooth adapter.
		//bdaddr_t my_addr;
		//int device_id = hci_get_route(&my_addr);
		//if (device_id < 0) { throw "BAD"; }

		//The above code won't work for some reason, so we hard-code my adapter.
		//bdaddr_t my_addr;
		//str2ba("5C:F3:70:61:BF:DE", &my_addr);
		int device_id = 0; //Note: I think this actually picks the right ID.
		int device_sock = hci_open_dev(device_id);
		if (device_sock < 0) {
			std::cout <<"Error: couldn't find a Bluetooth adapter.\n";
			return false;
		}

		//Set up an inquiry for X devices.
		const size_t max_infos = 128;
		inquiry_info info[max_infos];
		inquiry_info* info_p = &info[0];
		memset(&info[0], 0, max_infos*sizeof(inquiry_info));

		//Make an inquiry for X devices
		const int timeout = 5; //5*1.28 seconds
		int num_devices = hci_inquiry(device_id, timeout, max_infos, NULL, &info_p, IREQ_CACHE_FLUSH);
		std::cout <<"Found " <<num_devices <<" candidate devices\n";

		//Now check if these are actually Wii Remotes.
		for (int i=0; i<num_devices; ++i) {
			//Identify false positives.
			std::cout <<"Found: " <<int(info[i].dev_class[0]) <<" : " <<int(info[i].dev_class[1]) <<" : " <<int(info[i].dev_class[2]) <<"\n";

			//Check the device IDs.
			bool pass1 = (info[i].dev_class[0] == 0x08) && (info[i].dev_class[1] == 0x05) && (info[i].dev_class[2] == 0x00); //Old Wii Remote/Plus
			bool pass2 = (info[i].dev_class[0] == 0x04) && (info[i].dev_class[1] == 0x25) && (info[i].dev_class[2] == 0x00); //New Wii Remote/Plus
			if (pass1 || pass2) {
				//Found
				std::shared_ptr<WiiRemote> remote(new WiiRemote(info[i].bdaddr));
				remote->id = remotes.size();
				remotes.push_back(remote);

				char bdaddr_str[18]; //TODO: Make safer.
				ba2str(&remote->bdaddr, bdaddr_str);
				remote->bdaddr_str = bdaddr_str;
				std::cout <<"Found Wii remote: " <<remote->bdaddr_str <<"\n";
			}
		}

		//We are done with the socket, even if we don't have enough remotes.
		::close(device_sock);

		//Enough remotes?
		if (remotes.size() != 2) {
			std::cout <<"Error: Expected 2 Wii Remotes; found " <<remotes.size() <<"\n";
			return false;
		}

		//Good.
		return true;
	}

	bool WiiRemoteMgr::btconnect(WiiRemote& remote) {
		std::cout <<"Connecting to a Wii Remote.\n";

		//Prepare a new L2cap connection.
		sockaddr_l2 addr;
		memset(&addr, 0, sizeof(addr));

		//Set up Bluetooth address and family.
		addr.l2_bdaddr = remote.bdaddr;
		addr.l2_family = AF_BLUETOOTH;

		//We only need what's considered the "input" channel (the data channel)
		remote.sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
		if (remote.sock == -1) {
			std::cout <<"Error: could not create input socket.\n";
			return false;
		}

		//NOTE: It seems like we need the control channel after all? (Answer: yes)
		remote.ctrl_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
		if (remote.ctrl_sock == -1) {
			std::cerr <<"Error: ABC\n";
			return false;
		}
		addr.l2_psm = htobs(0x11);
		if (::connect(remote.ctrl_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
			std::cerr <<"Error: DEF\n";
			return false;
		}
		//END NOTE

		//Connect on the correct magic number.
		addr.l2_psm = htobs(0x13);

		//Actually connect.
		if (::connect(remote.sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
			std::cout <<"Error: connect() interrupt socket\n";
			return false;
		}

		//Success.
		std::cout <<"Initial connection to Wii Remote succeeded.\n";
		return true;
	}


	void WiiRemoteMgr::poll() {
		//select() should block for at most 1/2000s
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 500;

		//Create a set of file descriptors to check for.
		fd_set fds;
		FD_ZERO(&fds);

		//Find the highest FD to check. select() will scan from 0..max_fd-1
		int max_fd = -1;
		for (std::shared_ptr<WiiRemote>& remote : remotes) {
			FD_SET(remote->sock, &fds);
			if (remote->sock > max_fd) {
				max_fd = remote->sock;
			}
		}

		//Nothing to poll?
		if (max_fd == -1) {
			return;
		}

		//select() and check for errors.
		if (select(max_fd+1, &fds, NULL, NULL, &tv) < 0) {
			std::cout <<"Error: Unable to select() the wiimote interrupt socket(s).\n";
			running = false;
			return ;
		}

		//Check each socket for an event.
		for (std::shared_ptr<WiiRemote>& remote : remotes) {
			if (FD_ISSET(remote->sock, &fds)) {
				//Read the pending message.
				memset(remote->event_buf, 0, MAX_EVENT_LEGNTH);
				int r = ::read(remote->sock, remote->event_buf, MAX_EVENT_LEGNTH);

				//Error reading?
				if (r < 0) {
					std::cout <<"Error receiving Wii Remote data\n";
					running = false;
					return;
				}

				//If select() returned true, but read() returns nothing, the remote was disconnected.
				if (r == 0) {
					std::cout <<"Wii Remote disconnected.\n";
					running = false;
					return;
				}

				/*
				std::cout <<"READ: " <<r <<" : ";
				for (int i=0; i<r; i++) {
					printf(",0x%x", remote->event_buf[i]);
				}
				std::cout <<"\n";
				*/

				//Now deal with it.
				handle_event(*remote, remote->event_buf[1], remote->event_buf+2);
			}
		}
	}

    void WiiRemoteMgr::send_key(unsigned int keycode, bool isPressed) {
       if (display != nullptr) {
   			keycode = XKeysymToKeycode(display, keycode);
   			if (keycode != 0) {
   				if (XTestFakeKeyEvent(display, keycode, (isPressed?True:False), 0) == True) {
   					XFlush(display);
   				} else {
   					std::cout <<"ERROR: Display doesn't support fake key presses.\n";
   				}
   			}
   		}
    }


	size_t WiiRemoteMgr::send_to_wii_remote(WiiRemote& remote, unsigned char reportType, unsigned char* message, int length) {
		//Prepare a new-style message (the old method won't work on new remotes.)
		unsigned char buffer[32];
		buffer[0] = 0xa2;
		buffer[1] = reportType;
		memcpy(buffer+2, message, length);


//std::cout <<"SEND to remote: " <<remote.bdaddr_str <<" data: [";

		//TEMP
/*		std::cout <<"sending: [";
		for (size_t i=0; i<length+2; i++) {
			if (i!=0) {std::cout <<",";}
			printf("0x%x", buffer[i]);
		}
		std::cout <<"]\n";*/
		//END_TEMP

		//Make sure to disable rumble (really!)
		buffer[2] &= 0xFE;
		return ::write(remote.sock, buffer, length+2);
	}


	void WiiRemoteMgr::disconnect() {
		std::cout <<"Disconnecting all Wii Remotes.\n";
		for (std::shared_ptr<WiiRemote>& remote : remotes) {
			if (remote->sock != -1) {
				close(remote->sock);
				remote->sock = -1;
			}
			if (remote->ctrl_sock != -1) {
				close(remote->ctrl_sock);
				remote->ctrl_sock = -1;
			}
		}
		remotes.clear();
		std::cout <<"All Wii Remotes disconnected.\n";
	}

#endif //POWER_WORD_IMPLEMENTL