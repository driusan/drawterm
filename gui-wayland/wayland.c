#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "linux/input-event-codes.h"

#undef _XOPEN_SOURCE


#include "u.h"
#include "lib.h"
#include "dat.h"
#include "fns.h"
// #include "error.h"

#include <draw.h>
#include <memdraw.h>
#include <keyboard.h>
// #include <cursor.h>
#include "screen.h"

// Wayland samplecode stuff
#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>


static void *shm_data = nil;

enum pointer_event_mask {
       POINTER_EVENT_ENTER = 1 << 0,
       POINTER_EVENT_LEAVE = 1 << 1,
       POINTER_EVENT_MOTION = 1 << 2,
       POINTER_EVENT_BUTTON = 1 << 3,
       POINTER_EVENT_AXIS = 1 << 4,
       POINTER_EVENT_AXIS_SOURCE = 1 << 5,
       POINTER_EVENT_AXIS_STOP = 1 << 6,
       POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};


struct pointer_event {
	uint32_t event_mask;
	wl_fixed_t surface_x, surface_y;
	uint32_t button, state;
	uint32_t time;
	uint32_t serial;
	struct {
		int valid;
		wl_fixed_t value;
		int32_t discrete;
	} axes[2];
	uint32_t axis_source;
};

struct {
	int width;
	int height;

	struct wl_surface *surface;
	struct wl_buffer *buffer;
} window;

struct client_state {
	struct wl_keyboard *wl_keyboard;
	struct xkb_state *xkb_state;
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;

	struct wl_pointer *wl_pointer;
	struct pointer_event pointer_event;
	struct {
		int x, y;
		int buttons;
		ulong msec;
	} mouse;
};
Memimage *gscreen;

static void randname(char *buf) {                                                                  
        struct timespec ts;                                                                        
        clock_gettime(CLOCK_REALTIME, &ts);                                                        
        long r = ts.tv_nsec;                                                                       
        for (int i = 0; i < 6; ++i) {                                                              
                buf[i] = 'A'+(r&15)+(r&16)*2;                                                      
                r >>= 5;                                                                           
        }                                                                                          
}                                                                                                  
                                                                                                   
static int anonymous_shm_open(void) {                                                              
        char name[] = "/hello-wayland-XXXXXX";                                                     
        int retries = 100;                                                                         
                                                                                                   
        do {                                                                                       
                randname(name + strlen(name) - 6);                                                 
                                                                                                   
                --retries;                                                                         
                // shm_open guarantees that O_CLOEXEC is set                                       
                int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);                          
                if (fd >= 0) {                                                                     
                        shm_unlink(name);                                                          
                        return fd;                                                                 
                }                                                                                  
        } while (retries > 0 && errno == EEXIST);                                                  
                                                                                                   
        return -1;                                                                                 
}                                                                                                  
                                                                                                   
int create_shm_file(off_t size) {                                                                  
        int fd = anonymous_shm_open();                                                             
        if (fd < 0) {                                                                              
                return fd;                                                                         
        }                                                                                          
                                                                                                   
        if (ftruncate(fd, size) < 0) {                                                             
                close(fd);                                                                         
                return -1;                                                                         
        }                                                                                          
                                                                                                   
        return fd;                                                                                 
}                             

Memdata*           
attachscreen(Rectangle *r, ulong *chan, int *depth, int *width, int *softscreen)
{    
	gscreen->data->ref++;
	*r = gscreen->clipr;
	*chan = gscreen->chan;
	*depth = gscreen->depth;
	*width = gscreen->width;
	*softscreen = 1;

	return gscreen->data;
}

void                                             
screensize(Rectangle r, ulong chan)              
{                                                
        Memimage *i;                             
                                                 
        if((i = allocmemimage(r, chan)) == nil)  
                return;                          
        if(gscreen != nil)                     
                freememimage(gscreen);         
        gscreen = i;                           
        gscreen->clipr = ZR;                   
}

char*              
clipread(void)
{                            
        return 0;
}           
             
int                          
clipwrite(char *buf)   
{                       
        return 0;
}             

       
void               
guimain(void)
{                            
        cpubody();
}           

