//This file is weird.

//Includes for this platform
#ifdef POWER_WORD_INCLUDE
#include <Windows.h>
#include <WinBase.h>
//#include <Winsock2.h> //htonl (TODO: does it work?)
//#include <Winuser.h> //Scancodes
//#include <BaseTsd.h> //Handle
#include <hidsdi.h>
#include <setupapi.h>
#include <sstream>
#include <string>

//Also define relevant keycodes here
/*
#define MyKey_Left VK_LEFT
#define MyKey_Right VK_RIGHT
#define MyKey_Up VK_UP
#define MyKey_Down VK_DOWN
#define MyKey_T 0x54
#define MyKey_R 0x52
#define MyKey_E 0x45
#define MyKey_F 0x46
*/

//Defines for RPG Maker
#define MyKey_Left  0x48 //H
#define MyKey_Right 0x4C //L
#define MyKey_Up    0x4B //K
#define MyKey_Down  0x4A //J
#define MyKey_F 0x46 //N/A
#define MyKey_T 0x5A //Z
#define MyKey_E 0x58 //X
#define MyKey_R 0x58 //X

// 128 bit GUID to human-readable string 
std::string guid_to_str(const GUID& id) {
	std::stringstream res;
	char buff[64];
	int i;
	sprintf(buff, "%.8lX-%.4hX-%.4hX-", id.Data1, id.Data2, id.Data3);
	res << buff;
	for (i = 0; i < sizeof(id.Data4); ++i) {
		sprintf(buff, "%.2hhX", id.Data4[i]);
		res << buff;
		if (i == 1) {
			res << "-";
		}
	}
	return res.str();
}
#endif //POWER_WORD_INCLUDE

//Extra members in this class (and some definitions)
#ifdef POWER_WORD_CLASSDEF_WII_REMOTE
	WiiRemote(HANDLE handle) : handle(handle), handshake_calib(false), waitingOnData(false), id(-1), lastStatus(0) {}
	HANDLE handle; //Handle to the device
#endif //POWER_WORD_CLASSDEF_WII_REMOTE

//Extra members in the class (and some functions)
#ifdef POWER_WORD_CLASSDEF_WII_MGR
#endif //POWER_WORD_CLASSDEF_WII_MGR

