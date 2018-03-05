// xidmon.cpp: monitor XInput2 device hotplug and report on stdout.
// Copyright (C) 2018 Richard Tollerton.
// License ArtisticLicense2.0 <http://www.perlfoundation.org/attachment/legal/artistic-2_0.txt>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

class App {
public:
	App();
	~App();
	void read_event();
	void reset_timeout();
	int timed_out();
	void wait();
	int pending();
protected:
	void check_xinput();
	void setup_filter();
	void process_hierarchychanged(void* data);
private:
	Display *m_dpy;
	int m_opcode;

	// We select() directly on the X fd in order to do a cheap timeout
	int m_x11fd;
	fd_set m_infds;
	struct timeval m_timeout;
};

class ConnectionFailureException {};
class ReadFailureException {};
class UnexpectedException {};

App::App() {
        m_dpy = XOpenDisplay(NULL);
	if (!m_dpy) {
		fprintf(stderr, "Unable to connect to X display. "
			"Is DISPLAY set?\n");
		throw ConnectionFailureException();
	}
	m_x11fd = ConnectionNumber(m_dpy);
	FD_ZERO(&m_infds);
	FD_SET(m_x11fd, &m_infds);

	check_xinput();
	setup_filter();
}

App::~App() {
	XCloseDisplay(m_dpy);
}

void App::check_xinput() {
	int ev, error;
	if (!XQueryExtension(m_dpy, "XInputExtension", &m_opcode, &ev,
			&error))
	{
		fprintf(stderr, "X Input extension not available.\n");
		throw ConnectionFailureException();
	}
	int major = 2, minor = 0;
	if (XIQueryVersion(m_dpy, &major, &minor) == BadRequest) {
		fprintf(stderr, "XInputExtension: requested version 2.0, "
			"but only %d.%d is available.\n",
			major, minor);
		throw ConnectionFailureException();
	}
}

void App::setup_filter() {
	XIEventMask eventmask;
	unsigned char mask[2]={0, 0};
	eventmask.deviceid=XIAllDevices;
	eventmask.mask_len=sizeof(mask);
	eventmask.mask=mask;
	XISetMask(mask, XI_HierarchyChanged);
	XISelectEvents(m_dpy, DefaultRootWindow(m_dpy), &eventmask, 1);
}

static const char* xi_use_str(int use) {
	switch (use) {
	case XIMasterPointer: return "XIMasterPointer";
	case XIMasterKeyboard: return "XIMasterKeyboard";
	case XISlavePointer: return "XISlavePointer";
	case XISlaveKeyboard: return "XISlaveKeyboard";
	case XIFloatingSlave: return "XIFloatingSlave";
	}
        return "UnknownXIDeviceType";
}

static const char* xi_hierflags_str(int flags) {
	switch (flags) {
	case XIMasterAdded: return "XIMasterAdded";
	case XIMasterRemoved: return "XIMasterRemoved";
	case XISlaveAdded: return "XISlaveAdded";
	case XISlaveRemoved: return "XISlaveRemoved";
	case XISlaveAttached: return "XISlaveAttached";
	case XISlaveDetached: return "XISlaveDetached";
	case XIDeviceEnabled: return "XIDeviceEnabled";
	case XIDeviceDisabled: return "XIDeviceDisabled";
	}
	return "UnknownXIHierarchyFlag";
}

void App::process_hierarchychanged(void* data) {
	XIHierarchyEvent *e = (XIHierarchyEvent *)data;
	for (int i=0; i<e->num_info; i++) {
		XIHierarchyInfo &hi=e->info[i];
		// XISlaveRemoved is too late to get use field
		int filter_flags = (hi.flags & (XIDeviceEnabled|XIDeviceDisabled));
		if (!filter_flags) continue;
		// XIQueryDevice will be too late to get a name for the removed
		// device, even for XIDeviceDisabled, so for now, don't bother
		// trying to grab it
		printf("%s\t%s\t", xi_hierflags_str(hi.flags),
			xi_use_str(hi.use));
	}
}

