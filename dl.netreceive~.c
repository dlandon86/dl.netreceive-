/* ------------------------ dl.netreceive~ ----------------------------------------/
*
* Sends uncompressed audio data over IP, from dl.netreceive~ to dl.netreceive~.
*
* Copyright (C) 2020 David Landon
*
* dl.netreceive~ utilizes Libuv and Pthread libraries, so those will be needed
* if you intent to modify.
*
* dl.netreceive~ is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* dl.netreceive~ is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY
* OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* See the GNU General Public License for more details.
*
* <http://www.gnu.org/licenses/>
*
* ---------------------------------------------------------------------------- */


#include "uv.h" 

#include "ext.h"			// standard Max include, always required (except in Jitter)
#include "ext_obex.h"		// required for "new" style objects
#include "z_dsp.h"			// required for MSP objects

#define HAVE_STRUCT_TIMESPEC
#include "pthread.h"

#define DEFAULT_AUDIO_CHANNELS 1	    /* default number of audio channels */
#define MAXIMUM_AUDIO_CHANNELS 32	    /* max. number of audio channels we support */
#define DEFAULT_AUDIO_BUFFER_SIZE 1024	/* number of samples in one audio block */
#define DEFAULT_UDP_PACKT_SIZE 8192		/* number of bytes we send in one UDP datagram (OS X only) */
#define DEFAULT_IP_ADDRESS  "0.0.0.0"   /* default network port number */
#define DEFAULT_PORT "8000"               /* default network port number */


#define UV_ERROR(msg, code) do {                                           \
  post("%s: [%s: %s]\n", msg, uv_err_name((code)), uv_strerror((code)));   \
  assert(0);                                                               \
} while(0);


// struct to represent the object's state
typedef struct _dlnetreceive {
	t_pxobject		ob;			// the object itself (t_pxobject in MSP instead of t_object)
	double			d_offset; 	// Test for now. To be removed after completion.
    long            d_channels;
    t_symbol        *d_ipaddr;
    t_symbol        *d_portno;

    uv_loop_t       *loop;
    uv_udp_t        recv_handle;
    uv_udp_send_t   send_req;
    struct sockaddr_in recv_addr;

    pthread_t       thread;

    uv_buf_t        buffer;

    long            vs;

    uv_loop_t event_loop_struct;
    uv_loop_t* event_loop_ptr;



} t_dlnetreceive;






static t_symbol* ps_nothing, * ps_localhost;
static t_symbol* ps_format, * ps_channels, * ps_framesize, * ps_overflow, * ps_underflow;
static t_symbol* ps_queuesize, * ps_average, * ps_sf_float, * ps_sf_16bit, * ps_sf_8bit;
static t_symbol* ps_sf_mp3, * ps_sf_aac, * ps_sf_unknown, * ps_bitrate, * ps_hostname;