//Actual implementation of functions
#ifdef POWER_WORD_IMPLEMENTL
	WiiRemoteMgr::WiiRemoteMgr() : currAffinity(-1), currStepCount(0), batteryStatusRHS(0), batteryStatusLHS(0), sdlKnowsJoystick(false), 
	  run_0(true), run_1(true), leds_0(0), leds_1(0)
	{
		//Initialize all connections.
		//main_thread = std::thread(&WiiRemoteMgr::run_main, this);
		run_main_init();

		//Start all threads.
		if (remotes.size() == 2) {
			main_thread_0 = std::thread(&WiiRemoteMgr::run_main, this, 0);
			main_thread_1 = std::thread(&WiiRemoteMgr::run_main, this, 1);
		}
	}

	//Shouldn't matter; we always have access to SendInput
	bool WiiRemoteMgr::findWindow() { return true; }

	//TODO:
	bool WiiRemoteMgr::scan_remotes() {
		//Get the HID device GUID
		GUID device_id;
		HidD_GetHidGuid(&device_id);
		std::cout << "GUID: " << guid_to_str(device_id) <<"\n";

		//Get all HID devices currently plugged in.
		HDEVINFO device_info = SetupDiGetClassDevs(&device_id, NULL, NULL, (DIGCF_DEVICEINTERFACE | DIGCF_PRESENT));
		for (int index=0;;index++) {
			SP_DEVICE_INTERFACE_DATA device_data;
			device_data.cbSize = sizeof(device_data);
			if (SetupDiEnumDeviceInterfaces(device_info, NULL, &device_id, index, &device_data) == TRUE) {
				//Get some details; we need at least the size. This call always "FAILS", because Windows likes that.
				DWORD reqSize;
				SetupDiGetDeviceInterfaceDetail(device_info, &device_data, NULL, 0, &reqSize, NULL);

				//Allocate a device data structure.
				SP_DEVICE_INTERFACE_DETAIL_DATA* detail_data = (SP_DEVICE_INTERFACE_DETAIL_DATA*)malloc(reqSize);
				detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

				//Query this device.
				if (SetupDiGetDeviceInterfaceDetail(device_info, &device_data, detail_data, reqSize, NULL, NULL) == TRUE) {
					//Open a handle to the device.
					HANDLE dev = CreateFile(detail_data->DevicePath,
						(GENERIC_READ | GENERIC_WRITE),
						(FILE_SHARE_READ | FILE_SHARE_WRITE),
						NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
					if (dev != INVALID_HANDLE_VALUE) {
						//Get device attributes
						HIDD_ATTRIBUTES attr;
						attr.Size = sizeof(attr);
						if (HidD_GetAttributes(dev, &attr) == TRUE) {
							std::cout << "Found, vendor: " << attr.VendorID << "; product: " << attr.ProductID << "\n";
							bool pass1 = (attr.VendorID == 0x057E) && ((attr.ProductID== 0x0306) || (attr.ProductID == 0x0330));
							bool pass2 = false; //TODO
							if (pass1 || pass2) {
								std::cout << "  WIIMOTE found\n";
								std::shared_ptr<WiiRemote> remote(new WiiRemote(dev));
								remote->id = remotes.size();
								remotes.push_back(remote);
							} else {
								//Always clean up
								CloseHandle(dev);
							}
						} else {
							//Always clean up
							CloseHandle(dev);
						}
					}
				}

				//Always clean up.
				free(detail_data);
			} else {
				//No more devices.
				SetupDiDestroyDeviceInfoList(device_info);
				break;
			}
		}


		//Enough remotes?
		if (remotes.size() != 2) {
			std::cout <<"Error: Expected 2 Wii Remotes; found " <<remotes.size() <<"\n";
			return false;
		}

		//Good.
		return true;
	}

	//Because of the heavy-handed way that Windows handles bluetooth devices, we actually have everything we need already, so this is a no-op.
	bool WiiRemoteMgr::btconnect(WiiRemote& remote) { return true; }

	void WiiRemoteMgr::poll(WiiRemote& remote, WiiTransGamepad& currGamepad, std::atomic<bool>& running) {
		//for (std::shared_ptr<WiiRemote>& remote : remotes) {
			OVERLAPPED hid_overlap_read = OVERLAPPED();
			hid_overlap_read.hEvent = CreateEvent(nullptr, true, false, nullptr);

			unsigned char buff[32]; //32 is about as big as you can get.
			buff[0] = 0xa1; //Data reporting indicator.
			buff[1] = 0; //??

			DWORD bytes = 0;
			ResetEvent(hid_overlap_read.hEvent);
			if (ReadFile(remote.handle, buff + 1, 32 - 1, &bytes, &hid_overlap_read) != TRUE)
			{
				if (GetLastError() == ERROR_IO_PENDING)
				{
					//Actual overlap error.
					if (GetOverlappedResult(remote.handle, &hid_overlap_read, &bytes, TRUE) != TRUE)
					{
						if (GetLastError() == ERROR_OPERATION_ABORTED)
						{
							std::cout << "ERROR: Overlapped operation aborted.\n";
						}
						else
						{
							std::cout << "ERROR: Overlapped problem: " <<GetLastError() <<"\n";
						}
						return;
					}

					//Request may still be pending.
					if (hid_overlap_read.Internal == STATUS_PENDING)
					{
						std::cout << "ERROR: Request was pending...\n";
						CancelIo(remote.handle);
						return;
					}
				} 
				else
				{
					std::cout << "ERROR: Generic read error: " <<GetLastError() <<"\n";
				}
			}
			
			//How many bytes did we read?
			int len = bytes + 1;
			//std::cout << "Read " << len << "bytes\n";

			//What kind of message was it?
			handle_event(remote, currGamepad, buff[1], buff + 2, running);
		//}
	}

	//TODO:
	void WiiRemoteMgr::send_key(unsigned int keycode, bool isPressed) {
//std::cout <<"SEND: " <<keycode <<" : " <<isPressed <<"\n";

		INPUT input;
		KEYBDINPUT kInput;
		kInput.wVk = keycode;
		kInput.dwFlags  = isPressed ? 0 : KEYEVENTF_KEYUP;
		kInput.time = 0;
		kInput.dwExtraInfo = 0;
		input.type = INPUT_KEYBOARD;
		input.ki = kInput;
		//std::cout <<"Sending: " <<keycode <<"\n";
		SendInput(1,&input,sizeof(INPUT));
	}

    //Helper: from Dolphin code
    void IOWritePerWriteFile(WiiRemote& remote, OVERLAPPED& hid_overlap_write, unsigned char* buffer, int length, DWORD* written) {
		DWORD bytes_written = 0;
		LPCVOID write_buffer = buffer + 1;
		DWORD bytes_to_write = (DWORD)(length - 1);
		*written = 0;

		//Write it.
		ResetEvent(hid_overlap_write.hEvent);
		bool res = WriteFile(remote.handle, write_buffer, bytes_to_write, &bytes_written, &hid_overlap_write) == TRUE;
		if (!res && GetLastError() != ERROR_IO_PENDING) {
			std::cout << "ERROR writing: " << GetLastError() << "\n";
			return;
		}

		//Wait for it.
		DWORD wres = WaitForSingleObject(hid_overlap_write.hEvent, 1000); //1s max
		if (wres == WAIT_TIMEOUT) {
			std::cout << "ERROR; timeout on send_message\n";
			CancelIo(remote.handle);
			return;
		}
		else if (wres == WAIT_FAILED) {
			std::cout << "ERROR; failed on send_message\n";
			CancelIo(remote.handle);
			return;
		}

		//Check again.
		if (GetOverlappedResult(remote.handle, &hid_overlap_write, written, TRUE) != TRUE) {
			std::cout << "ERROR; get_overlapped failed\n";
			return;
		}
    }

	size_t WiiRemoteMgr::send_to_wii_remote(WiiRemote& remote, unsigned char reportType, unsigned char* message, int length) {
		//Prepare a new-style message (the old method won't work on new remotes.)
		unsigned char buffer[32];
		buffer[0] = 0xa2;
		buffer[1] = reportType;
		memcpy(buffer+2, message, length);

		//Make sure to disable rumble (really!)
		buffer[2] &= 0xFE;

		//Set up a new output overlap event.
		OVERLAPPED hid_overlap_write = OVERLAPPED();
		hid_overlap_write.hEvent = CreateEvent(NULL, true, false, NULL);

		//Write it.
		DWORD written;
		IOWritePerWriteFile(remote, hid_overlap_write, buffer, length + 2, &written);

		//Handle is done.
		CloseHandle(hid_overlap_write.hEvent);

		return written;
	}

	//TODO: I think we can just close all the handles? It might not even matter.
	void WiiRemoteMgr::disconnect() {
	}

#endif //POWER_WORD_IMPLEMENTL