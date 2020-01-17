#ifndef UNICODE
#define UNICODE
#endif 

#include <stdexcept>
#include <vector>
#include <windows.h>
#include "libusb.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// From the HID spec:
static const int HID_GET_REPORT = 0x01;
static const int HID_SET_REPORT = 0x09;
static const int HID_REPORT_TYPE_INPUT = 0x01;
static const int HID_REPORT_TYPE_OUTPUT = 0x02;
static const int HID_REPORT_TYPE_FEATURE = 0x03;

float get_brightness(libusb_device_handle* handle) {
	u_char data[8] = { 0x00 };
	int bytes = libusb_control_transfer(handle,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
		HID_GET_REPORT, (HID_REPORT_TYPE_FEATURE << 8) | 0, 1, data, sizeof(data), 0);
	if (bytes < 0)
		throw std::runtime_error("Unable to get brightness: libusb_control_transfer error.");
	uint16_t brightness_value = data[0] + (data[1] << 8);
	return float(brightness_value) / 54000.f;
}

void set_brightness(libusb_device_handle* handle, float brightness) {
	uint16_t brightness_value = max(min(int(brightness * 54000), 54000), 0);

	u_char data[6] = {
		u_char(brightness_value & 0x00ff),
		u_char((brightness_value >> 8) & 0x00ff), 0x00, 0x00, 0x00, 0x00 };
	int bytes = libusb_control_transfer(handle,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
		HID_SET_REPORT, (HID_REPORT_TYPE_FEATURE << 8) | 0, 1, data, sizeof(data), 0);

	if (bytes < 0)
		throw std::runtime_error("Unable to set brightness: libusb_control_transfer error.");
}

libusb_device_handle* get_lg_ultrafine() {
	const uint16_t vendor_id = 0x43e;
	const uint16_t product_id = 0x9A63; // Use 0x9a40 for 5k

	libusb_device** devices;
	ssize_t count = libusb_get_device_list(NULL, &devices);
	if (count < 0)
		throw std::runtime_error("Unable to get USB device list.");

	for (int i = 0; i < count; i++) {
		struct libusb_device_descriptor descriptor;
		int err = libusb_get_device_descriptor(devices[i], &descriptor);
		if (err) {
			libusb_free_device_list(devices, 1);
			throw std::runtime_error("Failed to get device descriptor");
		}

		if (descriptor.idVendor == vendor_id && descriptor.idProduct == product_id) {
			libusb_device_handle* handle;
			int err = libusb_open(devices[i], &handle);
			libusb_free_device_list(devices, 1);

			if (err)
				throw std::runtime_error("libusb_open failed");

			return handle;
		}
	}

	libusb_free_device_list(devices, 1);
	return NULL;
}

libusb_device_handle* init_lg_ultrafine() {
	libusb_device_handle* handle = get_lg_ultrafine();
	if (handle) {
		libusb_set_auto_detach_kernel_driver(handle, 1);
		int err = libusb_claim_interface(handle, 1);
		if (err)
			throw std::runtime_error("Failed to claim interface 1.");
	}
	return handle;
}

libusb_device_handle* handle = NULL;
float brightness;

void reinit_handle() {
	if (handle != NULL)
		libusb_close(handle);

	try {
		handle = init_lg_ultrafine();
		if (handle != NULL)
			brightness = get_brightness(handle);
	} catch (std::runtime_error err) {
		handle = NULL;
	}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
	int err = libusb_init(NULL);
	if (err)
		throw std::runtime_error("Unable to initialize libusb.");

	reinit_handle();

	// Register the window class.
	const wchar_t CLASS_NAME[] = L"LG Ultrafine Brightness";
	WNDCLASS wc = { };
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	RegisterClass(&wc);

	// Create the window.
	HWND hwnd = CreateWindowEx(
		0,                              // Optional window styles.
		CLASS_NAME,                     // Window class
		L"Controller",    // Window text
		WS_OVERLAPPEDWINDOW,            // Window style

		// Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

		NULL,       // Parent window    
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);
	if (hwnd == NULL)
		return 0;

	//ShowWindow(hwnd, nCmdShow);

	// Run the message loop.
	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	libusb_exit(NULL);
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case 1024:
		if (handle) {
			try {
				brightness -= 0.01f;
				set_brightness(handle, brightness);
			} catch (std::runtime_error err) {
				reinit_handle();
			}
		}
		return 0;

	case 1025:
		if (handle) {
			try {
				brightness += 0.01f;
				set_brightness(handle, brightness);
			} catch (std::runtime_error err) {
				reinit_handle();
			}
		}
		return 0;

	case WM_DEVICECHANGE:
		reinit_handle();
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