struct wl_display *wldisplay;
struct wl_compositor *compositor = nil;
struct wl_shm *shm = nil;
struct wl_shell *shell;
struct xdg_wm_base *xdg_wm_base=nil;
struct wl_seat *wl_seat;
struct wl_pointer *wl_pointer;
struct wl_touch *wl_touch;


static struct wl_buffer *create_buffer(int width, int height) {
	printf("Creating buffer %dx%d\n", width, height);
	int stride = width*4;
	int size = stride * height;

	int fd = create_shm_file(size);
	if (fd < 0) {
		panic("create_shm_file");
	}

	shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(shm_data == MAP_FAILED) {
		panic("mmap failed");
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
		WL_SHM_FORMAT_ARGB8888);
	return buffer;
}

static void
wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t format, int32_t fd, uint32_t size)
{
	fprintf(stderr, "key map");
	struct client_state *state = data;
       assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);

       char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
       assert(map_shm != MAP_FAILED);

       struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
                       state->xkb_context, map_shm,
                       XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
       munmap(map_shm, size);
       close(fd);

       struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
       xkb_keymap_unref(state->xkb_keymap);
       xkb_state_unref(state->xkb_state);
       state->xkb_keymap = xkb_keymap;
       state->xkb_state = xkb_state;
}

static void
wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, struct wl_surface *surface,
               struct wl_array *keys)
{
	/*
	struct client_state *state = data;
       fprintf(stderr, "keyboard enter; keys pressed are:\n");
       uint32_t *key;
       wl_array_for_each(key, keys) {
               char buf[128];
               xkb_keysym_t sym = xkb_state_key_get_one_sym(
                               state->xkb_state, *key + 8);
               xkb_keysym_get_name(sym, buf, sizeof(buf));
               fprintf(stderr, "sym: %-12s (%d), ", buf, sym);
               xkb_state_key_get_utf8(state->xkb_state,
                               *key + 8, buf, sizeof(buf));
               fprintf(stderr, "utf8: '%s'\n", buf);
       }
       fprintf(stderr,"keyboard enter done");
*/
}

QLock kblock;

static void
wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	Rune k;

       char buf[128];
	struct client_state *client_state = data;
       uint32_t keycode = key + 8;
       xkb_keysym_t sym = xkb_state_key_get_one_sym(
                       client_state->xkb_state, keycode);

       xkb_keysym_get_name(sym, buf, sizeof(buf));
       //const char *action =
       //        state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release";
       // fprintf(stderr, "key %s: sym: %-12s (%d), %x %x", action, buf, sym, keycode, sym);
       xkb_state_key_get_utf8(client_state->xkb_state, keycode,
                       buf, sizeof(buf));
       // fprintf(stderr, "utf8: '%s'\n", buf);
       chartorune(&k, buf);

       if(k == XKB_KEY_Multi_key || k == XKB_KEY_NoSymbol) {
	       // fprintf(stderr, "multikey or no symbol\n");
	       return;
       }
       assert(k < 128);

       // Map enter/return to \r, FIXME: Base on keycode, not k.
       // FIXME: Handle alt
       // FIXME: Handle lock when 2 keys pushed at once
       // FIXME: Check return code of chartorune?
       if (k == '\r') {
	       k = '\n';
       }
// fprintf(stderr, "kbdkey(%x, %d) serial:%d time:%d\n", k, state == WL_KEYBOARD_KEY_STATE_PRESSED, serial, time);
	kbdkey(k, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

static void
wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, struct wl_surface *surface)
{
       fprintf(stderr, "keyboard leave\n");
}

static void
wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
               uint32_t serial, uint32_t mods_depressed,
               uint32_t mods_latched, uint32_t mods_locked,
               uint32_t group)
{
       struct client_state *state = data;
       fprintf(stderr, "keyboard modifiers\n");
       xkb_state_update_mask(state->xkb_state,
               mods_depressed, mods_latched, mods_locked, 0, 0, group);
	fprintf(stderr, "done keyboard modifiers\n");
}

