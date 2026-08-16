#include <cairo/cairo.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define NOTIF_W    360
#define NOTIF_H    72
#define NOTIF_PAD  14
#define NOTIF_GAP  8
#define NOTIF_DUR  5000
#define NOTIF_MAX  8
#define FONT       "monospace"

#define BG_R 0.12f
#define BG_G 0.12f
#define BG_B 0.16f
#define FG_R 0.88f
#define FG_G 0.88f
#define FG_B 0.92f
#define AC_R 0.45f
#define AC_G 0.60f
#define AC_B 0.95f
#define ER_R 0.90f
#define ER_G 0.35f
#define ER_B 0.35f

typedef enum { URG_LOW=0, URG_NORMAL, URG_CRITICAL } urgency_t;

typedef struct {
    char      summary[256], body[512], app_name[128];
    urgency_t urgency;
    int64_t   expire_ms;
    uint32_t  id;
    bool      active, configured;
    int       width, height;
    struct wl_surface           *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer            *buffer;
} notif_t;

static struct {
    struct wl_display          *display;
    struct wl_registry         *registry;
    struct wl_compositor       *compositor;
    struct wl_shm              *shm;
    struct wl_seat              *seat;
    struct wl_pointer           *pointer;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_output           *output;
    notif_t  notifs[NOTIF_MAX];
    uint32_t next_id;
    int      sock_fd;
    bool     running;
    struct wl_surface *pointer_surface;
} app;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int shm_file(size_t sz)
{
    char n[] = "/kumnotify-XXXXXX";
    int fd = shm_open(n, O_RDWR|O_CREAT|O_EXCL, 0600);
    if (fd<0) return -1;
    shm_unlink(n);
    if (ftruncate(fd,(off_t)sz)<0) { close(fd); return -1; }
    return fd;
}

static struct wl_buffer *make_buf(int w, int h, uint8_t **out)
{
    int stride = w*4; size_t sz=(size_t)stride*h;
    int fd=shm_file(sz); if(fd<0) return NULL;
    uint8_t *data=mmap(NULL,sz,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(data==MAP_FAILED){close(fd);return NULL;}
    struct wl_shm_pool *pool=wl_shm_create_pool(app.shm,fd,(int32_t)sz);
    struct wl_buffer *buf=wl_shm_pool_create_buffer(pool,0,w,h,stride,WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool); close(fd); *out=data; return buf;
}

static void render(notif_t *n)
{
    if (!n->configured) return;
    if (n->buffer) wl_buffer_destroy(n->buffer);
    uint8_t *data=NULL;
    n->buffer=make_buf(n->width,n->height,&data);
    if (!n->buffer) return;
    cairo_surface_t *cs=cairo_image_surface_create_for_data(
        data,CAIRO_FORMAT_ARGB32,n->width,n->height,n->width*4);
    cairo_t *cr=cairo_create(cs);
    cairo_set_source_rgba(cr,BG_R,BG_G,BG_B,0.94);
    cairo_rectangle(cr,0,0,n->width,n->height); cairo_fill(cr);
    float ar=AC_R,ag=AC_G,ab=AC_B;
    if (n->urgency==URG_CRITICAL){ar=ER_R;ag=ER_G;ab=ER_B;}
    else if (n->urgency==URG_LOW){ar*=0.6f;ag*=0.6f;ab*=0.6f;}
    cairo_set_source_rgba(cr,ar,ag,ab,0.9);
    cairo_rectangle(cr,0,0,3,n->height); cairo_fill(cr);
    cairo_select_font_face(cr,FONT,CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr,13.5);
    cairo_set_source_rgba(cr,FG_R,FG_G,FG_B,1.0);
    cairo_move_to(cr,NOTIF_PAD,26); cairo_show_text(cr,n->summary);
    if (n->body[0]) {
        cairo_select_font_face(cr,FONT,CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr,12.0);
        cairo_set_source_rgba(cr,FG_R,FG_G,FG_B,0.70);
        cairo_move_to(cr,NOTIF_PAD,48); cairo_show_text(cr,n->body);
    }
    if (n->app_name[0]) {
        cairo_text_extents_t ext;
        cairo_set_font_size(cr,10.5);
        cairo_set_source_rgba(cr,FG_R,FG_G,FG_B,0.35);
        cairo_text_extents(cr,n->app_name,&ext);
        cairo_move_to(cr,n->width-ext.width-NOTIF_PAD,26);
        cairo_show_text(cr,n->app_name);
    }
    cairo_destroy(cr); cairo_surface_destroy(cs);
    wl_surface_attach(n->surface,n->buffer,0,0);
    wl_surface_damage(n->surface,0,0,n->width,n->height);
    wl_surface_commit(n->surface);
    munmap(data,(size_t)n->width*4*n->height);
}

static void restack(void)
{
    int y=NOTIF_GAP;
    for (int i=NOTIF_MAX-1;i>=0;i--) {
        if (!app.notifs[i].active) continue;
        zwlr_layer_surface_v1_set_margin(app.notifs[i].layer_surface,y,0,0,NOTIF_GAP);
        wl_surface_commit(app.notifs[i].surface);
        y+=app.notifs[i].height+NOTIF_GAP;
    }
}

static void dismiss(notif_t *n)
{
    if (!n->active) return;
    if (n->layer_surface){zwlr_layer_surface_v1_destroy(n->layer_surface);n->layer_surface=NULL;}
    if (n->surface){wl_surface_destroy(n->surface);n->surface=NULL;}
    if (n->buffer){wl_buffer_destroy(n->buffer);n->buffer=NULL;}
    n->active=n->configured=false;
    restack();
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    app.pointer_surface = surface;
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial,
    struct wl_surface *surface)
{
    app.pointer_surface = NULL;
}
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time,
    wl_fixed_t sx, wl_fixed_t sy) {}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
    if (state != WL_POINTER_BUTTON_STATE_PRESSED || !app.pointer_surface)
        return;
    for (int i = 0; i < NOTIF_MAX; i++) {
        if (app.notifs[i].active && app.notifs[i].surface == app.pointer_surface) {
            dismiss(&app.notifs[i]);
            app.pointer_surface = NULL;
            break;
        }
    }
}
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time,
    uint32_t axis, wl_fixed_t value) {}