// method prototypes
void *dlnetreceive_new(t_symbol *s, long argc, t_atom *argv);
void dlnetreceive_free(t_dlnetreceive *x);
void dlnetreceive_assist(t_dlnetreceive *x, void *b, long m, long a, char *s);
void dlnetreceive_float(t_dlnetreceive *x, double f);
void dlnetreceive_dsp64(t_dlnetreceive *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void dlnetreceive_perform64(t_dlnetreceive *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

void dlnetreceive_int(t_dlnetreceive* x, long n);
void recv_cb(uv_udp_t* req, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned int flags);
void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf);
void start_recv(t_dlnetreceive *x);
void sock_connect(t_dlnetreceive *x);
void thread_main(void* arg);
uv_loop_t* uv_event_loop(t_dlnetreceive* x);



// global class pointer variable
static t_class *dlnetreceive_class = NULL;


//***********************************************************************************************

void ext_main(void *r)
{
	// object initialization, note the use of dsp_free for the freemethod, which is required
	// unless you need to free allocated memory, in which case you should call dsp_free from
	// your custom free function.

	t_class *c = class_new("dl.netreceive~", (method)dlnetreceive_new, (method)dsp_free, (long)sizeof(t_dlnetreceive), 0L, A_GIMME, 0);

	class_addmethod(c, (method)dlnetreceive_float,		"float",	A_FLOAT, 0);
	class_addmethod(c, (method)dlnetreceive_dsp64,		"dsp64",	A_CANT, 0);
	class_addmethod(c, (method)dlnetreceive_assist,	    "assist",	A_CANT, 0);
    class_addmethod(c, (method)dlnetreceive_int,         "int",      A_LONG, 0);

	class_dspinit(c);
	class_register(CLASS_BOX, c);
	dlnetreceive_class = c;
}


void *dlnetreceive_new(t_symbol *s, long argc, t_atom *argv)
{
	t_dlnetreceive *x = (t_dlnetreceive *)object_alloc(dlnetreceive_class);

    t_atom* ap;
    ap = argv;

    ps_nothing = gensym("");
    ps_localhost = gensym("localhost");
    ps_hostname = gensym("ipaddr");
    ps_format = gensym("format");
    ps_channels = gensym("channels");
    ps_framesize = gensym("framesize");
    ps_bitrate = gensym("bitrate");
    ps_sf_float = gensym("_float_");
    ps_sf_16bit = gensym("_16bit_");
    ps_sf_8bit = gensym("_8bit_");
    ps_sf_mp3 = gensym("_mp3_");
    ps_sf_aac = gensym("_aac_");
    ps_sf_unknown = gensym("_unknown_");


	if (x) {

        x->d_offset = 0.0;

        x->d_channels = atom_getlong(ap);
        if (x->d_channels > 0 && x->d_channels <= MAXIMUM_AUDIO_CHANNELS)
        {
            post("dl.netreceive~: channels set to %d", x->d_channels);
        }
        else
        {
            x->d_channels = DEFAULT_AUDIO_CHANNELS;
            post("dl.netreceive~: Channel argument missing or outside allowable range. Channels set to %d", x->d_channels);
        }

		dsp_setup((t_pxobject *)x, x->d_channels);	// MSP inlets: arg is # of inlets and is REQUIRED!
		// use 0 if you don't need inlets
		outlet_new(x, "signal"); 		// signal outlet (note "signal" rather than NULL)

        // Set IP Address
        x->d_ipaddr = gensym(atom_getsym(ap + 1)->s_name);

        if (x->d_ipaddr->s_name != ps_nothing->s_name)
        {
            x->d_ipaddr = gensym(atom_getsym(ap + 1)->s_name);
            post("dl.netreceive~: Ip address set to %s", x->d_ipaddr->s_name);
        }
        else
        {
            x->d_ipaddr = gensym(DEFAULT_IP_ADDRESS);
            post("dl.netreceive~: IP Address argument missing. set to %s", x->d_ipaddr->s_name);
        }

        // Set port number
        x->d_portno = gensym(atom_getsym(ap + 2)->s_name);

        if (x->d_portno->s_name != ps_nothing->s_name)
        {
            x->d_portno = gensym(atom_getsym(ap + 2)->s_name);
            post("dl.netreceive~: Port number set to %s", x->d_portno->s_name);
        }
        else {
            x->d_portno = gensym(DEFAULT_PORT);
            post("dl.netreceive~: Port number argument missing. set to %s", x->d_portno->s_name);
        }
        
        x->buffer.base = (char*)malloc(sizeof(double) * sys_getblksize());
        x->buffer.len = sys_getblksize() * sizeof(double);

	}
	return (x);
}


// NOT CALLED!, we use dsp_free for a generic free function
void dlnetreceive_free(t_dlnetreceive *x)
{
	;
}


void dlnetreceive_assist(t_dlnetreceive *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { //inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}


void dlnetreceive_float(t_dlnetreceive *x, double f)
{
	x->d_offset = f;
}


// registers a function for the signal chain in Max
void dlnetreceive_dsp64(t_dlnetreceive *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	post("my sample rate is: %f", samplerate);

    x->vs = maxvectorsize;
    //x->buffer.base = (char*)malloc(sizeof(double) * maxvectorsize);
    //x->buffer.len = maxvectorsize * sizeof(double);

	object_method(dsp64, gensym("dsp_add64"), x, dlnetreceive_perform64, 0, NULL);
}


// this is the 64-bit perform method audio vectors
void dlnetreceive_perform64(t_dlnetreceive *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    t_double* in1a = ins[0];
    t_double *in1b = malloc(sampleframes * sizeof(double));	
	t_double *outL = outs[0];
	int n = sampleframes;

    memcpy(in1b, x->buffer.base, sampleframes * sizeof(double));

	// this perform method simply copies the input to the output, offsetting the value
	while (n--)
		*outL++ = *in1b++;
}

void dlnetreceive_int(t_dlnetreceive* x, long n)
{
    if (n != 0) {
        sock_connect(x);
    }
    else {
        uv_udp_recv_stop(&x->recv_handle); //without this, the loop runs even after a message is received.
    }
}


void recv_cb(uv_udp_t* req, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned int flags) {

    t_dlnetreceive* x = req->data;

    if (nread < 0) {
        // there seems to be no way to get an error code here (none of the udp tests do)
        post("recv error unexpected\n");
        uv_close((uv_handle_t*)req, NULL);
        free(buf->base);
        return;
    }

    post("Bytes read: %d", nread);
    post("%s", buf->base);

    char sender[17] = { 0 };
    uv_ip4_name((struct sockaddr_in*) addr, sender, 16);
    post("dlnetreceive: recv from %s\n", sender);

    //memcpy(x->buffer.base, buf->base, sizeof(double) * x->vs); // this is a precarious operation... look here for issues!

    free(buf->base);
}

void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    //t_dlnetreceive* x = handle->data;

    //buf->base = (char*)malloc(sizeof(double) * x->vs);
    //buf->len = x->vs;

    buf->base = malloc(size);
    buf->len = size;

    post("dl.netreceive: message received");
    assert(buf->base != NULL);
}

void start_recv(t_dlnetreceive *x) {
    int r;

    r = uv_ip4_addr("0.0.0.0", 9123, &x->recv_addr);
    if (r) UV_ERROR("ip4_addr", r);

    //x->recv_addr.sin6_family = AF_INET6;

    //  bind
    r = uv_udp_bind(&x->recv_handle, (const struct sockaddr*) &x->recv_addr, 0); // changed from 0 "unused"
    if (r) UV_ERROR("udp bind", r);

    //  start
    r = uv_udp_recv_start(&x->recv_handle, alloc_cb, recv_cb);
    if (r) UV_ERROR("recv start", r);
}

void sock_connect(t_dlnetreceive *x) {

    x->loop = uv_event_loop(x);

    uv_udp_init(x->loop, &x->recv_handle);

    start_recv(x);

    pthread_create(&x->thread, NULL, thread_main, x);

}

void thread_main(void* arg)
{
    t_dlnetreceive* x = arg;

    post("dlnetreceive: Opening loop");
    uv_run(x->loop, UV_RUN_DEFAULT);
    post("dlnetreceive: loop closing");
}

uv_loop_t* uv_event_loop(t_dlnetreceive* x) {
    if (x->event_loop_ptr != NULL)
        return x->event_loop_ptr;

    if (uv_loop_init(&x->event_loop_struct))
        return NULL;

    x->event_loop_ptr = &x->event_loop_struct;
    return x->event_loop_ptr;
}