static void
wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
               int32_t rate, int32_t delay)
{
       fprintf(stderr, "keyboard repeat info\n");
       /* Left as an exercise for the reader */
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
	.keymap = wl_keyboard_keymap,
	.enter = wl_keyboard_enter,
	.leave = wl_keyboard_leave,
	.key = wl_keyboard_key,
	.modifiers = wl_keyboard_modifiers,
	.repeat_info = wl_keyboard_repeat_info,
};

static void
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
               uint32_t serial, struct wl_surface *surface,
               wl_fixed_t surface_x, wl_fixed_t surface_y)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_ENTER;
       client_state->pointer_event.serial = serial;
       client_state->pointer_event.surface_x = surface_x,
               client_state->pointer_event.surface_y = surface_y;
}

static void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
               uint32_t serial, struct wl_surface *surface)
{
       struct client_state *client_state = data;
       client_state->pointer_event.serial = serial;
       client_state->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}

static void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               wl_fixed_t surface_x, wl_fixed_t surface_y)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_MOTION;
       client_state->pointer_event.time = time;
       client_state->pointer_event.surface_x = surface_x,
               client_state->pointer_event.surface_y = surface_y;
}

static void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
               uint32_t time, uint32_t button, uint32_t state)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
       client_state->pointer_event.time = time;
       client_state->pointer_event.serial = serial;
       client_state->pointer_event.button = button,
       client_state->pointer_event.state = state;
}

static void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               uint32_t axis, wl_fixed_t value)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS;
       client_state->pointer_event.time = time;
       client_state->pointer_event.axes[axis].valid = 1;
       client_state->pointer_event.axes[axis].value = value;
}

static void
wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis_source)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
       client_state->pointer_event.axis_source = axis_source;
}

static void
wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
               uint32_t time, uint32_t axis)
{
       struct client_state *client_state = data;
       client_state->pointer_event.time = time;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
       client_state->pointer_event.axes[axis].valid = 1;
}

static void
wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis, int32_t discrete)
{
       struct client_state *client_state = data;
       client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
       client_state->pointer_event.axes[axis].valid = 1;
       client_state->pointer_event.axes[axis].discrete = discrete;
}

static void
wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
       struct client_state *client_state = data;
       struct pointer_event *event = &client_state->pointer_event;
//        fprintf(stderr, "pointer frame @ %d: ", event->time);

       if (event->event_mask & POINTER_EVENT_ENTER) {
		client_state->mouse.x = wl_fixed_to_int(event->surface_x);
		client_state->mouse.y = wl_fixed_to_int(event->surface_y);
/*
               fprintf(stderr, "entered %f, %f ",
                               wl_fixed_to_double(event->surface_x),
                               wl_fixed_to_double(event->surface_y));
*/
       }

       if (event->event_mask & POINTER_EVENT_LEAVE) {
		//return;
               //fprintf(stderr, "leave");
       }

       if (event->event_mask & POINTER_EVENT_MOTION) {
		client_state->mouse.x = wl_fixed_to_int(event->surface_x);
		client_state->mouse.y = wl_fixed_to_int(event->surface_y);
		/*
               fprintf(stderr, "motion %f, %f ",
                               wl_fixed_to_double(event->surface_x),
                               wl_fixed_to_double(event->surface_y));
*/
       }

       if (event->event_mask & POINTER_EVENT_BUTTON) {
               char *state = event->state == WL_POINTER_BUTTON_STATE_RELEASED ?
                       "released" : "pressed";
		int btn = 0;
		switch(event->button) {
		case BTN_LEFT:
			btn = 1 << 0;
			break;
		case BTN_MIDDLE:
			btn = 1 << 1;
			break;
		case BTN_RIGHT:
			btn = 1 << 2;
			break;
		}
		if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
			client_state->mouse.buttons &= ~(btn);
		} else {
			client_state->mouse.buttons |= btn;
		}
               // fprintf(stderr, "button %d %s ", event->button, state);
		
       }
	absmousetrack(client_state->mouse.x, client_state->mouse.y, client_state->mouse.buttons, event->time);