static void ptr_frame(void *d, struct wl_pointer *p) {}
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t src) {}
static void ptr_axis_stop(void *d, struct wl_pointer *p,
    uint32_t time, uint32_t axis) {}
static void ptr_axis_discrete(void *d, struct wl_pointer *p,
    uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter=ptr_enter, .leave=ptr_leave, .motion=ptr_motion,
    .button=ptr_button, .axis=ptr_axis, .frame=ptr_frame,
    .axis_source=ptr_axis_source, .axis_stop=ptr_axis_stop,
    .axis_discrete=ptr_axis_discrete,
};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app.pointer) {
        app.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app.pointer, &pointer_listener, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities=seat_caps, .name=seat_name,
};

static void lsc_configure(void *d, struct zwlr_layer_surface_v1 *ls,
    uint32_t serial, uint32_t w, uint32_t h)
{
    notif_t *n=d;
    n->width=NOTIF_W; n->height=NOTIF_H; n->configured=true;
    zwlr_layer_surface_v1_ack_configure(ls,serial);
    render(n);
}
static void lsc_closed(void *d, struct zwlr_layer_surface_v1 *ls) { dismiss(d); }
static const struct zwlr_layer_surface_v1_listener lsc_listener = {
    .configure=lsc_configure, .closed=lsc_closed,
};

static uint32_t show(const char *app_name, const char *summary,
    const char *body, urgency_t urg, int32_t timeout)
{
    notif_t *slot=NULL;
    int oldest=-1; int64_t oexp=INT64_MAX;
    for (int i=0;i<NOTIF_MAX;i++) {
        if (!app.notifs[i].active){slot=&app.notifs[i];break;}
        if (app.notifs[i].expire_ms<oexp){oexp=app.notifs[i].expire_ms;oldest=i;}
    }
    if (!slot){dismiss(&app.notifs[oldest]);slot=&app.notifs[oldest];}
    memset(slot,0,sizeof(*slot));
    slot->active=true; slot->id=++app.next_id; slot->urgency=urg;
    slot->width=NOTIF_W; slot->height=NOTIF_H;
    slot->expire_ms=now_ms()+(timeout>0?timeout:NOTIF_DUR);
    strncpy(slot->summary,summary?summary:"",255);
    strncpy(slot->body,body?body:"",511);
    strncpy(slot->app_name,app_name?app_name:"",127);
    slot->surface=wl_compositor_create_surface(app.compositor);
    slot->layer_surface=zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell,slot->surface,app.output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,"kumnotify");
    zwlr_layer_surface_v1_set_size(slot->layer_surface,NOTIF_W,NOTIF_H);
    zwlr_layer_surface_v1_set_anchor(slot->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_margin(slot->layer_surface,NOTIF_GAP,NOTIF_GAP,0,0);
    zwlr_layer_surface_v1_add_listener(slot->layer_surface,&lsc_listener,slot);
    wl_surface_commit(slot->surface);
    restack();
    return slot->id;
}