class scoped_cookie_data {
public:
	scoped_cookie_data(Display*, XGenericEventCookie*);
	~scoped_cookie_data();
	void* get();
private:
	Display* m_dpy;
	XGenericEventCookie* m_cookie;
};

scoped_cookie_data::scoped_cookie_data(Display* dpy, XGenericEventCookie* cookie):
	m_dpy(dpy), m_cookie(cookie)
{
	if (!XGetEventData(m_dpy, m_cookie)) {
		// XGetEventData does not hit the server, so its failure cannot
		// be a normal result of a terminated X session
		fprintf(stderr, "XGetEventData failed\n");
		throw UnexpectedException();
	}
}

scoped_cookie_data::~scoped_cookie_data() {
	XFreeEventData(m_dpy, m_cookie);
}

void* scoped_cookie_data::get() {
	return m_cookie->data;
}

void App::read_event() {
	XEvent ev;
	XNextEvent(m_dpy, &ev);
	if (ev.xcookie.type != GenericEvent)
		return;
	if (ev.xcookie.extension != m_opcode)
		return;
	scoped_cookie_data cd(m_dpy, &ev.xcookie);
	switch(ev.xcookie.evtype) {
	case XI_HierarchyChanged:
		process_hierarchychanged(cd.get());
		break;
	}
}

void App::reset_timeout() {
	m_timeout.tv_usec = 250000;
	m_timeout.tv_sec = 0;
}

int App::timed_out() {
	return (m_timeout.tv_usec==0);
}

void App::wait() {
	// Don't reset tv while coalescing. This is deliberate, because we'd
	// like the timeout to be an upper bound for all coalescing, not for it
	// to be a timeout to wait for every additional event. This relies on
	// Linux-specific behavior, because only Linux is believed to ever
	// deduct from timeout in select(), but the balance of this behavior is
	// hopefully minor enough that other platforms won't notice. Yes I'm
	// being lazy
	select(m_x11fd+1, &m_infds, NULL, NULL, &m_timeout);
}

int App::pending() {
	return XPending(m_dpy);
}

static const char* helptext=
"xidmon: wait for device add/remove over XInput2 and print notifications on\n"
"stdout. Each notification is composed of two tab-delimited fields:\n"
"\t<XInput2 hierarchy flag> <XInput2 device type>\n\n"
"Consecutive events are coalesced onto a single line.\n\n"
"Usage: xidmon [-h] [-V]\n\n"
"Sample output:\n\n"
"\tXIDeviceDisabled\tXISlaveKeyboard\tXIDeviceDisabled\tXISlavePointer\n"
	"\tXIDeviceEnabled\tXISlaveKeyboard\tXIDeviceEnabled\tXISlavePointer\n\n";

void help() {
	printf(helptext);
}

void version() {
	printf(
"xidmon 0.0.1\n"
"Copyright (C) 2018 Richard Tollerton.\n"
"License ArtisticLicense2.0\n"
"<http://www.perlfoundation.org/attachment/legal/artistic-2_0.txt>\n"
		);
}

int main(int argc, char* argv[]) {
	if (argc==2) {
		if (!strcmp(argv[1], "-h")) {
			help();
			return 0;
		} else if (!strcmp(argv[1], "-V")) {
			version();
			return 0;
		} else {
			fprintf(stderr, "Unknown option %s\n", argv[1]);
			return 1;
		}
	} else if (argc>2) {
		fprintf(stderr, "Invalid number of arguments\n");
		return 1;
	}
	try {
		App app;
		while (true) {
			app.read_event();
			app.reset_timeout();
			while (!app.timed_out()) {
				app.wait();
				while (app.pending()) {
					app.read_event();
				}
			}
			printf("\n");
			fflush(stdout);
		}
	} catch (ConnectionFailureException& e) {
		return 1;
	} catch (UnexpectedException& e) {
		return 1;
	} catch (ReadFailureException& e) {
		// We assume this is because the X server quit because of a logout, and
		// so this isn't an error
	}
	return 0;
}