/*

       uint32_t axis_events = POINTER_EVENT_AXIS
               | POINTER_EVENT_AXIS_SOURCE
               | POINTER_EVENT_AXIS_STOP
               | POINTER_EVENT_AXIS_DISCRETE;
       char *axis_name[2] = {
               [WL_POINTER_AXIS_VERTICAL_SCROLL] = "vertical",
               [WL_POINTER_AXIS_HORIZONTAL_SCROLL] = "horizontal",
       };
       char *axis_source[4] = {
               [WL_POINTER_AXIS_SOURCE_WHEEL] = "wheel",
               [WL_POINTER_AXIS_SOURCE_FINGER] = "finger",
               [WL_POINTER_AXIS_SOURCE_CONTINUOUS] = "continuous",
               [WL_POINTER_AXIS_SOURCE_WHEEL_TILT] = "wheel tilt",
       };
       if (event->event_mask & axis_events) {
               for (size_t i = 0; i < 2; ++i) {
                       if (!event->axes[i].valid) {
                               continue;
                       }
                       fprintf(stderr, "%s axis ", axis_name[i]);
                       if (event->event_mask & POINTER_EVENT_AXIS) {
                               fprintf(stderr, "value %f ", wl_fixed_to_double(
                                                       event->axes[i].value));
                       }
                       if (event->event_mask & POINTER_EVENT_AXIS_DISCRETE) {
                               fprintf(stderr, "discrete %d ",
                                               event->axes[i].discrete);
                       }
                       if (event->event_mask & POINTER_EVENT_AXIS_SOURCE) {
                               fprintf(stderr, "via %s ",
                                               axis_source[event->axis_source]);
                       }
                       if (event->event_mask & POINTER_EVENT_AXIS_STOP) {
                               fprintf(stderr, "(stopped) ");
                       }
               }
       }

       fprintf(stderr, "\n");
*/
       memset(event, 0, sizeof(*event));
}

static const struct wl_pointer_listener wl_pointer_listener = {
       .enter = wl_pointer_enter,
       .leave = wl_pointer_leave,
       .motion = wl_pointer_motion,
       .button = wl_pointer_button,
       .axis = wl_pointer_axis,
       .frame = wl_pointer_frame,
       .axis_source = wl_pointer_axis_source,
       .axis_stop = wl_pointer_axis_stop,
       .axis_discrete = wl_pointer_axis_discrete,
};
static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	struct client_state *state = data;
	int have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

	if(have_pointer && state->wl_pointer == nil) {
		state->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(state->wl_pointer,
			&wl_pointer_listener, state);
	} else if (!have_pointer && state->wl_pointer != nil) {
		wl_pointer_release(state->wl_pointer);
		state->wl_pointer = nil;
	}
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		fprintf(stderr, "Have keyboard\n");
		state->wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(state->wl_keyboard, &wl_keyboard_listener, state);
	} else if (state->wl_keyboard != nil) {
		fprintf(stderr, "Releasing keyboard\n");
		wl_keyboard_release(state->wl_keyboard);
		state->wl_keyboard = nil;
	}
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
	fprintf(stderr, "seat name: %s\n", name);
}

static const struct wl_seat_listener wl_seat_listener = {
	.capabilities = wl_seat_capabilities,
	.name = wl_seat_name,
};

void                   
flushmemscreen(Rectangle r)
{
	printf("flushmemscreen\n");

	assert(!canqlock(&drawlock));
	if(rectclip(&r, gscreen->clipr) == 0) {
		return;
	}

	if(!window.buffer) {
		return;
	}
	assert(window.buffer);
	assert(window.surface);
	// window.buffer = create_buffer(window.width, window.height);
	memcpy(shm_data, gscreen->data->bdata, 4*window.width*window.height);

	wl_surface_attach(window.surface, window.buffer, 0, 0);
	wl_surface_damage(window.surface, r.min.x, r.min.y, r.max.x-r.min.x, r.max.y-r.min.y);
	wl_surface_commit(window.surface);

	wl_display_flush(wldisplay);
	wl_display_dispatch(wldisplay);
	printf("end of flushmemscreen\n");
	
/*
	memcpy(shm_data, gscreen->data, size);
	wl_surface_commit(surface);

*/
}