static void handle_msg(const char *msg)
{
    char app_n[128]={0}, sum[256]={0}, body[512]={0};
    int urg=1, timeout=-1;
    sscanf(msg,"NOTIFY %127[^|]|%255[^|]|%511[^|]|%d|%d",
        app_n, sum, body, &urg, &timeout);
    if (sum[0]) show(app_n, sum, body, (urgency_t)urg, timeout);
}

static void wlo_done(void *d, struct wl_output *o) {}
static void wlo_geom(void *d, struct wl_output *o, int x, int y, int pw, int ph,
    int sp, const char *mk, const char *mo, int t) {}
static void wlo_mode(void *d, struct wl_output *o, uint32_t f, int w, int h, int r) {}
static void wlo_scale(void *d, struct wl_output *o, int32_t s) {}
static void wlo_name(void *d, struct wl_output *o, const char *n) {}
static void wlo_desc(void *d, struct wl_output *o, const char *dc) {}
static const struct wl_output_listener wlo_listener = {
    .geometry=wlo_geom,.mode=wlo_mode,.done=wlo_done,
    .scale=wlo_scale,.name=wlo_name,.description=wlo_desc,
};

static void reg_global(void *d, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver)
{
    if (!strcmp(iface,wl_compositor_interface.name))
        app.compositor=wl_registry_bind(reg,name,&wl_compositor_interface,4);
    else if (!strcmp(iface,wl_shm_interface.name))
        app.shm=wl_registry_bind(reg,name,&wl_shm_interface,1);
    else if (!strcmp(iface,zwlr_layer_shell_v1_interface.name))
        app.layer_shell=wl_registry_bind(reg,name,&zwlr_layer_shell_v1_interface,1);
    else if (!strcmp(iface,wl_seat_interface.name)) {
        app.seat=wl_registry_bind(reg,name,&wl_seat_interface,7);
        wl_seat_add_listener(app.seat,&seat_listener,NULL);
    }
    else if (!strcmp(iface,wl_output_interface.name)&&!app.output) {
        app.output=wl_registry_bind(reg,name,&wl_output_interface,4);
        wl_output_add_listener(app.output,&wlo_listener,NULL);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_listener = {
    .global=reg_global,.global_remove=reg_remove,
};

int main(int argc, char *argv[])
{
    memset(&app,0,sizeof(app));
    app.running=true; app.sock_fd=-1;

    app.display=wl_display_connect(NULL);
    if (!app.display){fprintf(stderr,"kumnotify: cannot connect\n");return 1;}
    app.registry=wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry,&reg_listener,NULL);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.compositor||!app.shm||!app.layer_shell) {
        fprintf(stderr,"kumnotify: missing protocols\n"); return 1;
    }

    const char *runtime=getenv("XDG_RUNTIME_DIR");
    char sock_path[256]={0};
    if (runtime) {
        snprintf(sock_path,sizeof(sock_path),"%s/kumnotify.sock",runtime);
        unlink(sock_path);
        int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        if (fd>=0) {
            struct sockaddr_un addr={0};
            addr.sun_family=AF_UNIX;
            strncpy(addr.sun_path,sock_path,sizeof(addr.sun_path)-1);
            if (bind(fd,(struct sockaddr*)&addr,sizeof(addr))==0) {
                setenv("KUMNOTIFY_SOCK",sock_path,1);
                app.sock_fd=fd;
            } else close(fd);
        }
    }

    int wl_fd=wl_display_get_fd(app.display);
    while (app.running) {
        wl_display_flush(app.display);
        int64_t nxt=INT64_MAX;
        for (int i=0;i<NOTIF_MAX;i++)
            if (app.notifs[i].active&&app.notifs[i].expire_ms<nxt)
                nxt=app.notifs[i].expire_ms;
        int tms=-1;
        if (nxt!=INT64_MAX){int64_t d=nxt-now_ms();tms=d>0?(int)d:0;}

        struct pollfd fds[2]; int nfds=0;
        fds[nfds].fd=wl_fd; fds[nfds].events=POLLIN; nfds++;
        if (app.sock_fd>=0){fds[nfds].fd=app.sock_fd;fds[nfds].events=POLLIN;nfds++;}
        poll(fds,nfds,tms);

        if (fds[0].revents&POLLIN) wl_display_dispatch(app.display);
        if (nfds>1&&(fds[1].revents&POLLIN)) {
            char buf[768]; ssize_t n=recv(app.sock_fd,buf,sizeof(buf)-1,MSG_DONTWAIT);
            if (n>0){buf[n]='\0';handle_msg(buf);}
        }
        int64_t now=now_ms();
        for (int i=0;i<NOTIF_MAX;i++)
            if (app.notifs[i].active&&now>=app.notifs[i].expire_ms)
                dismiss(&app.notifs[i]);
    }
    return 0;
}