void
registry_global_handler(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	struct client_state *state = data;
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 3);
	} else if (strcmp(interface, "wl_shm") == 0) {
		shm = wl_registry_bind(registry, name,
				&wl_shm_interface, 1);
	} else if (strcmp(interface, "wl_shell") == 0) {
		shell = wl_registry_bind(registry, name,
				&wl_shell_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		wl_seat = wl_registry_bind(
			registry, name, &wl_seat_interface, 7);
		//wl_seat_add_listener(wl_seat, &wl_seat_listener, state);
		wl_seat_add_listener(wl_seat, &wl_seat_listener, state);
	}
}

void
registry_global_remove_handler(void *data, struct wl_registry *registry, uint32_t name)
{
}

const struct wl_registry_listener registry_listener = {
	.global = registry_global_handler,
	.global_remove = registry_global_remove_handler
};

static void xdg_surface_handle_configure(void *data,                            
                struct xdg_surface *xdg_surface, uint32_t serial) {                  
        xdg_surface_ack_configure(xdg_surface, serial);                                   
        wl_surface_commit(window.surface);                                            
}
static const struct xdg_surface_listener xdg_surface_listener = {                         
        .configure = xdg_surface_handle_configure,                                  
};

static void noop(void) {
}

static void xdg_toplevel_configure(
	void *data,
	struct xdg_toplevel *xdg_toplevel,
	int32_t width, int32_t height,
	struct wl_array *states)
{
	// struct client_state *state = data;
	if (width == 0 || height == 0) {
		printf("using own size\n");
		if (window.surface) {
			window.buffer = create_buffer(window.width, window.height);
		}
		return;
	}
	window.width = width;
	window.height = height;
	printf("want %dx%d\n", width, height);
	if (window.surface) {	
	window.buffer = create_buffer(window.width, window.height);
	}
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {                  
        .configure = xdg_toplevel_configure,                                                                
        .close = noop,
};     

void
wayproc(void *arg)
{
	struct client_state state;
	printf("wayproc\n");
	wldisplay = wl_display_connect(NULL);
	if (wldisplay == NULL) {
		panic("wl_display_connect");
	}


	printf("registry\n");
	struct wl_registry *registry = wl_display_get_registry(wldisplay);
	printf("new ctx\n");
	state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	wl_registry_add_listener(registry, &registry_listener, &state);

	printf("round trip\n");
	wl_display_roundtrip(wldisplay);

	if(shm == nil) panic("shm");
	if(compositor == nil) panic("compositor");
	if(xdg_wm_base ==nil) panic("xdg_wm_base");

	window.surface = wl_compositor_create_surface(compositor);

	struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, window.surface);
	struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

printf("adding listeners");
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &state);
        xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, &state);

	wl_surface_commit(window.surface);
printf("round tripping");
	wl_display_roundtrip(wldisplay);
printf("round tripped");
	while(wl_display_dispatch(wldisplay) != -1) {
		// keep going until the window is closed
	}
}
void
screeninit(void)
{
	printf("screeninit\n");
	Rectangle r;
	window.width = 1024;
	window.height = 768;

	kproc("wayscreen", wayproc, nil);
	memimageinit();

	r = ZR;

	r.max.x = window.width;
	r.max.y = window.height;

	screensize(r, ARGB32);

	assert(gscreen);
	gscreen->clipr = r;


	terminit();

	assert(gscreen->data);
	qlock(&drawlock);
	flushmemscreen(gscreen->clipr);
	qunlock(&drawlock);
}           

void setcursor(void)
{
	printf("setcursor\n");
}

void               
getcolor(ulong i, ulong *r, ulong *g, ulong *b)
{        
	printf("getcolor\n");
	*r = 3;
	*g = 5;
	*b = 5;
}

void                               
setcolor(ulong i, ulong r, ulong g, ulong b)
{                            
	printf("setcolor\n");
        /* no-op */                                                                                
}

void
mouseset(Point xy)
{
	printf("mouseset\n");
}
